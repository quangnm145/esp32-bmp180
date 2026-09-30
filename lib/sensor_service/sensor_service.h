#pragma once

#include <stdint.h>

#include "data_pool.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Bit notify tu app_tick cho task cam bien. */
#define SENSOR_NOTIFY_TICK (1u << 0)

typedef struct {
    int bmp180_sda_gpio;
    int bmp180_scl_gpio;
    int dht11_gpio;
    TaskHandle_t consumer_task;     /* nhan notify khi co object moi trong pool */
    uint32_t consumer_notify_bits;
} sensor_service_config_t;

/* Tao task cam bien (core 1). Task doc BMP180 moi tick, DHT11 moi 2 tick,
 * loc tung kenh; dong goi mau dau sau khoang 30 s khoi dong, sau do theo
 * chu ky ThingSpeak tinh tu mau dau.
 * *out_task dung de dang ky app_tick. */
esp_err_t sensor_service_start(const sensor_service_config_t *config, TaskHandle_t *out_task);

/* Object dong goi gan nhat (valid = 0 khi chua co). */
void sensor_service_get_latest(sensor_sample_t *out);
