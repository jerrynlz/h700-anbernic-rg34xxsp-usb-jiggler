#define _POSIX_C_SOURCE 200809L

#include "../src/app_runtime.h"
#include "../src/settings.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
        return; \
    } \
} while (0)

typedef struct {
    char directory[PATH_MAX];
    char setup[PATH_MAX];
    char cleanup[PATH_MAX];
    char hid[PATH_MAX];
    char events[PATH_MAX];
    char settings[PATH_MAX];
} Fixture;

static uint64_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void pause_ms(unsigned int milliseconds) {
    struct timespec duration = {
        .tv_sec = (time_t)(milliseconds / 1000U),
        .tv_nsec = (long)(milliseconds % 1000U) * 1000000L
    };
    nanosleep(&duration, NULL);
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }
    if (fputs(text, file) == EOF || fclose(file) != 0) {
        return -1;
    }
    return 0;
}

static int join_path(char *destination, size_t destination_size, const char *directory, const char *name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);

    if (directory_length + 1U + name_length + 1U > destination_size) {
        return -1;
    }
    memcpy(destination, directory, directory_length);
    destination[directory_length] = '/';
    memcpy(destination + directory_length + 1U, name, name_length + 1U);
    return 0;
}

static int append_suffix(char *destination, size_t destination_size, const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);

    if (path_length + suffix_length + 1U > destination_size) {
        return -1;
    }
    memcpy(destination, path, path_length);
    memcpy(destination + path_length, suffix, suffix_length + 1U);
    return 0;
}

static int make_fixture(Fixture *fixture, const char *setup_body) {
    char template_path[] = "/tmp/usbjiggler-runtime-XXXXXX";
    char cleanup_body[PATH_MAX + 64];

    memset(fixture, 0, sizeof(*fixture));
    if (mkdtemp(template_path) == NULL) {
        return -1;
    }
    snprintf(fixture->directory, sizeof(fixture->directory), "%s", template_path);
    if (join_path(fixture->setup, sizeof(fixture->setup), fixture->directory, "setup.sh") != 0 ||
        join_path(fixture->cleanup, sizeof(fixture->cleanup), fixture->directory, "cleanup.sh") != 0 ||
        join_path(fixture->hid, sizeof(fixture->hid), fixture->directory, "hidg0") != 0 ||
        join_path(fixture->events, sizeof(fixture->events), fixture->directory, "events") != 0 ||
        join_path(fixture->settings, sizeof(fixture->settings), fixture->directory, "settings.ini") != 0) {
        return -1;
    }
    if (write_text(fixture->setup, setup_body) != 0) {
        return -1;
    }
    snprintf(cleanup_body, sizeof(cleanup_body), "#!/bin/sh\nprintf 'cleanup\\n' >>'%s'\n", fixture->events);
    if (write_text(fixture->cleanup, cleanup_body) != 0 || write_text(fixture->hid, "") != 0) {
        return -1;
    }
    return 0;
}

static void remove_fixture(Fixture *fixture) {
    char temporary[PATH_MAX + 32];
    char suffix[32];

    unlink(fixture->setup);
    unlink(fixture->cleanup);
    unlink(fixture->hid);
    unlink(fixture->events);
    unlink(fixture->settings);
    snprintf(suffix, sizeof(suffix), ".tmp.%ld", (long)getpid());
    if (append_suffix(temporary, sizeof(temporary), fixture->settings, suffix) == 0) {
        unlink(temporary);
    }
    rmdir(fixture->directory);
}

static AppRuntimeConfig fixture_config(const Fixture *fixture) {
    AppRuntimeConfig config = {
        .setup_script = fixture->setup,
        .cleanup_script = fixture->cleanup,
        .hid_device = fixture->hid,
        .helper_timeout_ms = 1000,
        .allow_regular_hid = 1,
        .random_seed = 0x12345678U
    };
    return config;
}

static unsigned int count_event(const char *path, const char *wanted) {
    FILE *file = fopen(path, "r");
    char line[64];
    unsigned int count = 0;

    if (file == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, wanted) == 0) {
            count++;
        }
    }
    fclose(file);
    return count;
}

static int wait_for_reports(AppRuntime *runtime, uint64_t minimum, unsigned int timeout_ms) {
    uint64_t deadline = now_ms() + timeout_ms;
    AppRuntimeSnapshot snapshot;

    do {
        app_runtime_snapshot(runtime, &snapshot);
        if (snapshot.report_count >= minimum) {
            return 0;
        }
        pause_ms(10);
    } while (now_ms() < deadline);
    return -1;
}

