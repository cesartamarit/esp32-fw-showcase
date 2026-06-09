#include "wifi_mgr.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

#define CONN_BIT  BIT0
#define FAIL_BIT  BIT1
#define MAX_RETRY 5

static bool s_inited = false;
static int  s_retry  = 0;

static void event_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    EventGroupHandle_t eg = (EventGroupHandle_t)arg;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "disconnected reason=%d rssi=%d", d->reason, d->rssi);
            if (s_retry < MAX_RETRY) {
                esp_wifi_connect();
                s_retry++;
                ESP_LOGW(TAG, "retrying... (%d/%d)", s_retry, MAX_RETRY);
            } else {
                xEventGroupSetBits(eg, FAIL_BIT);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        xEventGroupSetBits(eg, CONN_BIT);
    }
}

esp_err_t wifi_mgr_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) return ret;

    s_inited = true;
    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(uint32_t timeout_ms)
{
    if (!s_inited) {
        esp_err_t ret = wifi_mgr_init();
        if (ret != ESP_OK) return ret;
    }

    /* Already connected? */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGI(TAG, "already connected to '%s'", ap.ssid);
        return ESP_OK;
    }

    EventGroupHandle_t eg = xEventGroupCreate();
    s_retry = 0;

    esp_event_handler_instance_t h_wifi, h_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        event_cb, eg, &h_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        event_cb, eg, &h_ip);

    wifi_config_t wcfg = {};
    strncpy((char *)wcfg.sta.ssid,     CONFIG_WIFI_SSID,     sizeof(wcfg.sta.ssid)     - 1);
    strncpy((char *)wcfg.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode =
        (strlen(CONFIG_WIFI_PASSWORD) > 0) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    esp_wifi_start();

    EventBits_t bits = xEventGroupWaitBits(eg, CONN_BIT | FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));

    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h_wifi);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h_ip);
    vEventGroupDelete(eg);

    if (bits & CONN_BIT) {
        ESP_LOGI(TAG, "connected to '%s'", CONFIG_WIFI_SSID);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "failed to connect to '%s'", CONFIG_WIFI_SSID);
    return ESP_FAIL;
}
