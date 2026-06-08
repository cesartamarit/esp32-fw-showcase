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
 * Temporarily prevent deep sleep from triggering even if the inactivity
 * timer fires.  Must be paired with power_mgr_unblock_sleep().
 * Use during OTA downloads or any long-running background operation.
 * Internally ref-counted so nested calls are safe.
 */
void power_mgr_block_sleep(void);
void power_mgr_unblock_sleep(void);

/**
 * Enter deep sleep immediately, regardless of inactivity timer.
 * Wake sources: boot button (GPIO) + optional timer.
 *
 * @param wake_after_s  Wake after N seconds (0 = only GPIO wake).
 */
void power_mgr_enter_deep_sleep(uint32_t wake_after_s);

/**
 * Request deep sleep through the normal path: fires the pre-sleep callback
 * (countdown, motor stop, etc.) then enters deep sleep.
 * Safe to call from any task. Use this instead of power_mgr_enter_deep_sleep()
 * when triggered by user action so the callback chain runs.
 */
void power_mgr_request_sleep(void);

/**
 * Register a callback invoked just before deep sleep.
 * The callback runs in the sleep_entry task context and may block
 * (e.g. to display a warning and count down). After it returns,
 * the system enters deep sleep immediately.
 */
typedef void (*power_mgr_pre_sleep_cb_t)(void);
void power_mgr_set_pre_sleep_cb(power_mgr_pre_sleep_cb_t cb);

#ifdef __cplusplus
}
#endif
