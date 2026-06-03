#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_MODE_ACTIVE,      /* full speed, all peripherals on */
    POWER_MODE_BALANCED,    /* CPU scales down when idle (tickless) */
    POWER_MODE_LOW_POWER,   /* minimum CPU freq, peripherals gated */
} power_mode_t;

/**
 * Initialize power management: configure CPU freq scaling and
 * start the inactivity watchdog for deep sleep.
 */
esp_err_t power_mgr_init(void);

/**
 * Switch between power modes at runtime.
 * BLE and OTA tasks call this to temporarily go ACTIVE during transfers.
 */
esp_err_t power_mgr_set_mode(power_mode_t mode);

/**
 * Signal activity — resets the deep sleep inactivity timer.
 * Call from any task that receives user input or network data.
 */
void power_mgr_notify_activity(void);

/**
 * Enter deep sleep immediately, regardless of inactivity timer.
 * Wake sources: boot button (GPIO) + optional timer.
 *
 * @param wake_after_s  Wake after N seconds (0 = only GPIO wake).
 */
void power_mgr_enter_deep_sleep(uint32_t wake_after_s);

#ifdef __cplusplus
}
#endif
