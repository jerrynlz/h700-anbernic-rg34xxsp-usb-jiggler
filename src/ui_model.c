#include "ui_model.h"

#include <string.h>

enum {
    UI_SETTING_INTERVAL = 0,
    UI_SETTING_MOVEMENT,
    UI_SETTING_PATTERN,
    UI_SETTING_COUNT,
    JOYSTICK_AXIS_THRESHOLD = 16000,
    JOYSTICK_AXIS_RELEASE = 8000,
    INPUT_REPEAT_DELAY_MS = 330,
    INPUT_REPEAT_RATE_MS = 90,
    INPUT_DEDUPE_MS = 80,
    INPUT_SOURCE_NONE = 0,
    INPUT_SOURCE_KEY,
    INPUT_SOURCE_HAT,
    INPUT_SOURCE_AXIS
};

void ui_model_init(UiModel *model, const JigglerSettings *settings) {
    if (model == NULL || settings == NULL) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->view = UI_VIEW_MAIN;
    model->settings = *settings;
    settings_validate(&model->settings);
}

int ui_adjustment_step(int selected_setting, unsigned int held_ms) {
    if (selected_setting == UI_SETTING_PATTERN) {
        return 1;
    }
    if (held_ms >= 1800U) {
        return selected_setting == UI_SETTING_INTERVAL ? 25 : 200;
    }
    if (held_ms >= 700U) {
        return selected_setting == UI_SETTING_INTERVAL ? 5 : 25;
    }
    return 1;
}

static void adjust_setting(UiModel *model, int direction, unsigned int held_ms) {
    int step = ui_adjustment_step(model->selected_setting, held_ms);

    if (model->selected_setting == UI_SETTING_INTERVAL) {
        model->settings.interval_sec += direction * step;
        if (model->settings.interval_sec < JIGGLER_MIN_INTERVAL_SEC) {
            model->settings.interval_sec = JIGGLER_MIN_INTERVAL_SEC;
        } else if (model->settings.interval_sec > JIGGLER_MAX_INTERVAL_SEC) {
            model->settings.interval_sec = JIGGLER_MAX_INTERVAL_SEC;
        }
    } else if (model->selected_setting == UI_SETTING_MOVEMENT) {
        model->settings.move_px += direction * step;
        if (model->settings.move_px < JIGGLER_MIN_MOVE_PX) {
            model->settings.move_px = JIGGLER_MIN_MOVE_PX;
        } else if (model->settings.move_px > JIGGLER_MAX_MOVE_PX) {
            model->settings.move_px = JIGGLER_MAX_MOVE_PX;
        }
    } else {
        int pattern = (int)model->settings.pattern + direction;
        if (pattern < 0) {
            pattern = JIGGLE_PATTERN_COUNT - 1;
        } else if (pattern >= JIGGLE_PATTERN_COUNT) {
            pattern = 0;
        }
        model->settings.pattern = (JigglerPattern)pattern;
    }
}

unsigned int ui_model_handle(UiModel *model, UiAction action, AppRuntimeState runtime_state,
                             unsigned int held_ms) {
    if (model == NULL || action == UI_ACTION_NONE) {
        return UI_EFFECT_NONE;
    }

    if (model->view == UI_VIEW_HELP) {
        if (action == UI_ACTION_BACK || action == UI_ACTION_HELP || action == UI_ACTION_ACCEPT) {
            model->view = UI_VIEW_MAIN;
        }
        return UI_EFFECT_NONE;
    }

    if (model->view == UI_VIEW_EXIT_CONFIRM) {
        if (action == UI_ACTION_ACCEPT) {
            return UI_EFFECT_EXIT;
        }
        if (action == UI_ACTION_BACK) {
            model->view = UI_VIEW_MAIN;
        } else if (action == UI_ACTION_HELP) {
            model->view = UI_VIEW_HELP;
        }
        return UI_EFFECT_NONE;
    }

    if (action == UI_ACTION_HELP) {
        model->view = UI_VIEW_HELP;
        return UI_EFFECT_NONE;
    }

    if (model->view == UI_VIEW_SETTINGS) {
        if (action == UI_ACTION_UP) {
            model->selected_setting = (model->selected_setting + UI_SETTING_COUNT - 1) % UI_SETTING_COUNT;
        } else if (action == UI_ACTION_DOWN) {
            model->selected_setting = (model->selected_setting + 1) % UI_SETTING_COUNT;
        } else if (action == UI_ACTION_LEFT || action == UI_ACTION_RIGHT) {
            adjust_setting(model, action == UI_ACTION_RIGHT ? 1 : -1, held_ms);
            return UI_EFFECT_SETTINGS_CHANGED;
        } else if (action == UI_ACTION_ACCEPT || action == UI_ACTION_BACK) {
            model->view = UI_VIEW_MAIN;
        }
        return UI_EFFECT_NONE;
    }

    if (action == UI_ACTION_BACK) {
        model->view = UI_VIEW_EXIT_CONFIRM;
    } else if (action == UI_ACTION_UP || action == UI_ACTION_DOWN) {
        model->view = UI_VIEW_SETTINGS;
    } else if (action == UI_ACTION_LEFT || action == UI_ACTION_RIGHT) {
        model->view = UI_VIEW_SETTINGS;
        adjust_setting(model, action == UI_ACTION_RIGHT ? 1 : -1, held_ms);
        return UI_EFFECT_SETTINGS_CHANGED;
    } else if (action == UI_ACTION_ACCEPT) {
        if (runtime_state == RUNTIME_READY) {
            return UI_EFFECT_START;
        }
        if (runtime_state == RUNTIME_ACTIVE) {
            return UI_EFFECT_STOP;
        }
        if (runtime_state == RUNTIME_ERROR || runtime_state == RUNTIME_DISCONNECTED) {
            return UI_EFFECT_RETRY;
        }
    }
    return UI_EFFECT_NONE;
}

