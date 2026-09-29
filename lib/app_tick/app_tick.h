#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define APP_TICK_MAX_SUBSCRIBERS 4

/* Dang ky task nhan notify bits (eSetBits) tu ISR moi chu ky tick.
 * Goi truoc app_tick_start(). */
esp_err_t app_tick_subscribe(TaskHandle_t task, uint32_t notify_bits);

/* Khoi dong GPTimer phan cung; ISR phat notify cho cac task da dang ky. */
esp_err_t app_tick_start(uint32_t period_ms);