static int wait_for_shutdown(AppRuntime *runtime, unsigned int timeout_ms, int *shutdown_result) {
    uint64_t deadline = now_ms() + timeout_ms;
    AppRuntimeSnapshot snapshot;

    do {
        app_runtime_snapshot(runtime, &snapshot);
        if (snapshot.shutdown_complete) {
            if (shutdown_result != NULL) *shutdown_result = snapshot.shutdown_result;
            return 0;
        }
        pause_ms(10);
    } while (now_ms() < deadline);
    return -1;
}

static int verify_smooth_reports(const char *path, size_t minimum_reports, int maximum_step,
                                 size_t minimum_nonzero, size_t minimum_zero) {
    FILE *file = fopen(path, "rb");
    uint8_t report[4];
    size_t count = 0;
    int x_sum = 0;
    int y_sum = 0;
    size_t nonzero = 0;
    size_t zero = 0;

    if (file == NULL) return -1;
    while (fread(report, 1, sizeof(report), file) == sizeof(report)) {
        int x = (int8_t)report[1];
        int y = (int8_t)report[2];
        int absolute_x = x < 0 ? -x : x;
        int absolute_y = y < 0 ? -y : y;
        if (report[0] != 0 || report[3] != 0 || absolute_x > maximum_step || absolute_y > maximum_step) {
            fclose(file);
            return -1;
        }
        x_sum += x;
        y_sum += y;
        if (x == 0 && y == 0) zero++; else nonzero++;
        count++;
    }
    if (ferror(file) || fclose(file) != 0) return -1;
    return count >= minimum_reports && nonzero >= minimum_nonzero && zero >= minimum_zero &&
           x_sum == 0 && y_sum == 0 ? 0 : -1;
}

static int verify_interpolation(int total_x, int total_y, int maximum_step,
                                size_t expected_nonzero, size_t expected_zero) {
    int x_sum = 0;
    int y_sum = 0;
    size_t nonzero = 0;
    size_t zero = 0;

    for (unsigned int frame = 1; frame <= APP_RUNTIME_SEGMENT_FRAMES; frame++) {
        uint8_t report[4];
        int x;
        int y;
        int absolute_x;
        int absolute_y;
        if (app_runtime_interpolate_step(total_x, total_y, frame, report) != 0) return -1;
        x = (int8_t)report[1];
        y = (int8_t)report[2];
        absolute_x = x < 0 ? -x : x;
        absolute_y = y < 0 ? -y : y;
        if (report[0] != 0 || report[3] != 0 || absolute_x > maximum_step || absolute_y > maximum_step) return -1;
        x_sum += x;
        y_sum += y;
        if (x == 0 && y == 0) zero++; else nonzero++;
    }
    return x_sum == total_x && y_sum == total_y &&
           nonzero == expected_nonzero && zero == expected_zero ? 0 : -1;
}

