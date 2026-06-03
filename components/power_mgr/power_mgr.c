#include "power_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

static const char *TAG = "power_mgr";

static TimerHandle_t s_sleep_timer = NULL;

/* --- Internal --- */

static void on_sleep_timeout(TimerHandle_t timer)
{
    ESP_LOGW(TAG, "inactivity timeout — entering deep sleep");
    power_mgr_enter_deep_sleep(0);
}

static esp_err_t apply_pm_config(power_mode_t mode)
{
    esp_pm_config_t cfg = {
        .max_freq_mhz = CONFIG_POWER_MGR_MAX_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_POWER_MGR_MIN_CPU_FREQ_MHZ,
        .light_sleep_enable = false,
    };

    switch (mode) {
        case POWER_MODE_ACTIVE:
            cfg.min_freq_mhz = cfg.max_freq_mhz;
            break;
        case POWER_MODE_BALANCED:
            cfg.light_sleep_enable = true;
            break;
        case POWER_MODE_LOW_POWER:
            cfg.max_freq_mhz = CONFIG_POWER_MGR_MIN_CPU_FREQ_MHZ;
            cfg.light_sleep_enable = true;
            break;
    }

    return esp_pm_configure(&cfg);
}

/* --- Public API --- */

esp_err_t power_mgr_init(void)
{
    ESP_LOGI(TAG, "initializing (max %d MHz / min %d MHz)",
             CONFIG_POWER_MGR_MAX_CPU_FREQ_MHZ,
             CONFIG_POWER_MGR_MIN_CPU_FREQ_MHZ);

    esp_err_t ret = apply_pm_config(POWER_MODE_BALANCED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pm config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (CONFIG_POWER_MGR_DEEP_SLEEP_TIMEOUT_S > 0) {
        s_sleep_timer = xTimerCreate(
            "sleep_watchdog",
            pdMS_TO_TICKS(CONFIG_POWER_MGR_DEEP_SLEEP_TIMEOUT_S * 1000),
            pdFALSE,  /* one-shot */
            NULL,
            on_sleep_timeout
        );
        if (!s_sleep_timer) {
            ESP_LOGE(TAG, "failed to create sleep timer");
            return ESP_ERR_NO_MEM;
        }
        xTimerStart(s_sleep_timer, 0);
        ESP_LOGI(TAG, "deep sleep watchdog armed (%ds)", CONFIG_POWER_MGR_DEEP_SLEEP_TIMEOUT_S);
    }

    return ESP_OK;
}

esp_err_t power_mgr_set_mode(power_mode_t mode)
{
    const char *names[] = {"ACTIVE", "BALANCED", "LOW_POWER"};
    ESP_LOGI(TAG, "switching to %s", names[mode]);
    return apply_pm_config(mode);
}

void power_mgr_notify_activity(void)
{
    if (s_sleep_timer) {
        xTimerReset(s_sleep_timer, 0);
    }
}

void power_mgr_enter_deep_sleep(uint32_t wake_after_s)
{
    ESP_LOGW(TAG, "entering deep sleep...");

    /* wake on boot button (GPIO0, active low) */
    esp_sleep_enable_ext0_wakeup(CONFIG_BSP_BUTTON_GPIO, 0);

    if (wake_after_s > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)wake_after_s * 1000000ULL);
        ESP_LOGI(TAG, "wake sources: GPIO%d + timer (%lus)",
                 CONFIG_BSP_BUTTON_GPIO, (unsigned long)wake_after_s);
    } else {
        ESP_LOGI(TAG, "wake source: GPIO%d only", CONFIG_BSP_BUTTON_GPIO);
    }

    esp_deep_sleep_start();
}
