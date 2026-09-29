#include "data_pool.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Pool cap phat tinh dang vong tron: head la object cu nhat. */
static sensor_sample_t s_items[DATA_POOL_CAPACITY];
static size_t s_head;
static size_t s_count;
static uint32_t s_next_seq = 1;
static data_pool_stats_t s_stats = {.capacity = DATA_POOL_CAPACITY};
static SemaphoreHandle_t s_lock;

esp_err_t data_pool_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    return s_lock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

bool data_pool_push(sensor_sample_t *sample)
{
    bool dropped = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_count == DATA_POOL_CAPACITY) {
        s_head = (s_head + 1) % DATA_POOL_CAPACITY;
        s_count--;
        s_stats.dropped++;
        dropped = true;
    }
    sample->seq = s_next_seq++;
    s_items[(s_head + s_count) % DATA_POOL_CAPACITY] = *sample;
    s_count++;
    s_stats.pushed++;
    xSemaphoreGive(s_lock);
    return dropped;
}

size_t data_pool_peek(sensor_sample_t *out, size_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_count < max ? s_count : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_items[(s_head + i) % DATA_POOL_CAPACITY];
    }
    xSemaphoreGive(s_lock);
    return n;
}

void data_pool_release(uint32_t last_seq)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* Object da gui co the da bi bo trong luc gui; chi xoa phan con lai. */
    while (s_count > 0 && (int32_t)(s_items[s_head].seq - last_seq) <= 0) {
        s_head = (s_head + 1) % DATA_POOL_CAPACITY;
        s_count--;
        s_stats.sent++;
    }
    xSemaphoreGive(s_lock);
}

void data_pool_get_stats(data_pool_stats_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_stats;
    out->count = s_count;
    xSemaphoreGive(s_lock);
}
