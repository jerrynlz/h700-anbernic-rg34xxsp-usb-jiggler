#ifndef USBJIGGLER_SETTINGS_H
#define USBJIGGLER_SETTINGS_H

#include <stddef.h>

enum {
    JIGGLER_MIN_INTERVAL_SEC = 1,
    JIGGLER_MAX_INTERVAL_SEC = 300,
    JIGGLER_MIN_MOVE_PX = 1,
    JIGGLER_MAX_MOVE_PX = 2000
};

typedef enum {
    JIGGLE_HORIZONTAL = 0,
    JIGGLE_VERTICAL,
    JIGGLE_SQUARE,
    JIGGLE_RANDOM,
    JIGGLE_PATTERN_COUNT
} JigglerPattern;

typedef struct {
    int interval_sec;
    int move_px;
    JigglerPattern pattern;
} JigglerSettings;

void settings_defaults(JigglerSettings *settings);
int settings_validate(JigglerSettings *settings);
int settings_load(const char *path, JigglerSettings *settings, char *error, size_t error_size);
int settings_save(const char *path, const JigglerSettings *settings, char *error, size_t error_size);
const char *settings_pattern_name(JigglerPattern pattern);

#endif
