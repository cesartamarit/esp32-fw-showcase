#pragma once

#include "esp_err.h"
#include "encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t menu_mgr_init(void);

void menu_mgr_handle_event(encoder_event_t evt);

/* Called from main loop to refresh the BLE status line. */
void menu_mgr_tick(void);

/* Called by ble_stack to push state changes to the display. */
void menu_mgr_set_ble_status(bool connected);
void menu_mgr_set_ble_last_cmd(const char *cmd);

/* Called from the power_mgr pre-sleep callback. Displays a sleep warning
   with a short countdown, then returns. */
void menu_mgr_show_sleep_warning(void);

/* Called from app_main on deep sleep wakeup to show a brief wake animation. */
void menu_mgr_show_wakeup_animation(void);

#ifdef __cplusplus
}
#endif
