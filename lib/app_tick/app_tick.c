#include "app_tick.h"

#include "driver/gptimer.h"

typedef struct {
    TaskHandle_t task;
    uint32_t bits;
} subscriber_t;

static subscriber_t s_subscribers[APP_TICK_MAX_SUBSCRIBERS];
static size_t s_count;
static gptimer_handle_t s_timer;

static bool tick_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event,
                     void *arg)
{
    BaseType_t woken = pdFALSE;
    for (size_t i = 0; i < s_count; i++) {
        xTaskNotifyFromISR(s_subscribers[i].task, s_subscribers[i].bits, eSetBits, &woken);
    }
    return woken == pdTRUE;
}

esp_err_t app_tick_subscribe(TaskHandle_t task, uint32_t notify_bits)
{
    if (task == NULL || s_timer != NULL || s_count >= APP_TICK_MAX_SUBSCRIBERS) {
        return ESP_ERR_INVALID_STATE;
    }
    s_subscribers[s_count++] = (subscriber_t){.task = task, .bits = notify_bits};
    return ESP_OK;
}

esp_err_t app_tick_start(uint32_t period_ms)
{
    if (s_timer != NULL || period_ms == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000 * 1000,
    };
    esp_err_t err = gptimer_new_timer(&config, &s_timer);
    if (err != ESP_OK) {
        return err;
    }
    const gptimer_event_callbacks_t callbacks = {.on_alarm = tick_isr};
    const gptimer_alarm_config_t alarm = {
        .alarm_count = (uint64_t)period_ms * 1000,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    if ((err = gptimer_register_event_callbacks(s_timer, &callbacks, NULL)) != ESP_OK ||
        (err = gptimer_set_alarm_action(s_timer, &alarm)) != ESP_OK ||
        (err = gptimer_enable(s_timer)) != ESP_OK ||
        (err = gptimer_start(s_timer)) != ESP_OK) {
        return err;
    }
    return ESP_OK;
}
