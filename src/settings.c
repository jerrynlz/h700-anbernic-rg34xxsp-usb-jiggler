#define _POSIX_C_SOURCE 200809L

#include "settings.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static int parse_int(const char *text, int *value) {
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < INT_MIN || parsed > INT_MAX) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

void settings_defaults(JigglerSettings *settings) {
    if (settings == NULL) {
        return;
    }
    settings->interval_sec = 30;
    settings->move_px = 2;
    settings->pattern = JIGGLE_HORIZONTAL;
}

int settings_validate(JigglerSettings *settings) {
    if (settings == NULL) {
        return -1;
    }
    if (settings->interval_sec < JIGGLER_MIN_INTERVAL_SEC || settings->interval_sec > JIGGLER_MAX_INTERVAL_SEC ||
        settings->move_px < JIGGLER_MIN_MOVE_PX || settings->move_px > JIGGLER_MAX_MOVE_PX ||
        settings->pattern < JIGGLE_HORIZONTAL || settings->pattern >= JIGGLE_PATTERN_COUNT) {
        settings_defaults(settings);
        return -1;
    }
    return 0;
}

const char *settings_pattern_name(JigglerPattern pattern) {
    static const char *const names[JIGGLE_PATTERN_COUNT] = {
        "Horizontal",
        "Vertical",
        "Square",
        "Random"
    };
    if (pattern < JIGGLE_HORIZONTAL || pattern >= JIGGLE_PATTERN_COUNT) {
        return "Unknown";
    }
    return names[pattern];
}

int settings_load(const char *path, JigglerSettings *settings, char *error, size_t error_size) {
    FILE *file;
    char line[128];
    int seen_interval = 0;
    int seen_move = 0;
    int seen_pattern = 0;

    if (path == NULL || settings == NULL) {
        set_error(error, error_size, "invalid settings arguments");
        return -1;
    }

    settings_defaults(settings);
    file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        set_error(error, error_size, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *newline = strchr(line, '\n');
        char *equals;
        int parsed;

        if (newline == NULL && !feof(file)) {
            fclose(file);
            settings_defaults(settings);
            set_error(error, error_size, "settings line is too long");
            return -1;
        }
        if (newline != NULL) {
            *newline = '\0';
        }
        if (line[0] == '\0') {
            continue;
        }
        equals = strchr(line, '=');
        if (equals == NULL || strchr(equals + 1, '=') != NULL) {
            fclose(file);
            settings_defaults(settings);
            set_error(error, error_size, "malformed settings line");
            return -1;
        }
        *equals = '\0';
        if (parse_int(equals + 1, &parsed) != 0) {
            fclose(file);
            settings_defaults(settings);
            set_error(error, error_size, "settings value is not an integer");
            return -1;
        }
        if (strcmp(line, "interval_sec") == 0 && !seen_interval) {
            settings->interval_sec = parsed;
            seen_interval = 1;
        } else if (strcmp(line, "move_px") == 0 && !seen_move) {
            settings->move_px = parsed;
            seen_move = 1;
        } else if (strcmp(line, "pattern") == 0 && !seen_pattern) {
            settings->pattern = (JigglerPattern)parsed;
            seen_pattern = 1;
        } else {
            fclose(file);
            settings_defaults(settings);
            set_error(error, error_size, "unknown or duplicate settings key");
            return -1;
        }
    }

    if (ferror(file)) {
        set_error(error, error_size, strerror(errno));
        fclose(file);
        settings_defaults(settings);
        return -1;
    }
    if (fclose(file) != 0) {
        set_error(error, error_size, strerror(errno));
        settings_defaults(settings);
        return -1;
    }
    if (!seen_interval || !seen_move || !seen_pattern || settings_validate(settings) != 0) {
        settings_defaults(settings);
        set_error(error, error_size, "settings are incomplete or out of range");
        return -1;
    }
    return 0;
}

int settings_save(const char *path, const JigglerSettings *settings, char *error, size_t error_size) {
    JigglerSettings checked;
    char temporary[PATH_MAX];
    FILE *file;
    int descriptor;
    int failed = 0;
    int saved_errno = 0;

    if (path == NULL || settings == NULL) {
        set_error(error, error_size, "invalid settings arguments");
        return -1;
    }
    checked = *settings;
    if (settings_validate(&checked) != 0) {
        set_error(error, error_size, "settings are out of range");
        return -1;
    }
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(temporary)) {
        set_error(error, error_size, "settings path is too long");
        return -1;
    }

    descriptor = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        set_error(error, error_size, strerror(errno));
        return -1;
    }
    file = fdopen(descriptor, "w");
    if (file == NULL) {
        set_error(error, error_size, strerror(errno));
        close(descriptor);
        unlink(temporary);
        return -1;
    }
    if (fprintf(file, "interval_sec=%d\nmove_px=%d\npattern=%d\n",
                checked.interval_sec, checked.move_px, checked.pattern) < 0) {
        failed = 1;
        saved_errno = errno;
    }
    if (!failed && fflush(file) != 0) {
        failed = 1;
        saved_errno = errno;
    }
    if (!failed && fsync(descriptor) != 0) {
        failed = 1;
        saved_errno = errno;
    }
    if (fclose(file) != 0 && !failed) {
        failed = 1;
        saved_errno = errno;
    }
    if (failed) {
        set_error(error, error_size, strerror(saved_errno));
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, path) != 0) {
        set_error(error, error_size, strerror(errno));
        unlink(temporary);
        return -1;
    }
    return 0;
}
