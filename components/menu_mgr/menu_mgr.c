#include "menu_mgr.h"
#include "display.h"
#include "bsp.h"
#include "power_mgr.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "menu_mgr";

/* --- BLE state (updated by ble_stack via public API) --- */

static bool s_ble_connected  = false;
static char s_ble_last_cmd[16] = "";

/* --- Menu definition --- */

typedef void (*menu_action_t)(void);

typedef struct {
    const char   *label;
    menu_action_t action;
} menu_item_t;

static void action_led_toggle(void) { bsp_led_toggle(); }
static void action_sleep(void)      { power_mgr_request_sleep(); }

static const menu_item_t s_items[] = {
    { "LED toggle",  action_led_toggle },
    { "FW info",     NULL              },
    { "Deep sleep",  action_sleep      },
};
#define ITEM_COUNT  (sizeof(s_items) / sizeof(s_items[0]))

#define VISIBLE_ROWS  4
#define HEADER_ROW    0
#define ITEM_ROW_BASE 2

static int  s_cursor     = 0;
static int  s_top        = 0;
static bool s_in_submenu = false;

/* --- Status line --- */

static void render_status_line(void)
{
    char line[22];
    if (s_ble_last_cmd[0]) {
        snprintf(line, sizeof(line), "%s|%.13s",
                 s_ble_connected ? "BLE:on " : "BLE:off",
                 s_ble_last_cmd);
    } else {
        snprintf(line, sizeof(line), "BLE: %s",
                 s_ble_connected ? "connected  " : "advertising");
    }
    display_print(7, line);
}

/* --- Rendering --- */

static void render_menu(void)
{
    char line[22];

    display_print(HEADER_ROW, "=== KodeFW menu ===");
    display_print(1, "");

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = s_top + i;
        if (idx >= (int)ITEM_COUNT) {
            display_print(ITEM_ROW_BASE + i, "");
            continue;
        }
        bool sel = (idx == s_cursor);
        snprintf(line, sizeof(line), "%s %-18s", sel ? ">" : " ", s_items[idx].label);
        display_print(ITEM_ROW_BASE + i, line);
    }

    display_print(6, "");
    render_status_line();
    display_flush();
}

static void render_fw_info(void)
{
    char line[22];
    const esp_app_desc_t *d = esp_app_get_description();
    display_print(0, "--- FW info ---");
    display_print(1, "");
    snprintf(line, sizeof(line), "v: %.18s", d->version);
    display_print(2, line);
    snprintf(line, sizeof(line), "%.20s", d->date);
    display_print(3, line);
    snprintf(line, sizeof(line), "%.20s", d->time);
    display_print(4, line);
    display_print(5, "");
    display_print(6, "");
    render_status_line();
    display_print(7, "[press] back    ");  /* override status on info screens */
    display_flush();
}

static void show_submenu(int idx)
{
    s_in_submenu = true;
    if (strcmp(s_items[idx].label, "FW info") == 0) {
        render_fw_info();
    }
}

/* --- Public API --- */

esp_err_t menu_mgr_init(void)
{
    render_menu();
    ESP_LOGI(TAG, "menu ready (%d items)", (int)ITEM_COUNT);
    return ESP_OK;
}

void menu_mgr_handle_event(encoder_event_t evt)
{
    power_mgr_notify_activity();

    if (s_in_submenu) {
        if (evt == ENCODER_EVENT_PRESS) {
            s_in_submenu = false;
            render_menu();
        }
        return;
    }

    switch (evt) {
        case ENCODER_EVENT_CW:
            if (s_cursor < (int)ITEM_COUNT - 1) {
                s_cursor++;
                if (s_cursor >= s_top + VISIBLE_ROWS) s_top++;
            }
            render_menu();
            break;

        case ENCODER_EVENT_CCW:
            if (s_cursor > 0) {
                s_cursor--;
                if (s_cursor < s_top) s_top--;
            }
            render_menu();
            break;

        case ENCODER_EVENT_PRESS:
            if (s_items[s_cursor].action) {
                s_items[s_cursor].action();
                render_menu();
            } else {
                show_submenu(s_cursor);
            }
            break;

        default:
            break;
    }
}

void menu_mgr_tick(void)
{
    /* Refresh only status line to show live BLE state */
    if (!s_in_submenu) {
        render_status_line();
        display_flush();
    }
}

void menu_mgr_set_ble_status(bool connected)
{
    s_ble_connected = connected;
    if (!s_in_submenu) {
        render_status_line();
        display_flush();
    }
}

void menu_mgr_set_ble_last_cmd(const char *cmd)
{
    strncpy(s_ble_last_cmd, cmd, sizeof(s_ble_last_cmd) - 1);
    s_ble_last_cmd[sizeof(s_ble_last_cmd) - 1] = '\0';
    if (!s_in_submenu) {
        render_status_line();
        display_flush();
    }
}

void menu_mgr_show_wakeup_animation(void)
{
    const char *frames[] = {
        "    (o_O)  !!!     ",
        "    (O_o)  ???     ",
        "    (^_^)  YAY!    ",
    };
    for (int f = 0; f < 3; f++) {
        display_print(0, "  System is awake!  ");
        display_print(1, "");
        display_print(2, frames[f]);
        display_print(3, "");
        display_print(4, "    *yawn*           ");
        display_print(5, "");
        display_print(6, "");
        display_print(7, "");
        display_flush();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void menu_mgr_show_sleep_warning(void)
{
    char line[22];

    for (int i = 3; i >= 0; i--) {
        display_print(0, "  Going to sleep!   ");
        display_print(1, "");
        display_print(2, "    (-_-)  zZz      ");
        display_print(3, "");
        if (i > 0) {
            snprintf(line, sizeof(line), "    in %d second%s...  ", i, i == 1 ? "" : "s");
        } else {
            snprintf(line, sizeof(line), "    Sweet dreams!    ");
        }
        display_print(4, line);
        display_print(5, "");
        display_print(6, "  [boot] to wake    ");
        display_print(7, "");
        display_flush();
        if (i > 0) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    display_sleep();  /* blank and power off the panel before deep sleep */
}
