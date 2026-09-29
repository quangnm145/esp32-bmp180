#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Bit notify danh thuc worker. */
#define THINGSPEAK_NOTIFY_TICK  (1u << 0)   /* tu ISR app_tick, ~1 s */
#define THINGSPEAK_NOTIFY_DATA  (1u << 1)   /* sensor_service vua day object moi */

/* Gioi han cua tai khoan ThingSpeak mien phi: 1 lan cap nhat moi 15 s. */
#define THINGSPEAK_MIN_INTERVAL_MS 15000

typedef bool (*thingspeak_network_ready_fn)(void);

typedef struct {
    bool time_synced;
    uint32_t requests;
    uint32_t failures;
    int last_http_status;
    uint32_t last_batch_size;
    int64_t last_success_uptime_us;     /* 0: chua gui thanh cong */
    char last_error[48];
} thingspeak_status_t;

/* Tao worker va khoi dong SNTP. Goi sau khi esp_netif da khoi tao.
 * Moi lan thuc day, worker lay cac object cu nhat trong data_pool va gui
 * khi: da bat ThingSpeak, co mang, qua THINGSPEAK_MIN_INTERVAL_MS tu lan truoc.
 * - Co Channel ID va dong ho da dong bo: bulk update toan bo object, moi
 *   object mang created_at dung thoi diem do.
 * - Nguoc lai: gui object moi nhat qua /update, bo cac object cu hon. */
esp_err_t thingspeak_worker_start(thingspeak_network_ready_fn network_ready,
                                  TaskHandle_t *out_task);

void thingspeak_get_status(thingspeak_status_t *out);
