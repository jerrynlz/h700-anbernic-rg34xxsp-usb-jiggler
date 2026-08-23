#include "../src/ui_model.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
        return; \
    } \
} while (0)

static SDL_Event key_event(SDL_Keycode key) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    return event;
}

static SDL_Event button_event(uint8_t button) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYBUTTONDOWN;
    event.jbutton.button = button;
    return event;
}

static SDL_Event hat_event(uint8_t value) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYHATMOTION;
    event.jhat.value = value;
    return event;
}

static SDL_Event axis_event(uint8_t axis, int16_t value) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_JOYAXISMOTION;
    event.jaxis.axis = axis;
    event.jaxis.value = value;
    return event;
}

static void test_keyboard_and_joystick_mapping(void) {
    SDL_Event event;

    event = key_event(SDLK_UP); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_UP);
    event = key_event(SDLK_DOWN); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_DOWN);
    event = key_event(SDLK_LEFT); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_LEFT);
    event = key_event(SDLK_RIGHT); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_RIGHT);
    event = key_event(SDLK_RETURN); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_ACCEPT);
    event = key_event(SDLK_ESCAPE); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_BACK);
    event = key_event(SDLK_SPACE); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_HELP);
    event = key_event(SDLK_F12); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_NONE);

    event = button_event(0); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_ACCEPT);
    event = button_event(1); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_BACK);
    event = button_event(6); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_HELP);
    event = button_event(7); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_BACK);
    event = button_event(4); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_NONE);

    event = hat_event(SDL_HAT_UP); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_UP);
    event = hat_event(SDL_HAT_DOWN); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_DOWN);
    event = hat_event(SDL_HAT_LEFT); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_LEFT);
    event = hat_event(SDL_HAT_RIGHT); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_RIGHT);
    event = axis_event(0, -20000); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_LEFT);
    event = axis_event(0, 20000); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_RIGHT);
    event = axis_event(1, -20000); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_UP);
    event = axis_event(1, 20000); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_DOWN);
    event = axis_event(1, 100); CHECK(ui_action_from_sdl_event(&event) == UI_ACTION_NONE);
}

static void test_main_actions_and_views(void) {
    JigglerSettings settings;
    UiModel model;

    settings_defaults(&settings);
    ui_model_init(&model, &settings);
    CHECK(model.view == UI_VIEW_MAIN);
    CHECK(ui_model_handle(&model, UI_ACTION_ACCEPT, RUNTIME_READY, 0) == UI_EFFECT_START);
    CHECK(ui_model_handle(&model, UI_ACTION_ACCEPT, RUNTIME_ACTIVE, 0) == UI_EFFECT_STOP);
    CHECK(ui_model_handle(&model, UI_ACTION_ACCEPT, RUNTIME_DISCONNECTED, 0) == UI_EFFECT_RETRY);
    CHECK(ui_model_handle(&model, UI_ACTION_ACCEPT, RUNTIME_ERROR, 0) == UI_EFFECT_RETRY);
    CHECK(ui_model_handle(&model, UI_ACTION_ACCEPT, RUNTIME_PREPARING, 0) == UI_EFFECT_NONE);

    CHECK(ui_model_handle(&model, UI_ACTION_HELP, RUNTIME_READY, 0) == UI_EFFECT_NONE);
    CHECK(model.view == UI_VIEW_HELP);
    ui_model_handle(&model, UI_ACTION_BACK, RUNTIME_READY, 0);
    CHECK(model.view == UI_VIEW_MAIN);

    ui_model_handle(&model, UI_ACTION_BACK, RUNTIME_READY, 0);
    CHECK(model.view == UI_VIEW_EXIT_CONFIRM);
    CHECK(ui_model_handle(&model, UI_ACTION_BACK, RUNTIME_READY, 0) == UI_EFFECT_NONE);
    CHECK(model.view == UI_VIEW_MAIN);
    ui_model_handle(&model, UI_ACTION_BACK, RUNTIME_READY, 0);
    CHECK(ui_model_handle(&model, UI_ACTION_ACCEPT, RUNTIME_READY, 0) == UI_EFFECT_EXIT);
}

