#include "motors.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "motors";

/* ------------------------------------------------------------------ */
/* Servo (SG90 / MG90S) — LEDC PWM 50 Hz                              */
/* Pulse:  0.5 ms = 0°,  1.5 ms = 90°,  2.5 ms = 180°                 */
/* ------------------------------------------------------------------ */

#define SERVO_GPIO       CONFIG_MOTORS_SERVO_GPIO
#define SERVO_HZ         50
#define SERVO_BITS       LEDC_TIMER_14_BIT
#define SERVO_MAX_COUNTS (1 << 14)         /* 16384 */
/* at 50 Hz, 1 count = 20 ms / 16384 ≈ 1.22 µs */
#define SERVO_DUTY_MIN 819   // ~1.0 ms
#define SERVO_DUTY_MAX 1638  // ~2.0 ms

static uint32_t angle_to_duty(int deg)
{
    if (deg < 0)   deg = 0;
    if (deg > 180) deg = 180;
    return (uint32_t)(SERVO_DUTY_MIN + (long)(SERVO_DUTY_MAX - SERVO_DUTY_MIN) * deg / 180);
}

static TaskHandle_t s_servo_task    = NULL;
static TaskHandle_t s_stepper_task  = NULL;
static bool         s_servo_running   = false;
static bool         s_stepper_running = false;

static void servo_task(void *arg)
{
    int deg = 0;
    int dir = 1;   /* +1 = forward, -1 = backward */
    while (1) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, angle_to_duty(deg));
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ESP_LOGD(TAG, "servo %d°", deg);
        deg += dir * 10;
        if (deg >= 180) { deg = 180; dir = -1; }
        else if (deg <= 0) { deg = 0; dir = 1; }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ------------------------------------------------------------------ */
/* Stepper 28BYJ-48 + ULN2003 — half-step, 2048 steps/rev             */
/* ------------------------------------------------------------------ */

#define STEP_IN1  CONFIG_MOTORS_STEPPER_IN1_GPIO
#define STEP_IN2  CONFIG_MOTORS_STEPPER_IN2_GPIO
#define STEP_IN3  CONFIG_MOTORS_STEPPER_IN3_GPIO
#define STEP_IN4  CONFIG_MOTORS_STEPPER_IN4_GPIO

/* Half-step sequence (8 phases): each row = {IN1, IN2, IN3, IN4} */
static const uint8_t STEP_SEQ[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1},
};

static void stepper_task(void *arg)
{
    int phase = 0;
    while (1) {
        gpio_set_level(STEP_IN1, STEP_SEQ[phase][0]);
        gpio_set_level(STEP_IN2, STEP_SEQ[phase][1]);
        gpio_set_level(STEP_IN3, STEP_SEQ[phase][2]);
        gpio_set_level(STEP_IN4, STEP_SEQ[phase][3]);
        phase = (phase + 1) % 8;
        vTaskDelay(pdMS_TO_TICKS(2));   /* ~4 s/revolution */
    }
}

/* ------------------------------------------------------------------ */
/* Public API — capability / state queries                             */
/* ------------------------------------------------------------------ */

bool motors_enabled(void)
{
#if CONFIG_MOTORS_ENABLED
    return true;
#else
    return false;
#endif
}

bool motors_servo_running(void)   { return s_servo_running; }
bool motors_stepper_running(void) { return s_stepper_running; }

void motors_start_servo(void)
{
#if CONFIG_MOTORS_ENABLED
    if (s_servo_task && !s_servo_running) {
        vTaskResume(s_servo_task);
        s_servo_running = true;
        ESP_LOGI(TAG, "servo started");
    }
#endif
}

void motors_stop_servo(void)
{
#if CONFIG_MOTORS_ENABLED
    if (s_servo_task && s_servo_running) {
        vTaskSuspend(s_servo_task);
        s_servo_running = false;
        ESP_LOGI(TAG, "servo stopped");
    }
#endif
}

void motors_start_stepper(void)
{
#if CONFIG_MOTORS_ENABLED
    if (s_stepper_task && !s_stepper_running) {
        vTaskResume(s_stepper_task);
        s_stepper_running = true;
        ESP_LOGI(TAG, "stepper started");
    }
#endif
}

void motors_stop_stepper(void)
{
#if CONFIG_MOTORS_ENABLED
    if (s_stepper_task && s_stepper_running) {
        vTaskSuspend(s_stepper_task);
        s_stepper_running = false;
        ESP_LOGI(TAG, "stepper stopped");
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                      */
/* ------------------------------------------------------------------ */

esp_err_t motors_init(void)
{
#if !CONFIG_MOTORS_ENABLED
    ESP_LOGI(TAG, "motors disabled at compile time");
    return ESP_OK;
#else
    /* --- Servo: configure LEDC --- */
    ledc_timer_config_t timer_cfg = {
        .duty_resolution = SERVO_BITS,
        .freq_hz         = SERVO_HZ,
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .clk_cfg         = LEDC_USE_XTAL_CLK,  /* fixed 40 MHz, unaffected by PM freq scaling */
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t ch_cfg = {
        .channel    = LEDC_CHANNEL_0,
        .duty       = angle_to_duty(0),
        .gpio_num   = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = LEDC_TIMER_0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* --- Stepper: configure GPIOs --- */
    gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << STEP_IN1) | (1ULL << STEP_IN2) |
                        (1ULL << STEP_IN3) | (1ULL << STEP_IN4),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&gpio_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "stepper GPIO config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_log_level_set(TAG, ESP_LOG_DEBUG);  /* enable servo/stepper debug prints */

    /* --- Launch tasks (start suspended — activate via BLE) --- */
    xTaskCreate(servo_task,   "servo",   2048, NULL, 3, &s_servo_task);
    xTaskCreate(stepper_task, "stepper", 2048, NULL, 3, &s_stepper_task);
    vTaskSuspend(s_servo_task);
    vTaskSuspend(s_stepper_task);

    s_servo_running   = false;
    s_stepper_running = false;

    ESP_LOGI(TAG, "servo on GPIO%d  |  stepper IN1-4: %d %d %d %d",
             SERVO_GPIO, STEP_IN1, STEP_IN2, STEP_IN3, STEP_IN4);
    return ESP_OK;
#endif /* CONFIG_MOTORS_ENABLED */
}

void motors_pre_sleep(void)
{
#if !CONFIG_MOTORS_ENABLED
    return;
#else
    if (s_servo_task)   vTaskSuspend(s_servo_task);
    if (s_stepper_task) vTaskSuspend(s_stepper_task);

    s_servo_running   = false;
    s_stepper_running = false;

    /* De-energize all stepper coils to avoid heating the motor */
    gpio_set_level(STEP_IN1, 0);
    gpio_set_level(STEP_IN2, 0);
    gpio_set_level(STEP_IN3, 0);
    gpio_set_level(STEP_IN4, 0);

    ESP_LOGI(TAG, "motors stopped for sleep");
#endif
}
