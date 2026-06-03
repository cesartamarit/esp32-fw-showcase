#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_REBOOTING,
    OTA_STATE_FAILED,
} ota_state_t;

typedef void (*ota_progress_cb_t)(ota_state_t state, int progress_pct);

/**
 * Initialize OTA manager: marks the current app as valid so the
 * bootloader does not roll back on next boot.
 * Must be called early in app_main after confirming the app works.
 */
esp_err_t ota_manager_init(void);

/**
 * Trigger an OTA update from the URL configured in Kconfig.
 * Runs in a background FreeRTOS task — non-blocking.
 * The optional callback receives progress events.
 *
 * On success:  device reboots into new firmware.
 * On failure:  current firmware keeps running (rollback via bootloader).
 */
esp_err_t ota_manager_start_update(ota_progress_cb_t cb);

/** Returns current OTA state (safe to call from any task). */
ota_state_t ota_manager_get_state(void);

#ifdef __cplusplus
}
#endif
