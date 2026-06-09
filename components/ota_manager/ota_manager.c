#include "ota_manager.h"
#include "wifi_mgr.h"
#include "power_mgr.h"
#include "esp_coexist.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "ota_manager";

static volatile ota_state_t s_state = OTA_STATE_IDLE;
static ota_progress_cb_t    s_progress_cb = NULL;

/* --- Internal --- */

static void set_state(ota_state_t state)
{
    s_state = state;
    if (s_progress_cb) {
        s_progress_cb(state, 0);
    }
}

static void ota_task(void *arg)
{
    ESP_LOGI(TAG, "starting OTA from: %s", CONFIG_OTA_MANAGER_FIRMWARE_URL);
    set_state(OTA_STATE_DOWNLOADING);

    power_mgr_block_sleep();  /* prevent deep sleep while updating */

    /* Give BLE time to send the initial "Downloading" notification, then
     * tell the coexistence arbiter to favour WiFi for association. */
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);

    if (wifi_mgr_connect(30000) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi unavailable — OTA aborted");
        set_state(OTA_STATE_FAILED);
        power_mgr_unblock_sleep();
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t http_cfg = {
        .url               = CONFIG_OTA_MANAGER_FIRMWARE_URL,
        .timeout_ms        = CONFIG_OTA_MANAGER_RECV_TIMEOUT_MS,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* verify HTTPS with Mozilla CA bundle */
        .buffer_size       = 4096,  /* GitHub redirects to CDN with large headers; default 512 overflows */
        .buffer_size_tx    = 1024,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_cfg, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        set_state(OTA_STATE_FAILED);
        power_mgr_unblock_sleep();
        vTaskDelete(NULL);
        return;
    }

    esp_app_desc_t new_app;
    if (esp_https_ota_get_img_desc(handle, &new_app) == ESP_OK) {
        ESP_LOGI(TAG, "new firmware: %s (%s)", new_app.version, new_app.date);
    }

    while (true) {
        ret = esp_https_ota_perform(handle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int image_len   = esp_https_ota_get_image_size(handle);
            int written_len = esp_https_ota_get_image_len_read(handle);
            if (image_len > 0 && s_progress_cb) {
                s_progress_cb(OTA_STATE_DOWNLOADING, (written_len * 100) / image_len);
            }
            continue;
        }
        break;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(handle);
        set_state(OTA_STATE_FAILED);
        power_mgr_unblock_sleep();
        vTaskDelete(NULL);
        return;
    }

    set_state(OTA_STATE_VERIFYING);
    ret = esp_https_ota_finish(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(ret));
        set_state(OTA_STATE_FAILED);
        power_mgr_unblock_sleep();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA successful — rebooting in 3s");
    set_state(OTA_STATE_REBOOTING);
    /* No unblock needed here — device reboots and block_count resets to 0 */
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

/* --- Public API --- */

esp_err_t ota_manager_init(void)
{
    /*
     * Mark the current app as valid. If we don't call this, the bootloader
     * will roll back to the previous firmware on the next reboot.
     * Only call this after the app has proven it works (WiFi up, BLE up...).
     */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t   state;

    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "marking app as valid (rollback window closed)");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    ESP_LOGI(TAG, "OTA manager ready — target: %s", CONFIG_OTA_MANAGER_FIRMWARE_URL);
    return ESP_OK;
}

esp_err_t ota_manager_start_update(ota_progress_cb_t cb)
{
    if (s_state == OTA_STATE_DOWNLOADING || s_state == OTA_STATE_VERIFYING) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    s_progress_cb = cb;

    BaseType_t created = xTaskCreate(
        ota_task, "ota_task",
        CONFIG_OTA_MANAGER_TASK_STACK_SIZE,
        NULL, 5, NULL
    );

    return (created == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

ota_state_t ota_manager_get_state(void)
{
    return s_state;
}
