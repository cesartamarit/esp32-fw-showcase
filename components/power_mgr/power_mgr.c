#include "power_mgr.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "sdkconfig.h"

static const char *TAG = "power_mgr";

static TimerHandle_t              s_sleep_timer       = NULL;
static TaskHandle_t               s_sleep_task_handle = NULL;
static power_mgr_pre_sleep_cb_t   s_pre_sleep_cb      = NULL;
static volatile int               s_sleep_block_count = 0;

/* --- Internal --- */

/* Runs in its own task so esp_deep_sleep_start() doesn't overflow Tmr Svc. */
static void sleep_entry_task(void *arg)
{
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    /* Honor sleep blocks set by OTA or other background operations.
     * Poll every second until all callers have called unblock. */
    while (s_sleep_block_count > 0) {
        ESP_LOGW(TAG, "sleep deferred — block count=%d", s_sleep_block_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG, "inactivity timeout — pre-sleep callback...");
    if (s_pre_sleep_cb) {
        s_pre_sleep_cb();   /* callback owns its own delay/countdown */
    }
    power_mgr_enter_deep_sleep(0);
    vTaskDelete(NULL); /* unreachable */
}

void power_mgr_block_sleep(void)
{
    s_sleep_block_count++;
    ESP_LOGI(TAG, "sleep blocked (count=%d)", s_sleep_block_count);
}

void power_mgr_unblock_sleep(void)
{
    if (s_sleep_block_count > 0) {
        s_sleep_block_count--;
    }
    ESP_LOGI(TAG, "sleep unblocked (count=%d)", s_sleep_block_count);
}

void power_mgr_set_pre_sleep_cb(power_mgr_pre_sleep_cb_t cb)
{
    s_pre_sleep_cb = cb;
}

void power_mgr_request_sleep(void)
{
    if (s_sleep_task_handle) {
        xTaskNotifyGive(s_sleep_task_handle);
    }
}

static void on_sleep_timeout(TimerHandle_t timer)
{
    if (s_sleep_task_handle) {
        xTaskNotifyGive(s_sleep_task_handle);
    }
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
    uint32_t cause_mask = esp_sleep_get_wakeup_causes();
    bool woke_from_sleep = !(cause_mask & BIT(ESP_SLEEP_WAKEUP_UNDEFINED));

    if (woke_from_sleep) {
        const char *why = (cause_mask & BIT(ESP_SLEEP_WAKEUP_EXT1))  ? "GPIO button" :
                          (cause_mask & BIT(ESP_SLEEP_WAKEUP_TIMER)) ? "timer"       : "other";
        ESP_LOGW(TAG, "*** woke from deep sleep (%s) ***", why);
    }

    /* Spurious-wakeup guard.
     * Two known causes of an immediate wakeup:
     *   1. EXT1 glitch: rtc_gpio_init() briefly droops GPIO6 during the
     *      digital→RTC domain switch and the transient latches ext1.
     *   2. BLE hardware RTC timer: the BLE controller has an internal RTC
     *      advertising timer (~100 ms interval) that survives even after
     *      esp_bt_controller_deinit() in some ESP-IDF 5.4 builds.
     * In both cases the button is NOT actually pressed when we arrive here
     * (~150 ms into boot).  Re-enter sleep immediately — nothing has been
     * initialised yet so there is nothing to tear down.
     * GPIO is still in RTC domain with pull-up active (preserved across deep
     * sleep), so rtc_gpio_get_level works without re-init. */
    if (woke_from_sleep) {
        int lvl = (int)rtc_gpio_get_level((gpio_num_t)CONFIG_POWER_MGR_WAKE_GPIO);
        ESP_LOGI(TAG, "wakeup GPIO%d = %d (0 = button held = genuine, 1 = spurious)",
                 CONFIG_POWER_MGR_WAKE_GPIO, lvl);
        if (lvl == 1) {
            /* Button not held → not a user wakeup → go back to sleep.
             * No BLE, no motors, no display yet: clean re-entry. */
            ESP_LOGW(TAG, "spurious wakeup — re-entering deep sleep");
            esp_sleep_enable_ext1_wakeup(1ULL << CONFIG_POWER_MGR_WAKE_GPIO,
                                         ESP_EXT1_WAKEUP_ALL_LOW);
            esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
            esp_deep_sleep_start();  /* does not return */
        }
        /* lvl == 0: button is pressed → genuine user wakeup → fall through */
    }

    ESP_LOGI(TAG, "initializing (max %d MHz / min %d MHz)",
             CONFIG_POWER_MGR_MAX_CPU_FREQ_MHZ,
             CONFIG_POWER_MGR_MIN_CPU_FREQ_MHZ);

    esp_err_t ret = apply_pm_config(POWER_MODE_BALANCED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pm config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (CONFIG_POWER_MGR_DEEP_SLEEP_TIMEOUT_S > 0) {
        /* Low priority so NimBLE host (~prio 20) can preempt the sleep
         * sequence and process ble_gap_adv_stop() without requiring
         * explicit vTaskDelay yields from our side. */
        xTaskCreate(sleep_entry_task, "sleep_entry", 3072, NULL,
                    tskIDLE_PRIORITY + 5, &s_sleep_task_handle);

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

    /* Clear any stale wakeup sources (including residual BLE controller
     * timers not yet removed by nimble_port_deinit). */
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

    gpio_num_t wake_pin = (gpio_num_t)CONFIG_POWER_MGR_WAKE_GPIO;

    /* Switch GPIO to RTC domain so its pull-up survives deep sleep.
     * rtc_gpio_init() briefly disconnects the digital pull-up while the RTC
     * domain takes over — this can leave a short LOW glitch on the line.
     * We therefore arm ext1 AFTER a 200 ms settling window so any transient
     * from the domain switch is gone before the wakeup hardware starts
     * monitoring. Without this, a recently-pressed button causes immediate
     * wakeup even though it has long been released. */
    rtc_gpio_init(wake_pin);
    rtc_gpio_set_direction(wake_pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(wake_pin);
    rtc_gpio_pulldown_dis(wake_pin);

    vTaskDelay(pdMS_TO_TICKS(200));   /* settle before arming ext1 */

    /* Verify the line is HIGH before arming — abort the whole sleep if it is
     * stuck LOW (button held or stuck) to avoid an instant wakeup loop. */
    for (int i = 0; rtc_gpio_get_level(wake_pin) == 0 && i < 100; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    int lvl = (int)rtc_gpio_get_level(wake_pin);
    ESP_LOGI(TAG, "GPIO%d = %d before arming ext1 (need 1)", CONFIG_POWER_MGR_WAKE_GPIO, lvl);
    if (lvl == 0) {
        ESP_LOGE(TAG, "wake pin still LOW — aborting sleep to avoid instant wakeup loop");
        return;
    }

    esp_sleep_enable_ext1_wakeup(1ULL << CONFIG_POWER_MGR_WAKE_GPIO,
                                 ESP_EXT1_WAKEUP_ALL_LOW);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    if (wake_after_s > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)wake_after_s * 1000000ULL);
        ESP_LOGI(TAG, "wake: GPIO%d | timer %lus",
                 CONFIG_POWER_MGR_WAKE_GPIO, (unsigned long)wake_after_s);
    } else {
        ESP_LOGI(TAG, "wake: press encoder SW (GPIO%d)", CONFIG_POWER_MGR_WAKE_GPIO);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGW(TAG, "GPIO%d = %d — sleeping now", CONFIG_POWER_MGR_WAKE_GPIO,
             (int)rtc_gpio_get_level(wake_pin));
    esp_deep_sleep_start();
}