UiAction ui_action_from_sdl_event(const SDL_Event *event) {
    if (event == NULL) {
        return UI_ACTION_NONE;
    }
    if (event->type == SDL_KEYDOWN) {
        switch (event->key.keysym.sym) {
            case SDLK_UP: return UI_ACTION_UP;
            case SDLK_DOWN: return UI_ACTION_DOWN;
            case SDLK_LEFT: return UI_ACTION_LEFT;
            case SDLK_RIGHT: return UI_ACTION_RIGHT;
            case SDLK_RETURN: return UI_ACTION_ACCEPT;
            case SDLK_ESCAPE: return UI_ACTION_BACK;
            case SDLK_SPACE: return UI_ACTION_HELP;
            default: return UI_ACTION_NONE;
        }
    }
    if (event->type == SDL_JOYBUTTONDOWN) {
        switch (event->jbutton.button) {
            case 0: return UI_ACTION_ACCEPT;
            case 1:
            case 7: return UI_ACTION_BACK;
            case 6: return UI_ACTION_HELP;
            default: return UI_ACTION_NONE;
        }
    }
    if (event->type == SDL_JOYHATMOTION) {
        if (event->jhat.value & SDL_HAT_UP) return UI_ACTION_UP;
        if (event->jhat.value & SDL_HAT_DOWN) return UI_ACTION_DOWN;
        if (event->jhat.value & SDL_HAT_LEFT) return UI_ACTION_LEFT;
        if (event->jhat.value & SDL_HAT_RIGHT) return UI_ACTION_RIGHT;
    }
    if (event->type == SDL_JOYAXISMOTION) {
        if (event->jaxis.axis == 0 && event->jaxis.value <= -JOYSTICK_AXIS_THRESHOLD) return UI_ACTION_LEFT;
        if (event->jaxis.axis == 0 && event->jaxis.value >= JOYSTICK_AXIS_THRESHOLD) return UI_ACTION_RIGHT;
        if (event->jaxis.axis == 1 && event->jaxis.value <= -JOYSTICK_AXIS_THRESHOLD) return UI_ACTION_UP;
        if (event->jaxis.axis == 1 && event->jaxis.value >= JOYSTICK_AXIS_THRESHOLD) return UI_ACTION_DOWN;
    }
    return UI_ACTION_NONE;
}

static int action_is_directional(UiAction action) {
    return action == UI_ACTION_UP || action == UI_ACTION_DOWN ||
           action == UI_ACTION_LEFT || action == UI_ACTION_RIGHT;
}

void ui_input_tracker_init(UiInputTracker *tracker) {
    if (tracker == NULL) return;
    memset(tracker, 0, sizeof(*tracker));
}

static void clear_held(UiInputTracker *tracker) {
    tracker->held_action = UI_ACTION_NONE;
    tracker->held_source = INPUT_SOURCE_NONE;
    tracker->held_device = 0;
    tracker->held_control = 0;
    tracker->secondary_source = INPUT_SOURCE_NONE;
    tracker->secondary_device = 0;
    tracker->secondary_control = 0;
}

