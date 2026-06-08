#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the TCP/IP stack and WiFi driver.
 * Safe to call multiple times — subsequent calls are no-ops.
 */
esp_err_t wifi_mgr_init(void);

/**
 * Connect to the AP configured in Kconfig (WIFI_SSID / WIFI_PASSWORD).
 * Blocking: returns once connected or after timeout_ms milliseconds.
 * If already connected, returns ESP_OK immediately.
 */
esp_err_t wifi_mgr_connect(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
