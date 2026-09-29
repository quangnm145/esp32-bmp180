#include "status_led.h"

#include <stdbool.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
} blink_pattern_t;

static const blink_pattern_t PATTERNS[] = {
    [STATUS_LED_DISCONNECTED] = {.on_ms = 500, .off_ms = 1000},
    [STATUS_LED_CONNECTED] = {.on_ms = 1000, .off_ms = 2000},
};

static gpio_num_t s_gpio = GPIO_NUM_NC;
static esp_timer_handle_t s_timer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static status_led_mode_t s_mode = STATUS_LED_DISCONNECTED;
static bool s_led_on;

/* Chay trong task esp_timer: dao trang thai den roi hen gio cho pha tiep theo. */
static void blink_cb(void *arg)
{
    portENTER_CRITICAL(&s_lock);
    s_led_on = !s_led_on;
    const blink_pattern_t *pattern = &PATTERNS[s_mode];
    bool on = s_led_on;
    portEXIT_CRITICAL(&s_lock);

    gpio_set_level(s_gpio, on);
    esp_timer_start_once(s_timer, (uint64_t)(on ? pattern->on_ms : pattern->off_ms) * 1000);
}

esp_err_t status_led_init(gpio_num_t gpio)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_gpio = gpio;
    gpio_reset_pin(gpio);
    esp_err_t err = gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        return err;
    }

    const esp_timer_create_args_t args = {
        .callback = blink_cb,
        .name = "status_led",
    };
    err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        return err;
    }
    s_led_on = false;
    blink_cb(NULL);
    return ESP_OK;
}

void status_led_set_mode(status_led_mode_t mode)
{
    if (s_timer == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    bool changed = s_mode != mode;
    s_mode = mode;
    s_led_on = false;
    portEXIT_CRITICAL(&s_lock);

    /* Chay lai chu ky trong task esp_timer de khong dao den song song. */
    if (changed) {
        esp_timer_stop(s_timer);
        esp_timer_start_once(s_timer, 1);
    }
}
