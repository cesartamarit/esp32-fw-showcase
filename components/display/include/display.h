#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(void);

/* Clear the framebuffer and push to screen. */
void display_clear(void);

/* Print a line of text (row 0-7 for 8px font). */
void display_print(uint8_t row, const char *text);

/* Force framebuffer flush to the panel. */
void display_flush(void);

/* Blank the framebuffer, flush, then send the SSD1306 display-off command.
   Call before entering deep sleep so the screen goes dark. */
void display_sleep(void);

#ifdef __cplusplus
}
#endif