static void test_settings_navigation_and_acceleration(void) {
    JigglerSettings settings = { .interval_sec = 30, .move_px = 2, .pattern = JIGGLE_HORIZONTAL };
    UiModel model;

    ui_model_init(&model, &settings);
    ui_model_handle(&model, UI_ACTION_DOWN, RUNTIME_READY, 0);
    CHECK(model.view == UI_VIEW_SETTINGS);
    CHECK(model.selected_setting == 0);
    ui_model_handle(&model, UI_ACTION_UP, RUNTIME_READY, 0);
    CHECK(model.selected_setting == 2);
    ui_model_handle(&model, UI_ACTION_DOWN, RUNTIME_READY, 0);
    CHECK(model.selected_setting == 0);

    CHECK(ui_model_handle(&model, UI_ACTION_RIGHT, RUNTIME_READY, 0) == UI_EFFECT_SETTINGS_CHANGED);
    CHECK(model.settings.interval_sec == 31);
    ui_model_handle(&model, UI_ACTION_RIGHT, RUNTIME_READY, 800);
    CHECK(model.settings.interval_sec == 36);
    ui_model_handle(&model, UI_ACTION_RIGHT, RUNTIME_READY, 2000);
    CHECK(model.settings.interval_sec == 61);
    for (int index = 0; index < 20; index++) {
        ui_model_handle(&model, UI_ACTION_RIGHT, RUNTIME_READY, 2000);
    }
    CHECK(model.settings.interval_sec == 300);
    for (int index = 0; index < 20; index++) {
        ui_model_handle(&model, UI_ACTION_LEFT, RUNTIME_READY, 2000);
    }
    CHECK(model.settings.interval_sec == 1);

    ui_model_handle(&model, UI_ACTION_DOWN, RUNTIME_READY, 0);
    CHECK(model.selected_setting == 1);
    for (int index = 0; index < 20; index++) {
        ui_model_handle(&model, UI_ACTION_RIGHT, RUNTIME_READY, 2000);
    }
    CHECK(model.settings.move_px == JIGGLER_MAX_MOVE_PX);
    for (int index = 0; index < 20; index++) {
        ui_model_handle(&model, UI_ACTION_LEFT, RUNTIME_READY, 2000);
    }
    CHECK(model.settings.move_px == 1);

    ui_model_handle(&model, UI_ACTION_DOWN, RUNTIME_READY, 0);
    CHECK(model.selected_setting == 2);
    ui_model_handle(&model, UI_ACTION_LEFT, RUNTIME_READY, 3000);
    CHECK(model.settings.pattern == JIGGLE_RANDOM);
    ui_model_handle(&model, UI_ACTION_RIGHT, RUNTIME_READY, 3000);
    CHECK(model.settings.pattern == JIGGLE_HORIZONTAL);
    ui_model_handle(&model, UI_ACTION_ACCEPT, RUNTIME_READY, 0);
    CHECK(model.view == UI_VIEW_MAIN);
}

static void test_adjustment_steps(void) {
    CHECK(ui_adjustment_step(0, 0) == 1);
    CHECK(ui_adjustment_step(0, 700) == 5);
    CHECK(ui_adjustment_step(0, 1800) == 25);
    CHECK(ui_adjustment_step(1, 0) == 1);
    CHECK(ui_adjustment_step(1, 700) == 25);
    CHECK(ui_adjustment_step(1, 1800) == 200);
    CHECK(ui_adjustment_step(2, 9999) == 1);
}

