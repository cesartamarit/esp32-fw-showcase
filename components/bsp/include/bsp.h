#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize all board peripherals: GPIO, I2C bus, LED, button.
 * Must be called once before any other bsp_* function.
 */
esp_err_t bsp_init(void);

/* --- LED --- */

esp_err_t bsp_led_set(bool on);
esp_err_t bsp_led_toggle(void);

/* --- Button --- */

/** Returns true if the boot button is currently pressed. */
bool bsp_button_is_pressed(void);

/* --- I2C --- */

/**
 * Returns the shared I2C master bus handle.
 * Drivers (sensors, NFC, haptic...) call this instead of initializing
 * their own bus — there is only one bus on this board.
 */
i2c_master_bus_handle_t bsp_i2c_get_bus(void);

#ifdef __cplusplus
}
#endif
