#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENCODER_EVENT_NONE   = 0,
    ENCODER_EVENT_CW,        /* clockwise turn */
    ENCODER_EVENT_CCW,       /* counter-clockwise turn */
    ENCODER_EVENT_PRESS,     /* button pressed */
} encoder_event_t;

esp_err_t encoder_init(void);

/* Returns the next pending event, or ENCODER_EVENT_NONE if queue is empty. */
encoder_event_t encoder_get_event(void);

#ifdef __cplusplus
}
#endif