static void test_input_tracker_dedupe_and_matching_release(void) {
    UiInputTracker tracker;
    SDL_Event event;
    UiAction action;
    unsigned int held_ms = 0;

    ui_input_tracker_init(&tracker);
    event = axis_event(0, 20000);
    event.jaxis.which = 2;
    CHECK(ui_input_tracker_event(&tracker, &event, 100) == UI_ACTION_RIGHT);
    event = axis_event(1, 0);
    event.jaxis.which = 2;
    CHECK(ui_input_tracker_event(&tracker, &event, 150) == UI_ACTION_NONE);
    CHECK(ui_input_tracker_repeat(&tracker, 430, &held_ms) == UI_ACTION_RIGHT);
    CHECK(held_ms == 330);
    event = axis_event(0, 0);
    event.jaxis.which = 2;
    CHECK(ui_input_tracker_event(&tracker, &event, 450) == UI_ACTION_NONE);
    CHECK(ui_input_tracker_repeat(&tracker, 800, &held_ms) == UI_ACTION_NONE);

    event = hat_event(SDL_HAT_UP);
    event.jhat.which = 3;
    event.jhat.hat = 1;
    CHECK(ui_input_tracker_event(&tracker, &event, 1000) == UI_ACTION_UP);
    event = axis_event(0, 0);
    event.jaxis.which = 3;
    CHECK(ui_input_tracker_event(&tracker, &event, 1100) == UI_ACTION_NONE);
    CHECK(ui_input_tracker_repeat(&tracker, 1330, &held_ms) == UI_ACTION_UP);
    event = hat_event(SDL_HAT_CENTERED);
    event.jhat.which = 3;
    event.jhat.hat = 1;
    CHECK(ui_input_tracker_event(&tracker, &event, 1350) == UI_ACTION_NONE);
    CHECK(ui_input_tracker_repeat(&tracker, 1700, &held_ms) == UI_ACTION_NONE);

    event = button_event(0);
    CHECK(ui_input_tracker_event(&tracker, &event, 2000) == UI_ACTION_ACCEPT);
    event = key_event(SDLK_RETURN);
    CHECK(ui_input_tracker_event(&tracker, &event, 2020) == UI_ACTION_NONE);
    CHECK(ui_input_tracker_event(&tracker, &event, 2100) == UI_ACTION_ACCEPT);

    event = key_event(SDLK_RIGHT);
    CHECK(ui_input_tracker_event(&tracker, &event, 3000) == UI_ACTION_RIGHT);
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYUP;
    event.key.keysym.sym = SDLK_RIGHT;
    CHECK(ui_input_tracker_event(&tracker, &event, 3100) == UI_ACTION_NONE);
    action = ui_input_tracker_repeat(&tracker, 3500, &held_ms);
    CHECK(action == UI_ACTION_NONE);

    ui_input_tracker_init(&tracker);
    event = key_event(SDLK_RIGHT);
    CHECK(ui_input_tracker_event(&tracker, &event, 4000) == UI_ACTION_RIGHT);
    event = hat_event(SDL_HAT_RIGHT);
    event.jhat.which = 5;
    event.jhat.hat = 0;
    CHECK(ui_input_tracker_event(&tracker, &event, 4020) == UI_ACTION_NONE);
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYUP;
    event.key.keysym.sym = SDLK_RIGHT;
    CHECK(ui_input_tracker_event(&tracker, &event, 4100) == UI_ACTION_NONE);
    CHECK(ui_input_tracker_repeat(&tracker, 4330, &held_ms) == UI_ACTION_RIGHT);
    event = hat_event(SDL_HAT_CENTERED);
    event.jhat.which = 5;
    event.jhat.hat = 0;
    CHECK(ui_input_tracker_event(&tracker, &event, 4350) == UI_ACTION_NONE);
    CHECK(ui_input_tracker_repeat(&tracker, 4700, &held_ms) == UI_ACTION_NONE);
}

int main(void) {
    test_keyboard_and_joystick_mapping();
    test_main_actions_and_views();
    test_settings_navigation_and_acceleration();
    test_adjustment_steps();
    test_input_tracker_dedupe_and_matching_release();

    if (failures != 0) {
        fprintf(stderr, "%d UI test(s) failed\n", failures);
        return 1;
    }
    puts("UI_TESTS_PASS=YES");
    return 0;
}
