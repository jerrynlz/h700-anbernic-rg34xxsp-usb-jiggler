#define _POSIX_C_SOURCE 200809L

#include "app_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    DEFAULT_HELPER_TIMEOUT_MS = 30000,
    HELPER_POLL_MS = 25,
    HELPER_TERM_GRACE_MS = 7000,
    REPORT_POLL_MS = 1000
};

struct AppRuntime {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    pthread_t worker;
    AppRuntimeConfig config;
    char *setup_script;
    char *cleanup_script;
    char *hid_device;
    AppRuntimeState state;
    JigglerSettings settings;
    uint64_t report_count;
    uint64_t last_report_ms;
    uint64_t next_report_ms;
    uint64_t cycle_started_ms;
    int cycle_target_x;
    int cycle_target_y;
    unsigned int cleanup_count;
    uint32_t random_state;
    int hid_fd;
    bool setup_owned;
    bool prepare_requested;
    bool shutdown_requested;
    bool cycle_in_progress;
    bool stop_after_cycle;
    bool worker_started;
    bool worker_joined;
    bool worker_finished;
    int shutdown_result;
    char error[192];
};

typedef struct {
    int x;
    int y;
} MovementVector;

static uint64_t monotonic_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void monotonic_after_ms(struct timespec *deadline, uint64_t milliseconds) {
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec += (time_t)(milliseconds / 1000U);
    deadline->tv_nsec += (long)(milliseconds % 1000U) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

static char *copy_string(const char *value) {
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    length = strlen(value) + 1U;
    copy = malloc(length);
    if (copy != NULL) {
        memcpy(copy, value, length);
    }
    return copy;
}

static void set_error_locked(AppRuntime *runtime, AppRuntimeState state, const char *message) {
    runtime->state = state;
    snprintf(runtime->error, sizeof(runtime->error), "%s", message != NULL ? message : "unknown error");
    pthread_cond_broadcast(&runtime->condition);
}

static bool cancellation_requested(AppRuntime *runtime) {
    bool cancelled;

    pthread_mutex_lock(&runtime->mutex);
    cancelled = runtime->shutdown_requested;
    pthread_mutex_unlock(&runtime->mutex);
    return cancelled;
}

static int sleep_ms(unsigned int milliseconds) {
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000U),
        .tv_nsec = (long)(milliseconds % 1000U) * 1000000L
    };

    return nanosleep(&duration, NULL);
}

static void terminate_helper(pid_t child) {
    uint64_t deadline;
    int status;

    kill(-child, SIGTERM);
    deadline = monotonic_ms() + HELPER_TERM_GRACE_MS;
    while (monotonic_ms() < deadline) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child || (result < 0 && errno == ECHILD)) {
            return;
        }
        if (result < 0 && errno != EINTR) {
            break;
        }
        if (sleep_ms(HELPER_POLL_MS) != 0 && errno != EINTR) {
            break;
        }
    }
    kill(-child, SIGKILL);
    for (unsigned int attempt = 0; attempt < 8; attempt++) {
        if (waitpid(child, &status, 0) >= 0 || errno == ECHILD) {
            break;
        }
        if (errno != EINTR) {
            break;
        }
    }
}

static int run_helper(AppRuntime *runtime, const char *script, bool cancellable) {
    pid_t child;
    uint64_t deadline;
    int status;

    child = fork();
    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        setpgid(0, 0);
        execl("/bin/sh", "sh", script, (char *)NULL);
        _exit(127);
    }
    setpgid(child, child);
    deadline = monotonic_ms() + runtime->config.helper_timeout_ms;
    for (;;) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
        }
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        if ((cancellable && cancellation_requested(runtime)) || monotonic_ms() >= deadline) {
            terminate_helper(child);
            return -2;
        }
        if (sleep_ms(HELPER_POLL_MS) != 0 && errno != EINTR) {
            terminate_helper(child);
            return -1;
        }
    }
}

