#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t motors_init(void);
void      motors_pre_sleep(void);

/** Returns true if built with CONFIG_MOTORS_ENABLED=y. */
bool motors_enabled(void);

/** Runtime running state (always false when motors_enabled() == false). */
bool motors_servo_running(void);
bool motors_stepper_running(void);

void motors_start_servo(void);
void motors_stop_servo(void);
void motors_start_stepper(void);
void motors_stop_stepper(void);

#ifdef __cplusplus
}
#endif