static void test_settings_defaults_and_malformed(void) {
    Fixture fixture;
    JigglerSettings settings;
    char error[128];

    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) == 0);
    CHECK(settings.interval_sec == 30);
    CHECK(settings.move_px == 2);
    CHECK(settings.pattern == JIGGLE_HORIZONTAL);

    CHECK(write_text(fixture.settings, "interval_sec=10\nmove_px=4\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) != 0);
    CHECK(settings.interval_sec == 30 && settings.move_px == 2 && settings.pattern == JIGGLE_HORIZONTAL);

    CHECK(write_text(fixture.settings, "interval_sec=10x\nmove_px=4\npattern=0\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) != 0);
    CHECK(settings.interval_sec == 30 && settings.move_px == 2);

    CHECK(write_text(fixture.settings, "interval_sec=10\ninterval_sec=11\nmove_px=4\npattern=0\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) != 0);

    CHECK(write_text(fixture.settings, "interval_sec=999999999999999999999\nmove_px=4\npattern=0\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) != 0);

    CHECK(write_text(fixture.settings, "interval_sec=-1\nmove_px=4\npattern=0\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) != 0);

    CHECK(write_text(fixture.settings, "interval_sec=10\nmove_px=2001\npattern=0\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) != 0);
    CHECK(settings.interval_sec == 30 && settings.move_px == 2);

    CHECK(write_text(fixture.settings, "interval_sec=10\nmove_px=4\npattern=0\nfuture=1\n") == 0);
    CHECK(settings_load(fixture.settings, &settings, error, sizeof(error)) != 0);
    remove_fixture(&fixture);
}

static void test_settings_boundaries_round_trip(void) {
    Fixture fixture;
    JigglerSettings written;
    JigglerSettings loaded;
    char error[128];

    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    for (int pattern = JIGGLE_HORIZONTAL; pattern < JIGGLE_PATTERN_COUNT; pattern++) {
        written.interval_sec = pattern % 2 == 0 ? 1 : 300;
        written.move_px = pattern % 2 == 0 ? JIGGLER_MIN_MOVE_PX : JIGGLER_MAX_MOVE_PX;
        written.pattern = (JigglerPattern)pattern;
        CHECK(settings_save(fixture.settings, &written, error, sizeof(error)) == 0);
        CHECK(settings_load(fixture.settings, &loaded, error, sizeof(error)) == 0);
        CHECK(memcmp(&written, &loaded, sizeof(written)) == 0);
    }
    CHECK(access(fixture.settings, F_OK) == 0);
    remove_fixture(&fixture);
}

static void test_report_sequences(void) {
    JigglerSettings settings = { .interval_sec = 30, .move_px = 127, .pattern = JIGGLE_HORIZONTAL };
    uint8_t reports[4][4];
    uint32_t random_state = 7;

    for (int pattern = JIGGLE_HORIZONTAL; pattern < JIGGLE_PATTERN_COUNT; pattern++) {
        int x_sum = 0;
        int y_sum = 0;
        settings.pattern = (JigglerPattern)pattern;
        size_t count = app_runtime_build_reports(&settings, &random_state, reports);
        CHECK(count == (pattern == JIGGLE_SQUARE ? 4U : 2U));
        for (size_t index = 0; index < count; index++) {
            CHECK(reports[index][0] == 0 && reports[index][3] == 0);
            x_sum += (int8_t)reports[index][1];
            y_sum += (int8_t)reports[index][2];
        }
        CHECK(x_sum == 0 && y_sum == 0);
        if (pattern == JIGGLE_HORIZONTAL) {
            CHECK(reports[0][1] == 0x7f && reports[1][1] == 0x81);
        }
    }
}

static void test_interpolated_report_boundaries(void) {
    uint8_t report[4];

    CHECK(verify_interpolation(1, 0, 1, 1, 29) == 0);
    CHECK(verify_interpolation(2, 0, 1, 2, 28) == 0);
    CHECK(verify_interpolation(0, -2, 1, 2, 28) == 0);
    CHECK(verify_interpolation(1, -2, 1, 2, 28) == 0);
    CHECK(verify_interpolation(2000, -1200, 67, 30, 0) == 0);
    CHECK(app_runtime_interpolate_step(1, 0, 0, report) != 0);
    CHECK(app_runtime_interpolate_step(1, 0, APP_RUNTIME_SEGMENT_FRAMES + 1U, report) != 0);
    CHECK(app_runtime_interpolate_step(2001, 0, 1, report) != 0);
}

static void test_prepare_active_stop_and_cleanup_once(void) {
    Fixture fixture;
    JigglerSettings settings = { .interval_sec = 1, .move_px = JIGGLER_MAX_MOVE_PX, .pattern = JIGGLE_HORIZONTAL };
    AppRuntimeConfig config;
    AppRuntime *runtime;
    AppRuntimeSnapshot snapshot;
    uint64_t request_started;
    int shutdown_result;
    char cleanup_body[PATH_MAX + 96];

    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    snprintf(cleanup_body, sizeof(cleanup_body), "#!/bin/sh\nsleep 0.3\nprintf 'cleanup\\n' >>'%s'\n", fixture.events);
    CHECK(write_text(fixture.cleanup, cleanup_body) == 0);
    config = fixture_config(&fixture);
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 1500) == 0);
    CHECK(app_runtime_start(runtime) == 0);
    CHECK(wait_for_reports(runtime, 1, 1800) == 0);
    CHECK(app_runtime_stop(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 2000) == 0);
    CHECK(wait_for_reports(runtime, 60, 100) == 0);

    settings.interval_sec = 300;
    CHECK(app_runtime_set_settings(runtime, &settings) == 0);
    CHECK(app_runtime_start(runtime) == 0);
    CHECK(app_runtime_stop(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 200) == 0);
    request_started = now_ms();
    CHECK(app_runtime_request_shutdown(runtime) == 0);
    CHECK(now_ms() - request_started < 100U);
    app_runtime_snapshot(runtime, &snapshot);
    CHECK(snapshot.state == RUNTIME_EXITING);
    CHECK(!snapshot.shutdown_complete);
    CHECK(wait_for_shutdown(runtime, 1500, &shutdown_result) == 0);
    CHECK(shutdown_result == 0);
    CHECK(app_runtime_shutdown(runtime) == 0);
    app_runtime_snapshot(runtime, &snapshot);
    CHECK(snapshot.cleanup_count == 1);
    CHECK(count_event(fixture.events, "cleanup") == 1);
    CHECK(verify_smooth_reports(fixture.hid, 60, 67, 60, 0) == 0);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

static void test_small_movement_keeps_human_gesture_duration(void) {
    Fixture fixture;
    JigglerSettings settings = { .interval_sec = 1, .move_px = 1, .pattern = JIGGLE_HORIZONTAL };
    AppRuntimeConfig config;
    AppRuntime *runtime;
    uint64_t first_report_at;
    uint64_t completed_at;

    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    config = fixture_config(&fixture);
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 1500) == 0);
    CHECK(app_runtime_start(runtime) == 0);
    CHECK(wait_for_reports(runtime, 1, 1800) == 0);
    first_report_at = now_ms();
    CHECK(wait_for_reports(runtime, 60, 1400) == 0);
    completed_at = now_ms();
    CHECK(completed_at - first_report_at >= 850U);
    CHECK(completed_at - first_report_at <= 1300U);
    CHECK(app_runtime_stop(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 200) == 0);
    CHECK(app_runtime_shutdown(runtime) == 0);
    CHECK(verify_smooth_reports(fixture.hid, 60, 1, 2, 58) == 0);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

static void test_square_gesture_duration_and_frames(void) {
    Fixture fixture;
    JigglerSettings settings = { .interval_sec = 1, .move_px = 2, .pattern = JIGGLE_SQUARE };
    AppRuntimeConfig config;
    AppRuntime *runtime;
    uint64_t first_report_at;
    uint64_t completed_at;

    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    config = fixture_config(&fixture);
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 1500) == 0);
    CHECK(app_runtime_start(runtime) == 0);
    CHECK(wait_for_reports(runtime, 1, 1800) == 0);
    first_report_at = now_ms();
    CHECK(wait_for_reports(runtime, 120, 2500) == 0);
    completed_at = now_ms();
    CHECK(completed_at - first_report_at >= 1750U);
    CHECK(completed_at - first_report_at <= 2300U);
    CHECK(app_runtime_stop(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 200) == 0);
    CHECK(app_runtime_shutdown(runtime) == 0);
    CHECK(verify_smooth_reports(fixture.hid, 120, 1, 8, 112) == 0);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

static void test_setup_failure_has_no_cleanup(void) {
    Fixture fixture;
    JigglerSettings settings;
    AppRuntimeConfig config;
    AppRuntime *runtime;
    AppRuntimeSnapshot snapshot;

    settings_defaults(&settings);
    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 9\n") == 0);
    config = fixture_config(&fixture);
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_ERROR, 1500) == 0);
    CHECK(app_runtime_shutdown(runtime) == 0);
    app_runtime_snapshot(runtime, &snapshot);
    CHECK(snapshot.cleanup_count == 0);
    CHECK(count_event(fixture.events, "cleanup") == 0);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

static void test_open_failure_retry_and_cleanup(void) {
    Fixture fixture;
    JigglerSettings settings;
    AppRuntimeConfig config;
    AppRuntime *runtime;
    AppRuntimeSnapshot snapshot;

    settings_defaults(&settings);
    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    unlink(fixture.hid);
    config = fixture_config(&fixture);
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_DISCONNECTED, 1500) == 0);
    CHECK(write_text(fixture.hid, "") == 0);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 2000) == 0);
    CHECK(app_runtime_shutdown(runtime) == 0);
    app_runtime_snapshot(runtime, &snapshot);
    CHECK(snapshot.cleanup_count == 2);
    CHECK(count_event(fixture.events, "cleanup") == 2);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

static void test_write_failure_disconnects(void) {
    Fixture fixture;
    JigglerSettings settings = { .interval_sec = 1, .move_px = 2, .pattern = JIGGLE_HORIZONTAL };
    AppRuntimeConfig config;
    AppRuntime *runtime;

    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    config = fixture_config(&fixture);
    config.hid_device = "/dev/full";
    config.allow_regular_hid = 0;
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 1500) == 0);
    CHECK(app_runtime_start(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_DISCONNECTED, 1800) == 0);
    CHECK(app_runtime_shutdown(runtime) == 0);
    CHECK(count_event(fixture.events, "cleanup") == 1);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

static void test_helper_timeout_and_shutdown_cancellation(void) {
    Fixture fixture;
    JigglerSettings settings;
    AppRuntimeConfig config;
    AppRuntime *runtime;
    uint64_t started;
    char trapped_setup[PATH_MAX * 2 + 256];

    settings_defaults(&settings);
    CHECK(make_fixture(&fixture, "#!/bin/sh\nsleep 10\n") == 0);
    config = fixture_config(&fixture);
    config.helper_timeout_ms = 120;
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    started = now_ms();
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_ERROR, 1000) == 0);
    CHECK(now_ms() - started < 900);
    app_runtime_destroy(runtime);

    snprintf(trapped_setup, sizeof(trapped_setup),
             "#!/bin/sh\ntrap 'printf \"term\\n\" >>\"%s\"; sleep 2; printf \"rollback_complete\\n\" >>\"%s\"; exit 143' TERM\nwhile :; do sleep 1; done\n",
             fixture.events, fixture.events);
    CHECK(write_text(fixture.setup, trapped_setup) == 0);
    config.helper_timeout_ms = 10000;
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    pause_ms(50);
    started = now_ms();
    CHECK(app_runtime_shutdown(runtime) == 0);
    CHECK(now_ms() - started >= 1800);
    CHECK(now_ms() - started < 5500);
    CHECK(count_event(fixture.events, "rollback_complete") == 1);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

static void test_cleanup_failure_retains_ownership(void) {
    Fixture fixture;
    JigglerSettings settings;
    AppRuntimeConfig config;
    AppRuntime *runtime;
    AppRuntimeSnapshot snapshot;
    char setup_body[PATH_MAX + 64];
    char cleanup_body[PATH_MAX + 64];

    settings_defaults(&settings);
    CHECK(make_fixture(&fixture, "#!/bin/sh\nexit 0\n") == 0);
    snprintf(setup_body, sizeof(setup_body), "#!/bin/sh\nprintf 'setup\\n' >>'%s'\n", fixture.events);
    snprintf(cleanup_body, sizeof(cleanup_body), "#!/bin/sh\nprintf 'cleanup\\n' >>'%s'\nexit 9\n", fixture.events);
    CHECK(write_text(fixture.setup, setup_body) == 0);
    CHECK(write_text(fixture.cleanup, cleanup_body) == 0);
    config = fixture_config(&fixture);
    runtime = app_runtime_create(&config, &settings);
    CHECK(runtime != NULL);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_READY, 1500) == 0);
    CHECK(app_runtime_prepare(runtime) == 0);
    CHECK(app_runtime_wait_for_state(runtime, RUNTIME_ERROR, 1500) == 0);
    app_runtime_snapshot(runtime, &snapshot);
    CHECK(snapshot.setup_owned == 1);
    CHECK(snapshot.cleanup_count == 1);
    CHECK(count_event(fixture.events, "setup") == 1);
    CHECK(count_event(fixture.events, "cleanup") == 1);
    CHECK(app_runtime_shutdown(runtime) != 0);
    app_runtime_snapshot(runtime, &snapshot);
    CHECK(snapshot.setup_owned == 1);
    CHECK(snapshot.cleanup_count == 2);
    CHECK(strstr(snapshot.error, "cleanup failed") != NULL);
    app_runtime_destroy(runtime);
    remove_fixture(&fixture);
}

int main(void) {
    test_settings_defaults_and_malformed();
    test_settings_boundaries_round_trip();
    test_report_sequences();
    test_interpolated_report_boundaries();
    test_prepare_active_stop_and_cleanup_once();
    test_small_movement_keeps_human_gesture_duration();
    test_square_gesture_duration_and_frames();
    test_setup_failure_has_no_cleanup();
    test_open_failure_retry_and_cleanup();
    test_write_failure_disconnects();
    test_helper_timeout_and_shutdown_cancellation();
    test_cleanup_failure_retains_ownership();

    if (failures != 0) {
        fprintf(stderr, "%d runtime test(s) failed\n", failures);
        return 1;
    }
    puts("RUNTIME_TESTS_PASS=YES");
    return 0;
}
