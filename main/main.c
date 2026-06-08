#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "bsp.h"
#include "ble_stack.h"
#include "ota_manager.h"
#include "power_mgr.h"
#include "display.h"
#include "encoder.h"
#include "menu_mgr.h"
#include "motors.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

static void pre_sleep_handler(void)
{
    bsp_led_set(false);        /* ensure LED is off before sleep (GPIO may float HIGH during domain switch) */
    ble_stack_stop();          /* must stop before deep sleep or BLE timer wakes us immediately */
    motors_pre_sleep();
    menu_mgr_show_sleep_warning();
}

void app_main(void)
{
    bool wakeup = !(esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_UNDEFINED));

    ESP_LOGI(TAG, "esp32-fw-showcase booting...");

    bsp_init();
    wifi_mgr_init();  /* init netif / event loop; does NOT connect yet — only on OTA */
    power_mgr_init();
    ota_manager_init();
    ble_stack_init();
    display_init();
    encoder_init();

    if (wakeup) {
        menu_mgr_show_wakeup_animation();  /* before menu_mgr_init to own the display */
    }

    menu_mgr_init();
    motors_init();  /* no-op when CONFIG_MOTORS_ENABLED=n */

    power_mgr_set_pre_sleep_cb(pre_sleep_handler);

    ESP_LOGI(TAG, "all subsystems up — entering task loop");

    TickType_t last_tick = xTaskGetTickCount();

    while (1) {
        encoder_event_t evt = encoder_get_event();
        if (evt != ENCODER_EVENT_NONE) {
            menu_mgr_handle_event(evt);
        }

        /* Refresh BLE status line every 500 ms */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_tick) >= pdMS_TO_TICKS(500)) {
            last_tick = now;
            menu_mgr_tick();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
