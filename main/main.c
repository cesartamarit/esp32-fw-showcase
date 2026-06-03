#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "bsp.h"
#include "ble_stack.h"
#include "ota_manager.h"
#include "power_mgr.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "esp32-fw-showcase booting...");

    bsp_init();
    power_mgr_init();
    ota_manager_init();
    ble_stack_init();

    ESP_LOGI(TAG, "all subsystems up — entering task loop");
}