static int open_hid(AppRuntime *runtime) {
    struct stat status;
    int fd;

    fd = open(runtime->hid_device, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &status) != 0 || (!runtime->config.allow_regular_hid && !S_ISCHR(status.st_mode))) {
        close(fd);
        errno = ENODEV;
        return -1;
    }
    return fd;
}

static int write_report(AppRuntime *runtime, const uint8_t report[4]) {
    struct pollfd descriptor = {
        .fd = runtime->hid_fd,
        .events = POLLOUT
    };
    int result;
    ssize_t written;

    result = poll(&descriptor, 1, REPORT_POLL_MS);
    if (result <= 0 || !(descriptor.revents & POLLOUT)) {
        errno = result == 0 ? ETIMEDOUT : errno;
        return -1;
    }
    written = write(runtime->hid_fd, report, 4);
    if (written != 4) {
        if (written >= 0) {
            errno = EIO;
        }
        return -1;
    }
    return 0;
}

static uint32_t next_random(uint32_t *state) {
    uint32_t value = *state != 0 ? *state : 0x6d2b79f5U;

    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

size_t app_runtime_build_reports(const JigglerSettings *settings, uint32_t *random_state,
                                 uint8_t reports[4][4]) {
    int movement;

    if (settings == NULL || random_state == NULL || reports == NULL) {
        return 0;
    }
    movement = settings->move_px;
    if (movement < 1) {
        movement = 1;
    } else if (movement > 127) {
        movement = 127;
    }
    memset(reports, 0, 16);

    switch (settings->pattern) {
        case JIGGLE_HORIZONTAL:
            reports[0][1] = (uint8_t)movement;
            reports[1][1] = (uint8_t)(-movement);
            return 2;
        case JIGGLE_VERTICAL:
            reports[0][2] = (uint8_t)movement;
            reports[1][2] = (uint8_t)(-movement);
            return 2;
        case JIGGLE_SQUARE:
            reports[0][1] = (uint8_t)movement;
            reports[1][2] = (uint8_t)movement;
            reports[2][1] = (uint8_t)(-movement);
            reports[3][2] = (uint8_t)(-movement);
            return 4;
        case JIGGLE_RANDOM: {
            uint32_t value = next_random(random_state);
            int x = (int)(value % (uint32_t)(movement * 2 + 1)) - movement;
            int y = (int)((value >> 16) % (uint32_t)(movement * 2 + 1)) - movement;
            if (x == 0 && y == 0) {
                x = movement;
            }
            reports[0][1] = (uint8_t)x;
            reports[0][2] = (uint8_t)y;
            reports[1][1] = (uint8_t)(-x);
            reports[1][2] = (uint8_t)(-y);
            return 2;
        }
        default:
            return 0;
    }
}

static size_t build_movement_vectors(const JigglerSettings *settings, uint32_t *random_state,
                                     MovementVector vectors[4]) {
    int movement;

    if (settings == NULL || random_state == NULL || vectors == NULL) return 0;
    movement = settings->move_px;
    if (movement < JIGGLER_MIN_MOVE_PX) movement = JIGGLER_MIN_MOVE_PX;
    if (movement > JIGGLER_MAX_MOVE_PX) movement = JIGGLER_MAX_MOVE_PX;
    memset(vectors, 0, sizeof(MovementVector) * 4U);

    if (settings->pattern == JIGGLE_HORIZONTAL) {
        vectors[0].x = movement;
        vectors[1].x = -movement;
        return 2;
    }
    if (settings->pattern == JIGGLE_VERTICAL) {
        vectors[0].y = movement;
        vectors[1].y = -movement;
        return 2;
    }
    if (settings->pattern == JIGGLE_SQUARE) {
        vectors[0].x = movement;
        vectors[1].y = movement;
        vectors[2].x = -movement;
        vectors[3].y = -movement;
        return 4;
    }
    if (settings->pattern == JIGGLE_RANDOM) {
        uint32_t value = next_random(random_state);
        int x = (int)(value % (uint32_t)(movement * 2 + 1)) - movement;
        int y = (int)((value >> 16) % (uint32_t)(movement * 2 + 1)) - movement;
        if (x == 0 && y == 0) x = movement;
        vectors[0].x = x;
        vectors[0].y = y;
        vectors[1].x = -x;
        vectors[1].y = -y;
        return 2;
    }
    return 0;
}

int app_runtime_interpolate_step(int total_x, int total_y, unsigned int frame, uint8_t report[4]) {
    int previous_x;
    int previous_y;
    int target_x;
    int target_y;

    if (report == NULL || frame == 0 || frame > APP_RUNTIME_SEGMENT_FRAMES ||
        total_x < -JIGGLER_MAX_MOVE_PX || total_x > JIGGLER_MAX_MOVE_PX ||
        total_y < -JIGGLER_MAX_MOVE_PX || total_y > JIGGLER_MAX_MOVE_PX) {
        return -1;
    }
    previous_x = total_x * (int)(frame - 1U) / APP_RUNTIME_SEGMENT_FRAMES;
    previous_y = total_y * (int)(frame - 1U) / APP_RUNTIME_SEGMENT_FRAMES;
    target_x = total_x * (int)frame / APP_RUNTIME_SEGMENT_FRAMES;
    target_y = total_y * (int)frame / APP_RUNTIME_SEGMENT_FRAMES;
    report[0] = 0;
    report[1] = (uint8_t)(target_x - previous_x);
    report[2] = (uint8_t)(target_y - previous_y);
    report[3] = 0;
    return 0;
}

static void wait_report_delay(AppRuntime *runtime, unsigned int milliseconds) {
    struct timespec deadline;

    monotonic_after_ms(&deadline, milliseconds);
    pthread_mutex_lock(&runtime->mutex);
    pthread_cond_timedwait(&runtime->condition, &runtime->mutex, &deadline);
    pthread_mutex_unlock(&runtime->mutex);
}

static int emit_cycle(AppRuntime *runtime, const JigglerSettings *settings) {
    MovementVector vectors[4];
    size_t vector_count;

    vector_count = build_movement_vectors(settings, &runtime->random_state, vectors);
    if (vector_count == 0) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&runtime->mutex);
    runtime->cycle_target_x = vectors[0].x;
    runtime->cycle_target_y = vectors[0].y;
    pthread_mutex_unlock(&runtime->mutex);
    for (size_t vector_index = 0; vector_index < vector_count; vector_index++) {
        int total_x = vectors[vector_index].x;
        int total_y = vectors[vector_index].y;
        int frames = APP_RUNTIME_SEGMENT_FRAMES;

        for (int frame = 1; frame <= frames; frame++) {
            uint8_t report[4];

            if (app_runtime_interpolate_step(total_x, total_y, (unsigned int)frame, report) != 0) return -1;
            if (write_report(runtime, report) != 0) return -1;
            pthread_mutex_lock(&runtime->mutex);
            runtime->report_count++;
            runtime->last_report_ms = monotonic_ms();
            pthread_mutex_unlock(&runtime->mutex);
            if (vector_index + 1U < vector_count || frame < frames) {
                wait_report_delay(runtime, APP_RUNTIME_REPORT_FRAME_MS);
            }
        }
    }
    return 0;
}

