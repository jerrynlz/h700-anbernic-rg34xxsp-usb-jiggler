#ifndef USBJIGGLER_UI_MODEL_H
#define USBJIGGLER_UI_MODEL_H

#include "app_runtime.h"
#include "settings.h"

#include <SDL2/SDL.h>

typedef enum {
    UI_VIEW_MAIN = 0,
    UI_VIEW_SETTINGS,
    UI_VIEW_HELP,
    UI_VIEW_EXIT_CONFIRM
} UiView;

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_UP,
    UI_ACTION_DOWN,
    UI_ACTION_LEFT,
    UI_ACTION_RIGHT,
    UI_ACTION_ACCEPT,
    UI_ACTION_BACK,
    UI_ACTION_HELP
} UiAction;

typedef enum {
    UI_EFFECT_NONE = 0,
    UI_EFFECT_START = 1 << 0,
    UI_EFFECT_STOP = 1 << 1,
    UI_EFFECT_RETRY = 1 << 2,
    UI_EFFECT_SETTINGS_CHANGED = 1 << 3,
    UI_EFFECT_EXIT = 1 << 4
} UiEffect;

typedef struct {
    UiView view;
    int selected_setting;
    JigglerSettings settings;
} UiModel;

typedef struct {
    UiAction held_action;
    uint32_t held_since;
    uint32_t next_repeat;
    int held_source;
    int32_t held_device;
    int32_t held_control;
    int secondary_source;
    int32_t secondary_device;
    int32_t secondary_control;
    UiAction last_action;
    uint32_t last_action_at;
} UiInputTracker;

void ui_model_init(UiModel *model, const JigglerSettings *settings);
unsigned int ui_model_handle(UiModel *model, UiAction action, AppRuntimeState runtime_state,
                             unsigned int held_ms);
UiAction ui_action_from_sdl_event(const SDL_Event *event);
void ui_input_tracker_init(UiInputTracker *tracker);
UiAction ui_input_tracker_event(UiInputTracker *tracker, const SDL_Event *event, uint32_t ticks);
UiAction ui_input_tracker_repeat(UiInputTracker *tracker, uint32_t ticks, unsigned int *held_ms);
int ui_adjustment_step(int selected_setting, unsigned int held_ms);
const char *ui_view_name(UiView view);

#endif