static int source_matches_event(int source, int32_t device, int32_t control, const SDL_Event *event) {
    if (event->type == SDL_KEYUP) {
        return source == INPUT_SOURCE_KEY && control == (int32_t)event->key.keysym.sym;
    }
    if (event->type == SDL_JOYHATMOTION && event->jhat.value == SDL_HAT_CENTERED) {
        return source == INPUT_SOURCE_HAT && device == event->jhat.which && control == event->jhat.hat;
    }
    if (event->type == SDL_JOYAXISMOTION && event->jaxis.value > -JOYSTICK_AXIS_RELEASE &&
        event->jaxis.value < JOYSTICK_AXIS_RELEASE) {
        return source == INPUT_SOURCE_AXIS && device == event->jaxis.which && control == event->jaxis.axis;
    }
    return 0;
}

static void event_source(const SDL_Event *event, int *source, int32_t *device, int32_t *control) {
    *device = 0;
    if (event->type == SDL_KEYDOWN) {
        *source = INPUT_SOURCE_KEY;
        *control = (int32_t)event->key.keysym.sym;
    } else if (event->type == SDL_JOYHATMOTION) {
        *source = INPUT_SOURCE_HAT;
        *device = event->jhat.which;
        *control = event->jhat.hat;
    } else {
        *source = INPUT_SOURCE_AXIS;
        *device = event->jaxis.which;
        *control = event->jaxis.axis;
    }
}

UiAction ui_input_tracker_event(UiInputTracker *tracker, const SDL_Event *event, uint32_t ticks) {
    UiAction action;

    if (tracker == NULL || event == NULL) return UI_ACTION_NONE;
    if (source_matches_event(tracker->held_source, tracker->held_device, tracker->held_control, event)) {
        if (tracker->secondary_source != INPUT_SOURCE_NONE) {
            tracker->held_source = tracker->secondary_source;
            tracker->held_device = tracker->secondary_device;
            tracker->held_control = tracker->secondary_control;
            tracker->secondary_source = INPUT_SOURCE_NONE;
        } else {
            clear_held(tracker);
        }
        return UI_ACTION_NONE;
    }
    if (source_matches_event(tracker->secondary_source, tracker->secondary_device,
                             tracker->secondary_control, event)) {
        tracker->secondary_source = INPUT_SOURCE_NONE;
        tracker->secondary_device = 0;
        tracker->secondary_control = 0;
        return UI_ACTION_NONE;
    }
    if (event->type == SDL_KEYUP ||
        (event->type == SDL_JOYHATMOTION && event->jhat.value == SDL_HAT_CENTERED) ||
        (event->type == SDL_JOYAXISMOTION && event->jaxis.value > -JOYSTICK_AXIS_RELEASE &&
         event->jaxis.value < JOYSTICK_AXIS_RELEASE)) {
        return UI_ACTION_NONE;
    }
    if (event->type == SDL_KEYDOWN && event->key.repeat) return UI_ACTION_NONE;

    action = ui_action_from_sdl_event(event);
    if (action == UI_ACTION_NONE) return UI_ACTION_NONE;
    if (action_is_directional(action) && tracker->held_action == action) {
        int source;
        int32_t device;
        int32_t control;
        event_source(event, &source, &device, &control);
        if (source != tracker->held_source || device != tracker->held_device || control != tracker->held_control) {
            tracker->secondary_source = source;
            tracker->secondary_device = device;
            tracker->secondary_control = control;
        }
        return UI_ACTION_NONE;
    }
    if (action == tracker->last_action && ticks - tracker->last_action_at < INPUT_DEDUPE_MS) {
        return UI_ACTION_NONE;
    }
    tracker->last_action = action;
    tracker->last_action_at = ticks;

    if (action_is_directional(action)) {
        tracker->secondary_source = INPUT_SOURCE_NONE;
        tracker->secondary_device = 0;
        tracker->secondary_control = 0;
        tracker->held_action = action;
        tracker->held_since = ticks;
        tracker->next_repeat = ticks + INPUT_REPEAT_DELAY_MS;
        event_source(event, &tracker->held_source, &tracker->held_device, &tracker->held_control);
    }
    return action;
}

UiAction ui_input_tracker_repeat(UiInputTracker *tracker, uint32_t ticks, unsigned int *held_ms) {
    if (tracker == NULL || tracker->held_action == UI_ACTION_NONE ||
        (int32_t)(ticks - tracker->next_repeat) < 0) {
        return UI_ACTION_NONE;
    }
    if (held_ms != NULL) *held_ms = ticks - tracker->held_since;
    tracker->next_repeat = ticks + INPUT_REPEAT_RATE_MS;
    return tracker->held_action;
}

const char *ui_view_name(UiView view) {
    static const char *const names[] = {"main", "settings", "help", "exit"};
    if (view < UI_VIEW_MAIN || view > UI_VIEW_EXIT_CONFIRM) {
        return "unknown";
    }
    return names[view];
}