static void close_hid_locked(AppRuntime *runtime) {
    if (runtime->hid_fd >= 0) {
        close(runtime->hid_fd);
        runtime->hid_fd = -1;
    }
}

static int cleanup_owned(AppRuntime *runtime) {
    bool should_cleanup;
    int result = 0;

    pthread_mutex_lock(&runtime->mutex);
    should_cleanup = runtime->setup_owned;
    close_hid_locked(runtime);
    if (should_cleanup) {
        runtime->cleanup_count++;
    }
    pthread_mutex_unlock(&runtime->mutex);
    if (should_cleanup) {
        result = run_helper(runtime, runtime->cleanup_script, false);
        if (result == 0) {
            pthread_mutex_lock(&runtime->mutex);
            runtime->setup_owned = false;
            pthread_mutex_unlock(&runtime->mutex);
        }
    }
    return result;
}

static void prepare_runtime(AppRuntime *runtime) {
    int helper_result;
    int hid_fd;

    if (cleanup_owned(runtime) != 0) {
        pthread_mutex_lock(&runtime->mutex);
        if (!runtime->shutdown_requested) {
            set_error_locked(runtime, RUNTIME_ERROR, "HID cleanup failed; refusing retry to preserve owned state");
        }
        pthread_mutex_unlock(&runtime->mutex);
        return;
    }
    if (cancellation_requested(runtime)) {
        return;
    }
    helper_result = run_helper(runtime, runtime->setup_script, true);
    if (helper_result != 0) {
        pthread_mutex_lock(&runtime->mutex);
        if (!runtime->shutdown_requested) {
            set_error_locked(runtime, RUNTIME_ERROR,
                             helper_result == -2 ? "HID setup timed out or was cancelled" : "HID setup failed; check the setup log");
        }
        pthread_mutex_unlock(&runtime->mutex);
        return;
    }

    pthread_mutex_lock(&runtime->mutex);
    runtime->setup_owned = true;
    pthread_mutex_unlock(&runtime->mutex);
    hid_fd = open_hid(runtime);
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->shutdown_requested) {
        pthread_mutex_unlock(&runtime->mutex);
        if (hid_fd >= 0) {
            close(hid_fd);
        }
        return;
    }
    if (hid_fd < 0) {
        set_error_locked(runtime, RUNTIME_DISCONNECTED, "HID device is unavailable; reconnect USB-C and retry");
    } else {
        runtime->hid_fd = hid_fd;
        runtime->state = RUNTIME_READY;
        runtime->error[0] = '\0';
        pthread_cond_broadcast(&runtime->condition);
    }
    pthread_mutex_unlock(&runtime->mutex);
}

