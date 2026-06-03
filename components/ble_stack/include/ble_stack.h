#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Custom GATT profile — one service, two characteristics:
 *
 *  Service  KODE_SVC   4fafc201-1fb5-459e-8fcc-c5c9c331914b
 *  ├─ CMD   (write)    beb5483e-36e1-4688-b7f5-ea07361b26a8
 *  │    0x01 → LED ON       0x02 → LED OFF
 *  │    0x03 → OTA update   0x04 → Deep sleep
 *  └─ STATUS (read+notify)  cba1d466-344c-4be3-ab3f-189f80dd7518
 *       JSON: {"fw":"1.0.0","uptime":42,"mode":"BALANCED"}
 */

/**
 * Initialize NimBLE host and start advertising.
 * Registers the GATT service and starts the NimBLE FreeRTOS task.
 */
esp_err_t ble_stack_init(void);

/**
 * Send a STATUS notification to the connected central (if any).
 * Call this whenever device state changes.
 */
void ble_stack_notify_status(void);

/** Returns true if a central is currently connected. */
bool ble_stack_is_connected(void);

#ifdef __cplusplus
}
#endif
