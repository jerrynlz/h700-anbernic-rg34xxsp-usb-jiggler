#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "app_runtime.h"
#include "settings.h"
#include "ui_model.h"

#include <SDL2/SDL.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

enum {
    SCREEN_WIDTH = 640,
    SCREEN_HEIGHT = 480,
    SETTING_COUNT = 3
};

enum {
    COLOR_VOID = 0x060916,
    COLOR_NIGHT = 0x0b1026,
    COLOR_PANEL = 0x111a38,
    COLOR_PANEL_LIGHT = 0x172449,
    COLOR_GRID = 0x172b53,
    COLOR_CYAN = 0x20e6ff,
    COLOR_CYAN_DARK = 0x087b99,
    COLOR_PINK = 0xff4f9a,
    COLOR_PINK_DARK = 0x8b285b,
    COLOR_LIME = 0x5dffb0,
    COLOR_AMBER = 0xffcf5a,
    COLOR_RED = 0xff526d,
    COLOR_WHITE = 0xf4f7ff,
    COLOR_TEXT = 0xb8c7e8,
    COLOR_MUTED = 0x7182aa
};

typedef struct {
    char character;
    uint8_t rows[7];
} Glyph;

static const Glyph glyphs[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'!', {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
    {'%', {0x19,0x1a,0x02,0x04,0x08,0x0b,0x13}},
    {'+', {0x00,0x04,0x04,0x1f,0x04,0x04,0x00}},
    {'-', {0x00,0x00,0x00,0x1f,0x00,0x00,0x00}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x0c,0x0c}},
    {'/', {0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
    {':', {0x00,0x0c,0x0c,0x00,0x0c,0x0c,0x00}},
    {'<', {0x02,0x04,0x08,0x10,0x08,0x04,0x02}},
    {'>', {0x10,0x08,0x04,0x02,0x04,0x08,0x10}},
    {'?', {0x0e,0x11,0x01,0x02,0x04,0x00,0x04}},
    {'0', {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}},
    {'1', {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}},
    {'2', {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}},
    {'3', {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}},
    {'4', {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}},
    {'5', {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e}},
    {'6', {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e}},
    {'7', {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8', {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}},
    {'9', {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e}},
    {'A', {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}},
    {'B', {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}},
    {'C', {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e}},
    {'D', {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}},
    {'E', {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}},
    {'F', {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}},
    {'G', {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f}},
    {'H', {0x11,0x11,0x11,0x1f,0x11,0x11,0x11}},
    {'I', {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}},
    {'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0c}},
    {'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
    {'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1f}},
    {'M', {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}},
    {'N', {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
    {'O', {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}},
    {'P', {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}},
    {'Q', {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}},
    {'R', {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}},
    {'S', {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}},
    {'T', {0x1f,0x04,0x04,0x04,0x04,0x04,0x04}},
    {'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0e}},
    {'V', {0x11,0x11,0x11,0x11,0x11,0x0a,0x04}},
    {'W', {0x11,0x11,0x11,0x15,0x15,0x1b,0x11}},
    {'X', {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}},
    {'Y', {0x11,0x11,0x0a,0x04,0x04,0x04,0x04}},
    {'Z', {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}}
};

static volatile sig_atomic_t stop_requested;

static uint8_t color_red(uint32_t color) { return (uint8_t)(color >> 16); }
static uint8_t color_green(uint32_t color) { return (uint8_t)(color >> 8); }
static uint8_t color_blue(uint32_t color) { return (uint8_t)color; }

static void set_color_alpha(SDL_Renderer *renderer, uint32_t color, uint8_t alpha) {
    SDL_SetRenderDrawColor(renderer, color_red(color), color_green(color), color_blue(color), alpha);
}

static void set_color(SDL_Renderer *renderer, uint32_t color) {
    set_color_alpha(renderer, color, SDL_ALPHA_OPAQUE);
}

static const uint8_t *find_glyph(char character) {
    char upper = (char)toupper((unsigned char)character);
    size_t index;

    for (index = 0; index < sizeof(glyphs) / sizeof(glyphs[0]); index++) {
        if (glyphs[index].character == upper) {
            return glyphs[index].rows;
        }
    }
    return glyphs[0].rows;
}

static int text_width(const char *text, int scale) {
    return (int)strlen(text) * 6 * scale;
}

static void draw_text(SDL_Renderer *renderer, int x, int y, int scale, uint32_t color, const char *text) {
    SDL_Rect pixel = {0, 0, scale, scale};

    set_color(renderer, color);
    while (*text != '\0') {
        const uint8_t *rows = find_glyph(*text++);
        for (int row = 0; row < 7; row++) {
            for (int column = 0; column < 5; column++) {
                if (rows[row] & (uint8_t)(1U << (4 - column))) {
                    pixel.x = x + column * scale;
                    pixel.y = y + row * scale;
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        x += 6 * scale;
    }
}

static void draw_glow_text(SDL_Renderer *renderer, int x, int y, int scale,
                           uint32_t glow, uint32_t color, const char *text) {
    draw_text(renderer, x + 2, y + 2, scale, glow, text);
    draw_text(renderer, x, y, scale, color, text);
}

static void draw_centered(SDL_Renderer *renderer, int y, int scale, uint32_t color, const char *text) {
    draw_text(renderer, (SCREEN_WIDTH - text_width(text, scale)) / 2, y, scale, color, text);
}

static void fill_rectangle(SDL_Renderer *renderer, int x, int y, int width, int height, uint32_t color) {
    SDL_Rect rectangle = {x, y, width, height};
    set_color(renderer, color);
    SDL_RenderFillRect(renderer, &rectangle);
}

static void outline_rectangle(SDL_Renderer *renderer, int x, int y, int width, int height, uint32_t color) {
    SDL_Rect rectangle = {x, y, width, height};
    set_color(renderer, color);
    SDL_RenderDrawRect(renderer, &rectangle);
}

static void draw_panel(SDL_Renderer *renderer, int x, int y, int width, int height,
                       uint32_t fill, uint32_t border, int selected) {
    fill_rectangle(renderer, x + 5, y + 6, width, height, COLOR_VOID);
    fill_rectangle(renderer, x, y, width, height, fill);
    outline_rectangle(renderer, x, y, width, height, border);
    if (selected) {
        outline_rectangle(renderer, x + 3, y + 3, width - 6, height - 6, border);
    }
    set_color(renderer, border);
    SDL_RenderDrawLine(renderer, x, y + 12, x, y);
    SDL_RenderDrawLine(renderer, x, y, x + 12, y);
    SDL_RenderDrawLine(renderer, x + width - 13, y, x + width - 1, y);
    SDL_RenderDrawLine(renderer, x + width - 1, y, x + width - 1, y + 12);
    SDL_RenderDrawLine(renderer, x, y + height - 13, x, y + height - 1);
    SDL_RenderDrawLine(renderer, x, y + height - 1, x + 12, y + height - 1);
    SDL_RenderDrawLine(renderer, x + width - 13, y + height - 1, x + width - 1, y + height - 1);
    SDL_RenderDrawLine(renderer, x + width - 1, y + height - 13, x + width - 1, y + height - 1);
}

static void draw_background(SDL_Renderer *renderer, uint32_t ticks) {
    static const SDL_Point stars[] = {
        {39,43},{92,104},{154,65},{225,118},{302,52},{380,95},{457,38},{531,121},{603,71},
        {22,176},{112,151},{188,203},{273,165},{347,211},{424,154},{514,197},{617,162}
    };
    int horizon = 170;
    int drift = (int)((ticks / 45U) % 24U);

    set_color(renderer, COLOR_VOID);
    SDL_RenderClear(renderer);
    fill_rectangle(renderer, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_VOID);
    for (int band = 0; band < 12; band++) {
        fill_rectangle(renderer, 0, band * 40, SCREEN_WIDTH, 40,
                       band < 5 ? COLOR_NIGHT : (band < 8 ? 0x0c142c : 0x0d1731));
    }
    for (size_t index = 0; index < sizeof(stars) / sizeof(stars[0]); index++) {
        int brightness = ((ticks / 180U + index) % 3U) == 0U ? 2 : 1;
        fill_rectangle(renderer, stars[index].x, stars[index].y, brightness, brightness,
                       brightness == 2 ? COLOR_CYAN : COLOR_MUTED);
    }
    set_color(renderer, COLOR_GRID);
    for (int x = -160; x <= SCREEN_WIDTH + 160; x += 64) {
        SDL_RenderDrawLine(renderer, SCREEN_WIDTH / 2, horizon, x + drift, SCREEN_HEIGHT);
    }
    for (int y = horizon; y < SCREEN_HEIGHT; y += 20) {
        int warped = horizon + ((y - horizon + drift) % (SCREEN_HEIGHT - horizon));
        SDL_RenderDrawLine(renderer, 0, warped, SCREEN_WIDTH, warped);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    set_color_alpha(renderer, 0x000000, 36);
    for (int y = 1; y < SCREEN_HEIGHT; y += 3) {
        SDL_RenderDrawLine(renderer, 0, y, SCREEN_WIDTH, y);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static uint32_t runtime_color(AppRuntimeState state) {
    switch (state) {
        case RUNTIME_READY: return COLOR_LIME;
        case RUNTIME_ACTIVE: return COLOR_CYAN;
        case RUNTIME_ERROR:
        case RUNTIME_DISCONNECTED: return COLOR_RED;
        case RUNTIME_EXITING: return COLOR_PINK;
        default: return COLOR_AMBER;
    }
}

static void draw_status_chip(SDL_Renderer *renderer, AppRuntimeState state) {
    uint32_t color = runtime_color(state);
    const char *name = app_runtime_state_name(state);
    int width = text_width(name, 2) + 34;
    int x = SCREEN_WIDTH - width - 28;

    fill_rectangle(renderer, x, 28, width, 28, COLOR_PANEL);
    outline_rectangle(renderer, x, 28, width, 28, color);
    fill_rectangle(renderer, x + 10, 39, 6, 6, color);
    draw_text(renderer, x + 24, 35, 2, color, name);
}

static void draw_header(SDL_Renderer *renderer, AppRuntimeState state) {
    draw_glow_text(renderer, 28, 26, 3, COLOR_CYAN_DARK, COLOR_CYAN, "USB JIGGLER");
    draw_text(renderer, 30, 54, 1, COLOR_MUTED, "RG34XXSP // HID CONTROL DECK");
    draw_status_chip(renderer, state);
}

static void draw_button_hint(SDL_Renderer *renderer, int x, int y, const char *button,
                             const char *action, uint32_t color) {
    int button_width = text_width(button, 1) + 16;

    fill_rectangle(renderer, x, y, button_width, 20, color);
    draw_text(renderer, x + 8, y + 6, 1, COLOR_VOID, button);
    draw_text(renderer, x + button_width + 8, y + 6, 1, COLOR_TEXT, action);
}

static void draw_footer(SDL_Renderer *renderer, AppRuntimeState state) {
    const char *primary = state == RUNTIME_ACTIVE ? "STOP" :
                          (state == RUNTIME_ERROR || state == RUNTIME_DISCONNECTED ? "RETRY" : "START");
    fill_rectangle(renderer, 0, 442, SCREEN_WIDTH, 38, COLOR_VOID);
    SDL_RenderDrawLine(renderer, 0, 442, SCREEN_WIDTH, 442);
    draw_button_hint(renderer, 22, 451, "A", primary, state == RUNTIME_ACTIVE ? COLOR_PINK : COLOR_LIME);
    draw_button_hint(renderer, 166, 451, "DPAD", "SETTINGS", COLOR_CYAN);
    draw_button_hint(renderer, 370, 451, "START", "HELP", COLOR_AMBER);
    draw_button_hint(renderer, 535, 451, "B", "EXIT", COLOR_PINK);
}

static void motion_point(JigglerPattern pattern, uint32_t ticks, int amplitude,
                         int movement, int target_x, int target_y, int *x, int *y) {
    unsigned int phase = ticks % 1000U;
    unsigned int travel = phase <= 500U ? phase : 1000U - phase;

    *x = 0;
    *y = 0;
    if (pattern == JIGGLE_HORIZONTAL) {
        *x = amplitude * (int)travel / 500;
    } else if (pattern == JIGGLE_VERTICAL) {
        *y = amplitude * (int)travel / 500;
    } else if (pattern == JIGGLE_SQUARE) {
        unsigned int segment = (ticks / 500U) % 4U;
        double local = (double)(ticks % 500U) / 500.0;
        if (segment == 0U) { *x = (int)(local * amplitude); *y = 0; }
        if (segment == 1U) { *x = amplitude; *y = (int)(local * amplitude); }
        if (segment == 2U) { *x = (int)(amplitude - local * amplitude); *y = amplitude; }
        if (segment == 3U) { *x = 0; *y = (int)(amplitude - local * amplitude); }
    } else {
        if (movement < 1) movement = 1;
        *x = target_x * amplitude * (int)travel / (movement * 500);
        *y = target_y * amplitude * (int)travel / (movement * 500);
    }
}

static void draw_cursor(SDL_Renderer *renderer, int x, int y, uint32_t color) {
    set_color(renderer, color);
    SDL_RenderDrawLine(renderer, x - 11, y, x + 11, y);
    SDL_RenderDrawLine(renderer, x, y - 11, x, y + 11);
    outline_rectangle(renderer, x - 5, y - 5, 11, 11, COLOR_WHITE);
    fill_rectangle(renderer, x - 2, y - 2, 5, 5, color);
}

static void draw_visualizer(SDL_Renderer *renderer, const AppRuntimeSnapshot *snapshot,
                            uint32_t ticks, uint32_t pulse_until) {
    int x = 30;
    int y = 96;
    int width = 360;
    int height = 252;
    int center_x = x + width / 2;
    int center_y = y + height / 2 + 8;
    int amplitude = 22 + snapshot->settings.move_px * 58 / JIGGLER_MAX_MOVE_PX;
    uint32_t gesture_ticks = (uint32_t)snapshot->gesture_elapsed_ms;
    uint32_t accent = snapshot->state == RUNTIME_ACTIVE ? COLOR_CYAN : COLOR_MUTED;

    draw_panel(renderer, x, y, width, height, COLOR_PANEL, accent, snapshot->state == RUNTIME_ACTIVE);
    draw_text(renderer, x + 18, y + 16, 1, COLOR_MUTED, "LIVE MOVEMENT VECTOR");
    set_color(renderer, COLOR_GRID);
    SDL_RenderDrawLine(renderer, x + 18, center_y, x + width - 18, center_y);
    SDL_RenderDrawLine(renderer, center_x, y + 40, center_x, y + height - 24);
    for (int ring = 24; ring <= 72; ring += 24) {
        outline_rectangle(renderer, center_x - ring, center_y - ring / 2, ring * 2, ring, COLOR_GRID);
    }

    for (int trail = 10; trail >= 0; trail--) {
        int offset_x;
        int offset_y;
        int size = trail == 0 ? 4 : 2;
        uint32_t sample_ticks = gesture_ticks > (uint32_t)trail * 24U
                                    ? gesture_ticks - (uint32_t)trail * 24U
                                    : 0;
        if (snapshot->gesture_active) {
            motion_point(snapshot->settings.pattern, sample_ticks, amplitude,
                         snapshot->settings.move_px, snapshot->gesture_target_x,
                         snapshot->gesture_target_y, &offset_x, &offset_y);
        } else {
            offset_x = 0;
            offset_y = 0;
        }
        fill_rectangle(renderer, center_x + offset_x - size / 2, center_y + offset_y - size / 2,
                       size, size, trail < 4 ? COLOR_CYAN : COLOR_CYAN_DARK);
    }
    {
        int offset_x;
        int offset_y;
        if (snapshot->gesture_active) {
            motion_point(snapshot->settings.pattern, gesture_ticks, amplitude,
                         snapshot->settings.move_px, snapshot->gesture_target_x,
                         snapshot->gesture_target_y, &offset_x, &offset_y);
        } else {
            offset_x = 0;
            offset_y = 0;
        }
        draw_cursor(renderer, center_x + offset_x, center_y + offset_y, accent);
    }

    if ((int32_t)(pulse_until - ticks) > 0) {
        int pulse = 16 + (int)((450U - (pulse_until - ticks)) / 7U);
        outline_rectangle(renderer, center_x - pulse, center_y - pulse, pulse * 2, pulse * 2, COLOR_PINK);
    }
    draw_text(renderer, x + 18, y + height - 24, 1, COLOR_MUTED,
              snapshot->gesture_active ? "SMOOTH GESTURE // NET ZERO PATH" :
              (snapshot->state == RUNTIME_ACTIVE ? "SIGNAL ONLINE // WAITING FOR TIMER" : "PREVIEW ARMED // PRESS A"));
}

static void draw_meter(SDL_Renderer *renderer, int x, int y, int width, int value, int maximum,
                       uint32_t color) {
    int filled = value * width / maximum;
    fill_rectangle(renderer, x, y, width, 7, COLOR_GRID);
    if (filled > 0) {
        fill_rectangle(renderer, x, y, filled, 7, color);
    }
}

static void draw_stats(SDL_Renderer *renderer, const AppRuntimeSnapshot *snapshot) {
    char value[64];
    int x = 410;
    int y = 96;
    int width = 200;
    uint32_t color = runtime_color(snapshot->state);

    draw_panel(renderer, x, y, width, 252, COLOR_PANEL, COLOR_GRID, 0);
    draw_text(renderer, x + 18, y + 16, 1, COLOR_MUTED, "JIGGLE PROFILE");
    draw_text(renderer, x + 18, y + 46, 1, COLOR_TEXT, "INTERVAL");
    snprintf(value, sizeof(value), "%d SEC", snapshot->settings.interval_sec);
    draw_text(renderer, x + 18, y + 61, 2, COLOR_WHITE, value);
    draw_meter(renderer, x + 18, y + 83, width - 36, snapshot->settings.interval_sec, 300, COLOR_CYAN);
    draw_text(renderer, x + 18, y + 102, 1, COLOR_TEXT, "MOVEMENT");
    snprintf(value, sizeof(value), "%d PX", snapshot->settings.move_px);
    draw_text(renderer, x + 18, y + 117, 2, COLOR_WHITE, value);
    draw_meter(renderer, x + 18, y + 139, width - 36, snapshot->settings.move_px, JIGGLER_MAX_MOVE_PX, COLOR_PINK);
    draw_text(renderer, x + 18, y + 158, 1, COLOR_TEXT, "PATTERN");
    draw_text(renderer, x + 18, y + 174, 2, COLOR_AMBER, settings_pattern_name(snapshot->settings.pattern));
    snprintf(value, sizeof(value), "REPORTS %llu", (unsigned long long)snapshot->report_count);
    draw_text(renderer, x + 18, y + 209, 1, color, value);
}

static void draw_countdown(SDL_Renderer *renderer, const AppRuntimeSnapshot *snapshot) {
    int x = 30;
    int y = 368;
    int width = 580;
    int total_ms = snapshot->settings.interval_sec * 1000;
    uint64_t remaining = snapshot->countdown_ms > (uint64_t)total_ms
                             ? (uint64_t)total_ms
                             : snapshot->countdown_ms;
    int elapsed = snapshot->state == RUNTIME_ACTIVE
                      ? total_ms - (int)remaining
                      : 0;
    char label[64];

    draw_panel(renderer, x, y, width, 54, COLOR_PANEL, COLOR_GRID, 0);
    draw_text(renderer, x + 16, y + 14, 1, COLOR_MUTED, "NEXT VECTOR");
    if (snapshot->state == RUNTIME_ACTIVE) {
        snprintf(label, sizeof(label), "%llu.%01llu SEC",
                 (unsigned long long)(snapshot->countdown_ms / 1000U),
                 (unsigned long long)((snapshot->countdown_ms % 1000U) / 100U));
    } else {
        snprintf(label, sizeof(label), "STANDBY");
    }
    draw_text(renderer, x + width - text_width(label, 1) - 16, y + 14, 1,
              snapshot->state == RUNTIME_ACTIVE ? COLOR_CYAN : COLOR_MUTED, label);
    draw_meter(renderer, x + 16, y + 34, width - 32, elapsed, total_ms, COLOR_CYAN);
}

static void draw_main_screen(SDL_Renderer *renderer, const AppRuntimeSnapshot *snapshot,
                             uint32_t ticks, uint32_t pulse_until) {
    draw_header(renderer, snapshot->state);
    draw_visualizer(renderer, snapshot, ticks, pulse_until);
    draw_stats(renderer, snapshot);
    draw_countdown(renderer, snapshot);
    draw_footer(renderer, snapshot->state);
}

static void draw_spinner(SDL_Renderer *renderer, int center_x, int center_y, uint32_t ticks) {
    int active = (int)((ticks / 90U) % 12U);
    for (int index = 0; index < 12; index++) {
        double angle = (double)index * 6.283185307179586 / 12.0;
        int x = center_x + (int)(cos(angle) * 45.0);
        int y = center_y + (int)(sin(angle) * 45.0);
        int distance = (active - index + 12) % 12;
        uint32_t color = distance == 0 ? COLOR_CYAN : (distance < 4 ? COLOR_CYAN_DARK : COLOR_GRID);
        fill_rectangle(renderer, x - 3, y - 3, 7, 7, color);
    }
}

static void draw_preparing_screen(SDL_Renderer *renderer, const AppRuntimeSnapshot *snapshot, uint32_t ticks) {
    const char *title = snapshot->state == RUNTIME_EXITING && snapshot->shutdown_complete && snapshot->shutdown_result != 0
                            ? "CLEANUP NEEDS ATTENTION" :
                        snapshot->state == RUNTIME_RETRYING ? "RETRYING HID LINK" :
                        (snapshot->state == RUNTIME_EXITING ? "CLEANING USB STATE" : "PREPARING USB HID");
    const char *detail = snapshot->state == RUNTIME_EXITING && snapshot->shutdown_complete && snapshot->shutdown_result != 0
                             ? snapshot->error
                             : (snapshot->state == RUNTIME_EXITING
                                    ? "STOPPING REPORTS // RESTORING USB // RELEASING MODULE"
                                    : "VALIDATING MODULE // CLAIMING GADGET // OPENING HID");

    draw_header(renderer, snapshot->state);
    draw_panel(renderer, 94, 104, 452, 282, COLOR_PANEL, COLOR_AMBER, 0);
    draw_spinner(renderer, 320, 212, ticks);
    draw_centered(renderer, 284, 3, COLOR_WHITE, title);
    draw_centered(renderer, 322, 1, COLOR_TEXT, detail);
    draw_meter(renderer, 150, 352, 340, (int)((ticks / 35U) % 341U), 340, COLOR_AMBER);
    fill_rectangle(renderer, 0, 442, SCREEN_WIDTH, 38, COLOR_VOID);
    if (snapshot->state == RUNTIME_EXITING) {
        draw_centered(renderer, 457, 1, COLOR_TEXT, "PLEASE WAIT // SAFE CLEANUP IN PROGRESS");
    } else {
        draw_button_hint(renderer, 178, 451, "START", "HELP", COLOR_AMBER);
        draw_button_hint(renderer, 390, 451, "B", "EXIT", COLOR_PINK);
    }
}

static void draw_wrapped_text(SDL_Renderer *renderer, int x, int y, int width, int scale,
                              int line_height, uint32_t color, const char *text) {
    char line[96];
    size_t line_length = 0;
    const char *cursor = text;

    while (*cursor != '\0') {
        const char *word_start;
        size_t word_length;
        while (*cursor == ' ') cursor++;
        word_start = cursor;
        while (*cursor != '\0' && *cursor != ' ') cursor++;
        word_length = (size_t)(cursor - word_start);
        if (word_length == 0) break;
        if (line_length > 0 && (int)((line_length + 1U + word_length) * 6U * (unsigned int)scale) > width) {
            line[line_length] = '\0';
            draw_text(renderer, x, y, scale, color, line);
            y += line_height;
            line_length = 0;
        }
        if (line_length > 0 && line_length + 1U < sizeof(line)) {
            line[line_length++] = ' ';
        }
        if (word_length > sizeof(line) - line_length - 1U) {
            word_length = sizeof(line) - line_length - 1U;
        }
        memcpy(line + line_length, word_start, word_length);
        line_length += word_length;
    }
    if (line_length > 0) {
        line[line_length] = '\0';
        draw_text(renderer, x, y, scale, color, line);
    }
}

static void draw_error_screen(SDL_Renderer *renderer, const AppRuntimeSnapshot *snapshot, uint32_t ticks) {
    const char *title = snapshot->state == RUNTIME_DISCONNECTED ? "HID LINK LOST" : "SETUP NEEDS ATTENTION";
    int pulse = 8 + (int)((ticks / 120U) % 4U);

    draw_header(renderer, snapshot->state);
    draw_panel(renderer, 70, 104, 500, 288, COLOR_PANEL, COLOR_RED, 1);
    set_color(renderer, COLOR_RED);
    SDL_RenderDrawLine(renderer, 320, 132, 280, 200);
    SDL_RenderDrawLine(renderer, 280, 200, 360, 200);
    SDL_RenderDrawLine(renderer, 360, 200, 320, 132);
    fill_rectangle(renderer, 318, 153, 5, 24, COLOR_RED);
    fill_rectangle(renderer, 318, 184, 5, 5, COLOR_RED);
    outline_rectangle(renderer, 272 - pulse, 124 - pulse, 96 + pulse * 2, 84 + pulse * 2, COLOR_PINK_DARK);
    draw_centered(renderer, 224, 3, COLOR_RED, title);
    draw_wrapped_text(renderer, 112, 268, 416, 2, 26, COLOR_TEXT, snapshot->error);
    draw_centered(renderer, 348, 1, COLOR_AMBER, "CHECK THE USB C CONNECTION THEN RETRY");
    fill_rectangle(renderer, 0, 442, SCREEN_WIDTH, 38, COLOR_VOID);
    draw_button_hint(renderer, 120, 451, "A", "SAFE RETRY", COLOR_LIME);
    draw_button_hint(renderer, 330, 451, "START", "HELP", COLOR_AMBER);
    draw_button_hint(renderer, 520, 451, "B", "EXIT", COLOR_PINK);
}

static void draw_value_bar(SDL_Renderer *renderer, int x, int y, int width, int value, int maximum,
                           uint32_t color) {
    fill_rectangle(renderer, x, y, width, 9, COLOR_GRID);
    fill_rectangle(renderer, x, y, value * width / maximum, 9, color);
    outline_rectangle(renderer, x, y, width, 9, color);
}

static void draw_settings_screen(SDL_Renderer *renderer, const UiModel *model,
                                 const AppRuntimeSnapshot *snapshot) {
    const char *labels[SETTING_COUNT] = {"INTERVAL", "MOVEMENT", "PATTERN"};
    const char *descriptions[SETTING_COUNT] = {"TIME BETWEEN MOVES", "CURSOR TRAVEL SIZE", "MOVEMENT ROUTE"};
    char value[64];

    draw_header(renderer, snapshot->state);
    draw_glow_text(renderer, 30, 83, 3, COLOR_PINK_DARK, COLOR_PINK, "PROFILE TUNER");
    draw_text(renderer, 32, 111, 1, COLOR_MUTED, "DPAD UP DOWN SELECTS // LEFT RIGHT ADJUSTS");

    for (int row = 0; row < SETTING_COUNT; row++) {
        int y = 140 + row * 88;
        int selected = row == model->selected_setting;
        uint32_t color = selected ? COLOR_CYAN : COLOR_GRID;

        draw_panel(renderer, 30, y, 580, 72, selected ? COLOR_PANEL_LIGHT : COLOR_PANEL, color, selected);
        if (selected) {
            draw_text(renderer, 46, y + 27, 2, COLOR_CYAN, ">");
        }
        draw_text(renderer, 76, y + 16, 2, selected ? COLOR_WHITE : COLOR_TEXT, labels[row]);
        draw_text(renderer, 76, y + 42, 1, COLOR_MUTED, descriptions[row]);
        if (row == 0) {
            snprintf(value, sizeof(value), "<  %d SEC  >", model->settings.interval_sec);
            draw_value_bar(renderer, 390, y + 49, 186, model->settings.interval_sec, 300, COLOR_CYAN);
        } else if (row == 1) {
            snprintf(value, sizeof(value), "<  %d PX  >", model->settings.move_px);
            draw_value_bar(renderer, 390, y + 49, 186, model->settings.move_px, JIGGLER_MAX_MOVE_PX, COLOR_PINK);
        } else {
            snprintf(value, sizeof(value), "<  %s  >", settings_pattern_name(model->settings.pattern));
            for (int dot = 0; dot < JIGGLE_PATTERN_COUNT; dot++) {
                fill_rectangle(renderer, 438 + dot * 28, y + 50, 12, 8,
                               dot == (int)model->settings.pattern ? COLOR_AMBER : COLOR_GRID);
            }
        }
        draw_text(renderer, 580 - text_width(value, 2), y + 17, 2,
                  selected ? color : COLOR_TEXT, value);
    }
    fill_rectangle(renderer, 0, 442, SCREEN_WIDTH, 38, COLOR_VOID);
    draw_button_hint(renderer, 144, 451, "A", "SAVE AND BACK", COLOR_LIME);
    draw_button_hint(renderer, 400, 451, "B", "BACK", COLOR_PINK);
}

static void draw_help_row(SDL_Renderer *renderer, int y, const char *button,
                          const char *title, const char *description, uint32_t color) {
    draw_button_hint(renderer, 54, y, button, title, color);
    draw_text(renderer, 220, y + 6, 1, COLOR_TEXT, description);
}

static void draw_help_screen(SDL_Renderer *renderer, const AppRuntimeSnapshot *snapshot) {
    draw_header(renderer, snapshot->state);
    draw_glow_text(renderer, 30, 84, 3, COLOR_CYAN_DARK, COLOR_CYAN, "QUICK MANUAL");
    draw_panel(renderer, 30, 132, 580, 236, COLOR_PANEL, COLOR_GRID, 0);
    draw_text(renderer, 54, 141, 1, COLOR_AMBER, "CONNECT USB C TO THE HOST COMPUTER BEFORE STARTING");
    draw_help_row(renderer, 158, "A", "START STOP", "TOGGLE HOST CURSOR MOVEMENT", COLOR_LIME);
    draw_help_row(renderer, 203, "DPAD", "TUNE", "CHOOSE INTERVAL SIZE AND PATTERN", COLOR_CYAN);
    draw_help_row(renderer, 248, "START", "HELP", "OPEN OR CLOSE THIS MANUAL", COLOR_AMBER);
    draw_help_row(renderer, 293, "B", "BACK EXIT", "GO BACK OR CONFIRM CLEAN EXIT", COLOR_PINK);
    draw_text(renderer, 54, 338, 1, COLOR_MUTED, "USB SETUP IS REVERSIBLE AND CLEANED WHEN THE APP EXITS");
    fill_rectangle(renderer, 0, 442, SCREEN_WIDTH, 38, COLOR_VOID);
    draw_button_hint(renderer, 238, 451, "START OR B", "BACK", COLOR_AMBER);
}

static void draw_exit_overlay(SDL_Renderer *renderer) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    set_color_alpha(renderer, COLOR_VOID, 210);
    SDL_Rect full = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    SDL_RenderFillRect(renderer, &full);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    draw_panel(renderer, 106, 142, 428, 202, COLOR_PANEL, COLOR_PINK, 1);
    draw_centered(renderer, 174, 3, COLOR_WHITE, "EXIT USB JIGGLER?");
    draw_centered(renderer, 226, 1, COLOR_TEXT, "MOUSE MOVEMENT WILL STOP");
    draw_centered(renderer, 248, 1, COLOR_TEXT, "OWNED USB STATE WILL BE CLEANED");
    draw_button_hint(renderer, 188, 292, "A", "EXIT", COLOR_PINK);
    draw_button_hint(renderer, 342, 292, "B", "CANCEL", COLOR_CYAN);
}

static void render_application(SDL_Renderer *renderer, const UiModel *model,
                               const AppRuntimeSnapshot *snapshot, uint32_t ticks,
                               uint32_t pulse_until) {
    draw_background(renderer, ticks);
    if (model->view == UI_VIEW_SETTINGS) {
        draw_settings_screen(renderer, model, snapshot);
    } else if (model->view == UI_VIEW_HELP) {
        draw_help_screen(renderer, snapshot);
    } else if (snapshot->state == RUNTIME_PREPARING || snapshot->state == RUNTIME_RETRYING ||
               snapshot->state == RUNTIME_EXITING) {
        draw_preparing_screen(renderer, snapshot, ticks);
    } else if (snapshot->state == RUNTIME_ERROR || snapshot->state == RUNTIME_DISCONNECTED) {
        draw_error_screen(renderer, snapshot, ticks);
    } else {
        draw_main_screen(renderer, snapshot, ticks, pulse_until);
    }
    if (model->view == UI_VIEW_EXIT_CONFIRM) {
        draw_exit_overlay(renderer);
    }
}

static int append_path(char *destination, size_t destination_size,
                       const char *directory, const char *relative) {
    size_t directory_length = strlen(directory);
    size_t relative_length = strlen(relative);

    if (directory_length + 1U + relative_length + 1U > destination_size) {
        return -1;
    }
    memcpy(destination, directory, directory_length);
    destination[directory_length] = '/';
    memcpy(destination + directory_length + 1U, relative, relative_length + 1U);
    return 0;
}

static int application_directory(const char *argument, char *directory, size_t directory_size) {
    char resolved[PATH_MAX];
    char *slash;

    if (realpath(argument, resolved) == NULL) {
        if (getcwd(resolved, sizeof(resolved)) == NULL) return -1;
    } else {
        slash = strrchr(resolved, '/');
        if (slash == NULL) return -1;
        *slash = '\0';
    }
    if (strlen(resolved) + 1U > directory_size) return -1;
    memcpy(directory, resolved, strlen(resolved) + 1U);
    return 0;
}

static void signal_handler(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static void persist_settings(const char *path, const JigglerSettings *settings) {
    char error[128];
    if (settings_save(path, settings, error, sizeof(error)) != 0) {
        fprintf(stderr, "settings save failed: %s\n", error);
    }
}

static void dispatch_action(UiModel *model, UiAction action, unsigned int held_ms,
                            AppRuntime *runtime, const AppRuntimeSnapshot *snapshot,
                            const char *settings_path) {
    unsigned int effects = ui_model_handle(model, action, snapshot->state, held_ms);

    if (effects & UI_EFFECT_SETTINGS_CHANGED) {
        app_runtime_set_settings(runtime, &model->settings);
        persist_settings(settings_path, &model->settings);
    }
    if (effects & UI_EFFECT_START) app_runtime_start(runtime);
    if (effects & UI_EFFECT_STOP) app_runtime_stop(runtime);
    if (effects & UI_EFFECT_RETRY) app_runtime_prepare(runtime);
    if (effects & UI_EFFECT_EXIT) {
        model->view = UI_VIEW_MAIN;
        app_runtime_request_shutdown(runtime);
    }
}

static void apply_input_event(const SDL_Event *event, UiInputTracker *tracker,
                              UiModel *model, AppRuntime *runtime,
                              const AppRuntimeSnapshot *snapshot, const char *settings_path,
                              uint32_t ticks) {
    UiAction action = ui_input_tracker_event(tracker, event, ticks);
    if (action == UI_ACTION_NONE) return;
    dispatch_action(model, action, 0, runtime, snapshot, settings_path);
}

static int save_capture(SDL_Renderer *renderer, const char *path) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    int result;

    if (surface == NULL) return -1;
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             surface->pixels, surface->pitch) != 0) {
        SDL_FreeSurface(surface);
        return -1;
    }
    result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    return result;
}

static int render_screen_captures(const char *directory) {
    static const struct {
        const char *name;
        UiView view;
        AppRuntimeState state;
    } cases[] = {
        {"preparing", UI_VIEW_MAIN, RUNTIME_PREPARING},
        {"ready", UI_VIEW_MAIN, RUNTIME_READY},
        {"active", UI_VIEW_MAIN, RUNTIME_ACTIVE},
        {"settings", UI_VIEW_SETTINGS, RUNTIME_READY},
        {"help", UI_VIEW_HELP, RUNTIME_READY},
        {"disconnected", UI_VIEW_MAIN, RUNTIME_DISCONNECTED},
        {"error", UI_VIEW_MAIN, RUNTIME_ERROR},
        {"cleanup", UI_VIEW_MAIN, RUNTIME_EXITING},
        {"exit", UI_VIEW_EXIT_CONFIRM, RUNTIME_READY}
    };
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    JigglerSettings settings = {.interval_sec = 30, .move_px = 1200, .pattern = JIGGLE_SQUARE};
    UiModel model;
    AppRuntimeSnapshot snapshot;
    int result = -1;

    if (mkdir(directory, 0755) != 0 && errno != EEXIST) return -1;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    window = SDL_CreateWindow("USB Jiggler Capture", 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_HIDDEN);
    if (window == NULL) goto finish;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL || SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT) != 0) goto finish;

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char path[PATH_MAX];
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.state = cases[index].state;
        snapshot.settings = settings;
        snapshot.report_count = 42;
        snapshot.countdown_ms = 12400;
        snapshot.gesture_active = cases[index].state == RUNTIME_ACTIVE;
        snapshot.gesture_elapsed_ms = 750;
        snapshot.gesture_target_x = 1200;
        snapshot.gesture_target_y = -700;
        snprintf(snapshot.error, sizeof(snapshot.error), "%s",
                 cases[index].state == RUNTIME_DISCONNECTED
                     ? "HID REPORT PATH IS UNAVAILABLE"
                     : "VALIDATED SETUP HELPER REPORTED AN ERROR");
        ui_model_init(&model, &settings);
        model.view = cases[index].view;
        model.selected_setting = 1;
        render_application(renderer, &model, &snapshot, 4242U, cases[index].state == RUNTIME_ACTIVE ? 4470U : 0U);
        SDL_RenderPresent(renderer);
        if (snprintf(path, sizeof(path), "%s/%s.bmp", directory, cases[index].name) >= (int)sizeof(path) ||
            save_capture(renderer, path) != 0) {
            goto finish;
        }
    }
    result = 0;

finish:
    if (renderer != NULL) SDL_DestroyRenderer(renderer);
    if (window != NULL) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

int main(int argc, char **argv) {
    char base_directory[PATH_MAX];
    char setup_path[PATH_MAX];
    char cleanup_path[PATH_MAX];
    char settings_path[PATH_MAX];
    char settings_error[128];
    JigglerSettings settings;
    AppRuntimeConfig runtime_config;
    AppRuntime *runtime = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Joystick *joystick = NULL;
    UiModel model;
    UiInputTracker input_tracker;
    uint64_t previous_report_count = 0;
    uint32_t pulse_until = 0;
    uint32_t shutdown_screen_started_at = 0;
    int shutdown_screen_visible = 0;
    int window_width = SCREEN_WIDTH;
    int window_height = SCREEN_HEIGHT;
    uint32_t window_flags = SDL_WINDOW_SHOWN;
    int running = 1;
    int result = 1;

    if (argc == 3 && strcmp(argv[1], "--render-screens") == 0) {
        return render_screen_captures(argv[2]) == 0 ? 0 : 1;
    }
    if (application_directory(argv[0], base_directory, sizeof(base_directory)) != 0 ||
        append_path(setup_path, sizeof(setup_path), base_directory, "scripts/gadget_setup.sh") != 0 ||
        append_path(cleanup_path, sizeof(cleanup_path), base_directory, "scripts/gadget_cleanup.sh") != 0 ||
        append_path(settings_path, sizeof(settings_path), base_directory, "usbjiggler.cfg") != 0) {
        fprintf(stderr, "cannot resolve application paths\n");
        return 1;
    }
    if (settings_load(settings_path, &settings, settings_error, sizeof(settings_error)) != 0) {
        fprintf(stderr, "using safe settings defaults: %s\n", settings_error);
    }
    ui_model_init(&model, &settings);
    ui_input_tracker_init(&input_tracker);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    if (getenv("USBJIGGLER_WINDOWED") == NULL) {
        SDL_DisplayMode display_mode;
        if (SDL_GetCurrentDisplayMode(0, &display_mode) != 0) goto finish;
        window_width = display_mode.w;
        window_height = display_mode.h;
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }
    window = SDL_CreateWindow("USB Jiggler", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              window_width, window_height, window_flags);
    if (window == NULL) goto finish;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == NULL || SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT) != 0) goto finish;
    if (SDL_NumJoysticks() > 0) joystick = SDL_JoystickOpen(0);

    runtime_config.setup_script = setup_path;
    runtime_config.cleanup_script = cleanup_path;
    runtime_config.hid_device = "/dev/hidg0";
    runtime_config.helper_timeout_ms = 30000;
    runtime_config.allow_regular_hid = 0;
    runtime_config.random_seed = (uint32_t)SDL_GetTicks();
    runtime = app_runtime_create(&runtime_config, &model.settings);
    if (runtime == NULL || app_runtime_prepare(runtime) != 0) goto finish;

    while (running) {
        AppRuntimeSnapshot snapshot;
        SDL_Event event;
        uint32_t ticks = SDL_GetTicks();

        if (stop_requested) {
            model.view = UI_VIEW_MAIN;
            app_runtime_request_shutdown(runtime);
        }
        app_runtime_snapshot(runtime, &snapshot);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                model.view = UI_VIEW_EXIT_CONFIRM;
            } else if (snapshot.state != RUNTIME_EXITING) {
                apply_input_event(&event, &input_tracker, &model, runtime, &snapshot,
                                  settings_path, ticks);
            }
        }
        {
            unsigned int held_ms = 0;
            UiAction repeat_action = ui_input_tracker_repeat(&input_tracker, ticks, &held_ms);
            if (repeat_action != UI_ACTION_NONE) {
                dispatch_action(&model, repeat_action, held_ms,
                                runtime, &snapshot, settings_path);
            }
        }
        app_runtime_snapshot(runtime, &snapshot);
        if (snapshot.state == RUNTIME_EXITING) {
            model.view = UI_VIEW_MAIN;
            if (!shutdown_screen_visible) {
                shutdown_screen_started_at = ticks;
                shutdown_screen_visible = 1;
            }
        }
        if (snapshot.report_count != previous_report_count) {
            previous_report_count = snapshot.report_count;
            pulse_until = ticks + 450U;
        }
        render_application(renderer, &model, &snapshot, ticks, pulse_until);
        SDL_RenderPresent(renderer);
        if (snapshot.shutdown_complete && shutdown_screen_visible) {
            uint32_t minimum = snapshot.shutdown_result == 0 ? 400U : 3000U;
            if (ticks - shutdown_screen_started_at >= minimum) {
                result = snapshot.shutdown_result == 0 ? 0 : 1;
                running = 0;
            }
        }
        SDL_Delay(16);
    }
    persist_settings(settings_path, &model.settings);

finish:
    if (runtime != NULL) {
        if (app_runtime_shutdown(runtime) != 0) result = 1;
        app_runtime_destroy(runtime);
    }
    if (joystick != NULL) SDL_JoystickClose(joystick);
    if (renderer != NULL) SDL_DestroyRenderer(renderer);
    if (window != NULL) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
