#include "ble_stack.h"

#include "bsp.h"
#include "power_mgr.h"
#include "ota_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "sdkconfig.h"
#include "esp_app_desc.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ble_stack";

/* --- UUIDs --- */

/* Service: 4fafc201-1fb5-459e-8fcc-c5c9c331914b */
static const ble_uuid128_t KODE_SVC_UUID =
    BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
                     0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);

/* CMD characteristic: beb5483e-36e1-4688-b7f5-ea07361b26a8 */
static const ble_uuid128_t CMD_CHR_UUID =
    BLE_UUID128_INIT(0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
                     0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

/* STATUS characteristic: cba1d466-344c-4be3-ab3f-189f80dd7518 */
static const ble_uuid128_t STATUS_CHR_UUID =
    BLE_UUID128_INIT(0x18, 0x75, 0xdd, 0x80, 0x9f, 0x18, 0x3f, 0xab,
                     0xe3, 0x4b, 0x4c, 0x34, 0x66, 0xd4, 0xa1, 0xcb);

/* --- State --- */

static uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_handle = 0;

/* --- Command byte definitions --- */
#define CMD_LED_ON      0x01
#define CMD_LED_OFF     0x02
#define CMD_OTA_UPDATE  0x03
#define CMD_DEEP_SLEEP  0x04

/* --- GATT handlers --- */

static int cmd_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    if (OS_MBUF_PKTLEN(ctxt->om) < 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint8_t cmd;
    os_mbuf_copydata(ctxt->om, 0, 1, &cmd);
    power_mgr_notify_activity();

    switch (cmd) {
        case CMD_LED_ON:
            ESP_LOGI(TAG, "CMD: LED ON");
            bsp_led_set(true);
            break;
        case CMD_LED_OFF:
            ESP_LOGI(TAG, "CMD: LED OFF");
            bsp_led_set(false);
            break;
        case CMD_OTA_UPDATE:
            ESP_LOGI(TAG, "CMD: OTA update triggered via BLE");
            ota_manager_start_update(NULL);
            break;
        case CMD_DEEP_SLEEP:
            ESP_LOGW(TAG, "CMD: entering deep sleep via BLE request");
            power_mgr_enter_deep_sleep(0);
            break;
        default:
            ESP_LOGW(TAG, "CMD: unknown command 0x%02x", cmd);
            return BLE_ATT_ERR_UNLIKELY;
    }

    ble_stack_notify_status();
    return 0;
}

static int status_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"fw\":\"%s\","
             "\"uptime\":%lu,"
             "\"ota\":%d}",
             esp_app_get_description()->version,
             (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ),
             (int)ota_manager_get_state());

    int rc = os_mbuf_append(ctxt->om, buf, strlen(buf));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* --- GATT service table --- */

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &KODE_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* CMD: write-only */
                .uuid       = &CMD_CHR_UUID.u,
                .access_cb  = cmd_chr_access,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                /* STATUS: read + notify */
                .uuid       = &STATUS_CHR_UUID.u,
                .access_cb  = status_chr_access,
                .val_handle = &s_status_handle,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 } /* terminator */
        },
    },
    { 0 } /* terminator */
};

/* --- Forward declarations --- */

static int gap_event_cb(struct ble_gap_event *event, void *arg);

/* --- Advertising --- */

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,  /* undirected connectable */
        .disc_mode = BLE_GAP_DISC_MODE_GEN,  /* general discoverable */
    };

    struct ble_hs_adv_fields fields = {
        .flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .tx_pwr_lvl_is_present = 1,
        .tx_pwr_lvl            = BLE_HS_ADV_TX_PWR_LVL_AUTO,
        .name                  = (uint8_t *)CONFIG_BLE_DEVICE_NAME,
        .name_len              = strlen(CONFIG_BLE_DEVICE_NAME),
        .name_is_complete      = 1,
    };

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields error: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv start error: %d", rc);
    } else {
        ESP_LOGI(TAG, "advertising as \"%s\"", CONFIG_BLE_DEVICE_NAME);
    }
}

/* --- GAP event handler --- */

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "central connected (handle=%d)", s_conn_handle);
                power_mgr_set_mode(POWER_MODE_ACTIVE);
            } else {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ESP_LOGW(TAG, "connection failed (status=%d)", event->connect.status);
                start_advertising();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "central disconnected (reason=%d)", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            power_mgr_set_mode(POWER_MODE_BALANCED);
            start_advertising();
            break;

        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "STATUS notifications %s",
                     event->subscribe.cur_notify ? "enabled" : "disabled");
            break;

        default:
            break;
    }
    return 0;
}

/* --- NimBLE host callbacks --- */

static void on_ble_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    start_advertising();
}

static void on_ble_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset (reason=%d)", reason);
}

static void nimble_host_task(void *arg)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* --- Public API --- */

esp_err_t ble_stack_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb  = on_ble_sync;
    ble_hs_cfg.reset_cb = on_ble_reset;

    ble_hs_cfg.store_read_cb   = ble_store_config_read;
    ble_hs_cfg.store_write_cb  = ble_store_config_write;
    ble_hs_cfg.store_delete_cb = ble_store_config_delete;
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(s_gatt_svcs);
    assert(rc == 0);

    ble_svc_gap_device_name_set(CONFIG_BLE_DEVICE_NAME);

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "NimBLE initialized — device name: \"%s\"", CONFIG_BLE_DEVICE_NAME);
    return ESP_OK;
}

void ble_stack_notify_status(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_status_handle == 0) {
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"fw\":\"%s\","
             "\"uptime\":%lu,"
             "\"ota\":%d}",
             esp_app_get_description()->version,
             (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ),
             (int)ota_manager_get_state());

    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));
    if (om) {
        ble_gatts_notify_custom(s_conn_handle, s_status_handle, om);
    }
}

bool ble_stack_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}