static void *runtime_worker(void *argument) {
    AppRuntime *runtime = argument;

    for (;;) {
        bool prepare;
        bool shutdown;
        AppRuntimeState state;
        JigglerSettings settings;
        uint64_t next_report;
        uint64_t now;

        pthread_mutex_lock(&runtime->mutex);
        prepare = runtime->prepare_requested;
        runtime->prepare_requested = false;
        shutdown = runtime->shutdown_requested;
        state = runtime->state;
        settings = runtime->settings;
        next_report = runtime->next_report_ms;
        pthread_mutex_unlock(&runtime->mutex);

        if (shutdown) {
            if (cleanup_owned(runtime) != 0) {
                pthread_mutex_lock(&runtime->mutex);
                runtime->shutdown_result = -1;
                snprintf(runtime->error, sizeof(runtime->error),
                         "HID cleanup failed; owned state may remain and requires cleanup retry");
                pthread_mutex_unlock(&runtime->mutex);
                fprintf(stderr, "HID cleanup failed during shutdown; owned state remains recorded\n");
            }
            pthread_mutex_lock(&runtime->mutex);
            runtime->worker_finished = true;
            pthread_cond_broadcast(&runtime->condition);
            pthread_mutex_unlock(&runtime->mutex);
            return NULL;
        }
        if (prepare) {
            prepare_runtime(runtime);
            continue;
        }
        if (state == RUNTIME_ACTIVE) {
            int emit_result;
            now = monotonic_ms();
            if (now < next_report) {
                struct timespec deadline;
                uint64_t delay = next_report - now;
                monotonic_after_ms(&deadline, delay > 100U ? 100U : delay);
                pthread_mutex_lock(&runtime->mutex);
                pthread_cond_timedwait(&runtime->condition, &runtime->mutex, &deadline);
                pthread_mutex_unlock(&runtime->mutex);
                continue;
            }
            pthread_mutex_lock(&runtime->mutex);
            if (runtime->state != RUNTIME_ACTIVE) {
                pthread_mutex_unlock(&runtime->mutex);
                continue;
            }
            runtime->cycle_in_progress = true;
            runtime->cycle_started_ms = monotonic_ms();
            settings = runtime->settings;
            pthread_mutex_unlock(&runtime->mutex);
            emit_result = emit_cycle(runtime, &settings);
            pthread_mutex_lock(&runtime->mutex);
            runtime->cycle_in_progress = false;
            runtime->cycle_started_ms = 0;
            runtime->cycle_target_x = 0;
            runtime->cycle_target_y = 0;
            if (emit_result < 0 && !runtime->shutdown_requested) {
                close_hid_locked(runtime);
                runtime->stop_after_cycle = false;
                set_error_locked(runtime, RUNTIME_DISCONNECTED, "HID report failed; reconnect USB-C and retry");
            } else if (runtime->stop_after_cycle && !runtime->shutdown_requested) {
                runtime->stop_after_cycle = false;
                runtime->state = RUNTIME_READY;
                runtime->next_report_ms = 0;
                pthread_cond_broadcast(&runtime->condition);
            } else if (runtime->state == RUNTIME_ACTIVE) {
                runtime->next_report_ms = monotonic_ms() + (uint64_t)runtime->settings.interval_sec * 1000U;
            }
            pthread_mutex_unlock(&runtime->mutex);
            continue;
        }

        pthread_mutex_lock(&runtime->mutex);
        if (!runtime->shutdown_requested && !runtime->prepare_requested && runtime->state != RUNTIME_ACTIVE) {
            pthread_cond_wait(&runtime->condition, &runtime->mutex);
        }
        pthread_mutex_unlock(&runtime->mutex);
    }
}

