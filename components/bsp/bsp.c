#include "bsp.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "bsp";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static bool s_led_state = false;

/* --- Init --- */

static esp_err_t init_led(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_LED_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret == ESP_OK) {
        gpio_set_level(CONFIG_BSP_LED_GPIO, 0);
    }
    return ret;
}

static esp_err_t init_button(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << CONFIG_BSP_BUTTON_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,   /* button pulls to GND when pressed */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static esp_err_t init_i2c(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port      = I2C_NUM_0,
        .sda_io_num    = CONFIG_BSP_I2C_SDA_GPIO,
        .scl_io_num    = CONFIG_BSP_I2C_SCL_GPIO,
        .clk_source    = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

esp_err_t bsp_init(void)
{
    ESP_LOGI(TAG, "initializing board (ESP32-S3)");

    esp_err_t ret;

    ret = init_led();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = init_button();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = init_i2c();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "board ready — LED GPIO%d, BTN GPIO%d, I2C SDA%d/SCL%d @ %dHz",
             CONFIG_BSP_LED_GPIO, CONFIG_BSP_BUTTON_GPIO,
             CONFIG_BSP_I2C_SDA_GPIO, CONFIG_BSP_I2C_SCL_GPIO,
             CONFIG_BSP_I2C_FREQ_HZ);

    return ESP_OK;
}

/* --- LED --- */

esp_err_t bsp_led_set(bool on)
{
    s_led_state = on;
    return gpio_set_level(CONFIG_BSP_LED_GPIO, on ? 1 : 0);
}

esp_err_t bsp_led_toggle(void)
{
    return bsp_led_set(!s_led_state);
}

/* --- Button --- */

bool bsp_button_is_pressed(void)
{
    /* active low: button pulls GPIO to GND when pressed */
    return gpio_get_level(CONFIG_BSP_BUTTON_GPIO) == 0;
}

/* --- I2C --- */

i2c_master_bus_handle_t bsp_i2c_get_bus(void)
{
    return s_i2c_bus;
}
