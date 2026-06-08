#include "encoder.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "encoder";

#define ENC_CLK   CONFIG_ENCODER_CLK_GPIO
#define ENC_DT    CONFIG_ENCODER_DT_GPIO
#define ENC_SW    CONFIG_ENCODER_SW_GPIO

#define DEBOUNCE_TICKS  pdMS_TO_TICKS(8)

static QueueHandle_t            s_evt_queue      = NULL;
static volatile TickType_t      s_last_enc_tick  = 0;
static volatile TickType_t      s_last_sw_tick   = 0;

static void IRAM_ATTR isr_clk(void *arg)
{
    /* Only act on falling edge of CLK */
    if (gpio_get_level(ENC_CLK) != 0) return;

    TickType_t now = xTaskGetTickCountFromISR();
    if ((now - s_last_enc_tick) < DEBOUNCE_TICKS) return;
    s_last_enc_tick = now;

    /* DT sampled at CLK falling edge determines direction */
    encoder_event_t evt = gpio_get_level(ENC_DT) ? ENCODER_EVENT_CCW : ENCODER_EVENT_CW;
    xQueueSendFromISR(s_evt_queue, &evt, NULL);
}

static void IRAM_ATTR isr_sw(void *arg)
{
    /* Only act on falling edge (press) */
    if (gpio_get_level(ENC_SW) != 0) return;

    TickType_t now = xTaskGetTickCountFromISR();
    if ((now - s_last_sw_tick) < DEBOUNCE_TICKS) return;
    s_last_sw_tick = now;

    encoder_event_t evt = ENCODER_EVENT_PRESS;
    xQueueSendFromISR(s_evt_queue, &evt, NULL);
}

esp_err_t encoder_init(void)
{
    s_evt_queue = xQueueCreate(16, sizeof(encoder_event_t));
    if (!s_evt_queue) return ESP_ERR_NO_MEM;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ENC_CLK) | (1ULL << ENC_DT) | (1ULL << ENC_SW),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) return ret;

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENC_CLK, isr_clk, NULL);
    gpio_isr_handler_add(ENC_SW,  isr_sw,  NULL);

    ESP_LOGI(TAG, "encoder ready — CLK:%d DT:%d SW:%d", ENC_CLK, ENC_DT, ENC_SW);
    return ESP_OK;
}

encoder_event_t encoder_get_event(void)
{
    encoder_event_t evt = ENCODER_EVENT_NONE;
    xQueueReceive(s_evt_queue, &evt, 0);
    return evt;
}
