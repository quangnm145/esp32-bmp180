#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Wait after each HTTP attempt; queue updates never trigger an early send. */
#define THINGSPEAK_SEND_INTERVAL_MS 30000

typedef struct {
    float temperature_c;
    float pressure_hpa;
    float humidity_percent;
} thingspeak_sample_t;

/* pvParameters must be a QueueHandle_t containing thingspeak_sample_t items.
 * Start after Wi-Fi has an IP address. Use a one-item queue and
 * xQueueOverwrite() to supply the latest complete, valid sensor reading.
 * The caller owns the queue and must keep it alive for the task lifetime.
 */
void send_data_to_thingspeak(void *pvParameters);