AppRuntime *app_runtime_create(const AppRuntimeConfig *config, const JigglerSettings *settings) {
    AppRuntime *runtime;
    JigglerSettings checked;
    pthread_condattr_t condition_attributes;

    if (config == NULL || config->setup_script == NULL || config->cleanup_script == NULL ||
        config->hid_device == NULL || settings == NULL) {
        return NULL;
    }
    checked = *settings;
    if (settings_validate(&checked) != 0) {
        errno = EINVAL;
        return NULL;
    }
    runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return NULL;
    }
    runtime->setup_script = copy_string(config->setup_script);
    runtime->cleanup_script = copy_string(config->cleanup_script);
    runtime->hid_device = copy_string(config->hid_device);
    if (runtime->setup_script == NULL || runtime->cleanup_script == NULL || runtime->hid_device == NULL) {
        app_runtime_destroy(runtime);
        return NULL;
    }
    runtime->config = *config;
    runtime->config.setup_script = runtime->setup_script;
    runtime->config.cleanup_script = runtime->cleanup_script;
    runtime->config.hid_device = runtime->hid_device;
    if (runtime->config.helper_timeout_ms == 0) {
        runtime->config.helper_timeout_ms = DEFAULT_HELPER_TIMEOUT_MS;
    }
    runtime->state = RUNTIME_IDLE;
    runtime->settings = checked;
    runtime->random_state = config->random_seed != 0 ? config->random_seed : (uint32_t)monotonic_ms();
    runtime->hid_fd = -1;
    if (pthread_mutex_init(&runtime->mutex, NULL) != 0) {
        free(runtime->setup_script);
        free(runtime->cleanup_script);
        free(runtime->hid_device);
        free(runtime);
        return NULL;
    }
    if (pthread_condattr_init(&condition_attributes) != 0) {
        pthread_mutex_destroy(&runtime->mutex);
        free(runtime->setup_script);
        free(runtime->cleanup_script);
        free(runtime->hid_device);
        free(runtime);
        return NULL;
    }
    if (pthread_condattr_setclock(&condition_attributes, CLOCK_MONOTONIC) != 0 ||
        pthread_cond_init(&runtime->condition, &condition_attributes) != 0) {
        pthread_condattr_destroy(&condition_attributes);
        pthread_mutex_destroy(&runtime->mutex);
        free(runtime->setup_script);
        free(runtime->cleanup_script);
        free(runtime->hid_device);
        free(runtime);
        return NULL;
    }
    pthread_condattr_destroy(&condition_attributes);
    if (pthread_create(&runtime->worker, NULL, runtime_worker, runtime) != 0) {
        pthread_cond_destroy(&runtime->condition);
        pthread_mutex_destroy(&runtime->mutex);
        free(runtime->setup_script);
        free(runtime->cleanup_script);
        free(runtime->hid_device);
        free(runtime);
        return NULL;
    }
    runtime->worker_started = true;
    return runtime;
}

