#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DATA_POOL_CAPACITY 32

/* Co danh dau truong nao trong mau hop le. */
#define SAMPLE_VALID_BMP180 (1u << 0)
#define SAMPLE_VALID_DHT11  (1u << 1)

/* Mot object du lieu: ket qua cua ca hai cam bien trong cung mot chu ky. */
typedef struct {
    uint32_t seq;               /* tang dan, dung de xac nhan da gui */
    int64_t uptime_us;          /* thoi diem lay mau theo esp_timer */
    uint8_t valid;              /* SAMPLE_VALID_* */
    float bmp_temperature_c;
    float pressure_hpa;
    float humidity_percent;
    float dht_temperature_c;
} sensor_sample_t;

typedef struct {
    size_t count;
    size_t capacity;
    uint32_t pushed;
    uint32_t dropped;           /* so object cu bi bo do pool day */
    uint32_t sent;
} data_pool_stats_t;

esp_err_t data_pool_init(void);

/* Gan seq roi xep vao cuoi hang. Pool day thi bo object cu nhat.
 * Tra ve true neu co object bi bo. */
bool data_pool_push(sensor_sample_t *sample);

/* Sao chep toi da max object cu nhat, khong lay ra khoi pool. */
size_t data_pool_peek(sensor_sample_t *out, size_t max);

/* Xoa moi object co seq <= last_seq sau khi gui thanh cong. */
void data_pool_release(uint32_t last_seq);

void data_pool_get_stats(data_pool_stats_t *out);
