#ifndef USBJIGGLER_APP_RUNTIME_H
#define USBJIGGLER_APP_RUNTIME_H

#include "settings.h"

#include <stddef.h>
#include <stdint.h>

enum {
    APP_RUNTIME_REPORT_FRAME_MS = 16,
    APP_RUNTIME_SEGMENT_FRAMES = 30
};

typedef enum {
    RUNTIME_IDLE = 0,
    RUNTIME_PREPARING,
    RUNTIME_READY,
    RUNTIME_ACTIVE,
    RUNTIME_DISCONNECTED,
    RUNTIME_ERROR,
    RUNTIME_RETRYING,
    RUNTIME_EXITING
} AppRuntimeState;

typedef struct {
    const char *setup_script;
    const char *cleanup_script;
    const char *hid_device;
    unsigned int helper_timeout_ms;
    int allow_regular_hid;
    uint32_t random_seed;
} AppRuntimeConfig;

typedef struct {
    AppRuntimeState state;
    JigglerSettings settings;
    uint64_t report_count;
    uint64_t last_report_ms;
    uint64_t next_report_ms;
    uint64_t countdown_ms;
    unsigned int cleanup_count;
    int setup_owned;
    int gesture_active;
    uint64_t gesture_elapsed_ms;
    int gesture_target_x;
    int gesture_target_y;
    int shutdown_complete;
    int shutdown_result;
    char error[192];
} AppRuntimeSnapshot;

typedef struct AppRuntime AppRuntime;

AppRuntime *app_runtime_create(const AppRuntimeConfig *config, const JigglerSettings *settings);
int app_runtime_prepare(AppRuntime *runtime);
int app_runtime_start(AppRuntime *runtime);
int app_runtime_stop(AppRuntime *runtime);
int app_runtime_set_settings(AppRuntime *runtime, const JigglerSettings *settings);
void app_runtime_snapshot(AppRuntime *runtime, AppRuntimeSnapshot *snapshot);
int app_runtime_wait_for_state(AppRuntime *runtime, AppRuntimeState state, unsigned int timeout_ms);
int app_runtime_request_shutdown(AppRuntime *runtime);
int app_runtime_shutdown(AppRuntime *runtime);
void app_runtime_destroy(AppRuntime *runtime);
const char *app_runtime_state_name(AppRuntimeState state);
size_t app_runtime_build_reports(const JigglerSettings *settings, uint32_t *random_state,
                                 uint8_t reports[4][4]);
int app_runtime_interpolate_step(int total_x, int total_y, unsigned int frame, uint8_t report[4]);

#endif