int app_runtime_prepare(AppRuntime *runtime) {
    if (runtime == NULL) {
        return -1;
    }
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->shutdown_requested || runtime->state == RUNTIME_PREPARING ||
        runtime->state == RUNTIME_RETRYING || runtime->state == RUNTIME_ACTIVE) {
        pthread_mutex_unlock(&runtime->mutex);
        return -1;
    }
    runtime->state = (runtime->state == RUNTIME_ERROR || runtime->state == RUNTIME_DISCONNECTED)
                         ? RUNTIME_RETRYING
                         : RUNTIME_PREPARING;
    runtime->error[0] = '\0';
    runtime->prepare_requested = true;
    pthread_cond_broadcast(&runtime->condition);
    pthread_mutex_unlock(&runtime->mutex);
    return 0;
}

int app_runtime_start(AppRuntime *runtime) {
    if (runtime == NULL) {
        return -1;
    }
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->state != RUNTIME_READY || runtime->hid_fd < 0) {
        pthread_mutex_unlock(&runtime->mutex);
        return -1;
    }
    runtime->state = RUNTIME_ACTIVE;
    runtime->stop_after_cycle = false;
    runtime->next_report_ms = monotonic_ms() + (uint64_t)runtime->settings.interval_sec * 1000U;
    pthread_cond_broadcast(&runtime->condition);
    pthread_mutex_unlock(&runtime->mutex);
    return 0;
}

int app_runtime_stop(AppRuntime *runtime) {
    if (runtime == NULL) {
        return -1;
    }
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->state != RUNTIME_ACTIVE) {
        pthread_mutex_unlock(&runtime->mutex);
        return -1;
    }
    if (runtime->cycle_in_progress) {
        runtime->stop_after_cycle = true;
    } else {
        runtime->state = RUNTIME_READY;
        runtime->next_report_ms = 0;
        pthread_cond_broadcast(&runtime->condition);
    }
    pthread_mutex_unlock(&runtime->mutex);
    return 0;
}

int app_runtime_set_settings(AppRuntime *runtime, const JigglerSettings *settings) {
    JigglerSettings checked;

    if (runtime == NULL || settings == NULL) {
        return -1;
    }
    checked = *settings;
    if (settings_validate(&checked) != 0) {
        return -1;
    }
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->shutdown_requested) {
        pthread_mutex_unlock(&runtime->mutex);
        return -1;
    }
    runtime->settings = checked;
    if (runtime->state == RUNTIME_ACTIVE) {
        runtime->next_report_ms = monotonic_ms() + (uint64_t)checked.interval_sec * 1000U;
        pthread_cond_broadcast(&runtime->condition);
    }
    pthread_mutex_unlock(&runtime->mutex);
    return 0;
}

