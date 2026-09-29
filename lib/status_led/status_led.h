#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

typedef enum {
    STATUS_LED_DISCONNECTED,   /* 0,5 s sang - 1 s tat */
    STATUS_LED_CONNECTED,      /* 1 s sang - 2 s tat */
} status_led_mode_t;

esp_err_t status_led_init(gpio_num_t gpio);
/* An toan khi goi tu event handler; chi khoi dong lai chu ky khi doi che do. */
void status_led_set_mode(status_led_mode_t mode);