void app_runtime_snapshot(AppRuntime *runtime, AppRuntimeSnapshot *snapshot) {
    uint64_t now;

    if (runtime == NULL || snapshot == NULL) {
        return;
    }
    pthread_mutex_lock(&runtime->mutex);
    snapshot->state = runtime->state;
    snapshot->settings = runtime->settings;
    snapshot->report_count = runtime->report_count;
    snapshot->last_report_ms = runtime->last_report_ms;
    snapshot->next_report_ms = runtime->next_report_ms;
    now = monotonic_ms();
    snapshot->countdown_ms = runtime->state == RUNTIME_ACTIVE && runtime->next_report_ms > now
                                 ? runtime->next_report_ms - now
                                 : 0;
    snapshot->cleanup_count = runtime->cleanup_count;
    snapshot->setup_owned = runtime->setup_owned ? 1 : 0;
    snapshot->gesture_active = runtime->cycle_in_progress ? 1 : 0;
    snapshot->gesture_elapsed_ms = runtime->cycle_in_progress && now >= runtime->cycle_started_ms
                                       ? now - runtime->cycle_started_ms
                                       : 0;
    snapshot->gesture_target_x = runtime->cycle_target_x;
    snapshot->gesture_target_y = runtime->cycle_target_y;
    snapshot->shutdown_complete = runtime->worker_finished ? 1 : 0;
    snapshot->shutdown_result = runtime->shutdown_result;
    snprintf(snapshot->error, sizeof(snapshot->error), "%s", runtime->error);
    pthread_mutex_unlock(&runtime->mutex);
}

int app_runtime_wait_for_state(AppRuntime *runtime, AppRuntimeState state, unsigned int timeout_ms) {
    struct timespec deadline;
    int result = 0;

    if (runtime == NULL) {
        return -1;
    }
    monotonic_after_ms(&deadline, timeout_ms);
    pthread_mutex_lock(&runtime->mutex);
    while (runtime->state != state && result == 0) {
        result = pthread_cond_timedwait(&runtime->condition, &runtime->mutex, &deadline);
    }
    if (runtime->state == state) {
        result = 0;
    } else {
        result = -1;
    }
    pthread_mutex_unlock(&runtime->mutex);
    return result;
}

int app_runtime_request_shutdown(AppRuntime *runtime) {
    if (runtime == NULL) {
        return -1;
    }
    if (!runtime->worker_started) {
        return 0;
    }
    pthread_mutex_lock(&runtime->mutex);
    runtime->shutdown_requested = true;
    runtime->state = RUNTIME_EXITING;
    pthread_cond_broadcast(&runtime->condition);
    pthread_mutex_unlock(&runtime->mutex);
    return 0;
}

int app_runtime_shutdown(AppRuntime *runtime) {
    if (runtime == NULL) return -1;
    if (!runtime->worker_started) return 0;
    if (runtime->worker_joined) return runtime->shutdown_result;
    if (app_runtime_request_shutdown(runtime) != 0) return -1;
    pthread_join(runtime->worker, NULL);
    runtime->worker_joined = true;
    return runtime->shutdown_result;
}

void app_runtime_destroy(AppRuntime *runtime) {
    if (runtime == NULL) {
        return;
    }
    (void)app_runtime_shutdown(runtime);
    if (runtime->worker_started) {
        pthread_cond_destroy(&runtime->condition);
        pthread_mutex_destroy(&runtime->mutex);
    }
    free(runtime->setup_script);
    free(runtime->cleanup_script);
    free(runtime->hid_device);
    free(runtime);
}

const char *app_runtime_state_name(AppRuntimeState state) {
    static const char *const names[] = {
        "IDLE",
        "PREPARING",
        "READY",
        "ACTIVE",
        "DISCONNECTED",
        "ERROR",
        "RETRYING",
        "EXITING"
    };
    if (state < RUNTIME_IDLE || state > RUNTIME_EXITING) {
        return "UNKNOWN";
    }
    return names[state];
}
