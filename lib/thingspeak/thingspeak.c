#include "thingspeak.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "app_config.h"
#include "cJSON.h"
#include "data_pool.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

static const char *TAG = "THINGSPEAK";

#define TIME_VALID_EPOCH  1700000000     /* truoc moc nay coi nhu chua dong bo SNTP */

typedef enum {
    SRC_BMP_TEMPERATURE,
    SRC_PRESSURE,
    SRC_HUMIDITY,
    SRC_DHT_TEMPERATURE,
    SRC_ALTITUDE,
} field_source_t;

/* Anh xa field ThingSpeak -> du lieu. Tam hardcode, sau nay doc tu cau hinh. */
static const struct {
    const char *field;
    field_source_t source;
} FIELD_MAP[] = {
    {"field1", SRC_BMP_TEMPERATURE},
    {"field2", SRC_PRESSURE},
    {"field3", SRC_HUMIDITY},
    {"field4", SRC_DHT_TEMPERATURE},
    {"field5", SRC_ALTITUDE},
};

typedef struct {
    char body[64];
    size_t length;
    bool overflow;
} http_response_t;

static thingspeak_network_ready_fn s_network_ready;
static thingspeak_status_t s_status;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static sensor_sample_t s_batch[DATA_POOL_CAPACITY];

static bool sample_value(const sensor_sample_t *sample, field_source_t source, float *out)
{
    switch (source) {
    case SRC_BMP_TEMPERATURE:
        *out = sample->bmp_temperature_c;
        return sample->valid & SAMPLE_VALID_BMP180;
    case SRC_PRESSURE:
        *out = sample->pressure_hpa;
        return sample->valid & SAMPLE_VALID_BMP180;
    case SRC_HUMIDITY:
        *out = sample->humidity_percent;
        return sample->valid & SAMPLE_VALID_DHT11;
    case SRC_DHT_TEMPERATURE:
        *out = sample->dht_temperature_c;
        return sample->valid & SAMPLE_VALID_DHT11;
    case SRC_ALTITUDE:
        *out = sample->altitude_m;
        return sample->valid & SAMPLE_VALID_BMP180;
    }
    return false;
}

static void set_result(int http_status, uint32_t batch, const char *error)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.requests++;
    s_status.last_http_status = http_status;
    s_status.last_batch_size = batch;
    if (error == NULL) {
        s_status.last_success_uptime_us = esp_timer_get_time();
        s_status.last_error[0] = '\0';
    } else {
        s_status.failures++;
        strlcpy(s_status.last_error, error, sizeof(s_status.last_error));
    }
    portEXIT_CRITICAL(&s_status_lock);
}

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t available = sizeof(response->body) - 1 - response->length;
        size_t count = (size_t)event->data_len;
        if (count > available) {
            response->overflow = true;
            count = available;
        }
        memcpy(response->body + response->length, event->data, count);
        response->length += count;
        response->body[response->length] = '\0';
    }
    return ESP_OK;
}

static esp_err_t http_request(const char *url, const char *json_body,
                              int *status, http_response_t *response)
{
    memset(response, 0, sizeof(*response));
    esp_http_client_config_t config = {
        .url = url,
        .method = json_body != NULL ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .event_handler = on_http_event,
        .user_data = response,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (json_body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_body, strlen(json_body));
    }
    esp_err_t err = esp_http_client_perform(client);
    *status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err;
}

/* Gui mot object qua /update; ThingSpeak tra ve entry ID > 0 khi nhan. */
static bool send_single(const app_thingspeak_config_t *config, const sensor_sample_t *sample)
{
    char url[256];
    int length = snprintf(url, sizeof(url), "https://api.thingspeak.com/update?api_key=%s",
                          config->write_api_key);
    for (size_t i = 0; i < sizeof(FIELD_MAP) / sizeof(FIELD_MAP[0]); i++) {
        float value;
        if (length > 0 && length < sizeof(url) &&
            sample_value(sample, FIELD_MAP[i].source, &value)) {
            length += snprintf(url + length, sizeof(url) - length, "&%s=%.2f",
                               FIELD_MAP[i].field, value);
        }
    }
    if (length <= 0 || length >= sizeof(url)) {
        set_result(0, 1, "URL qua dai");
        return false;
    }

    http_response_t response;
    int status = 0;
    esp_err_t err = http_request(url, NULL, &status, &response);
    if (err != ESP_OK) {
        set_result(status, 1, esp_err_to_name(err));
        return false;
    }
    char *end;
    long long entry_id = strtoll(response.body, &end, 10);
    if (status != 200 || response.overflow || end == response.body || entry_id <= 0) {
        set_result(status, 1, "ThingSpeak tu choi du lieu");
        return false;
    }
    ESP_LOGI(TAG, "Da gui object #%" PRIu32 ", entry ID %lld", sample->seq, entry_id);
    set_result(status, 1, NULL);
    return true;
}

static char *build_bulk_body(const app_thingspeak_config_t *config,
                             const sensor_sample_t *batch, size_t count,
                             time_t now_epoch, int64_t now_uptime_us)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *updates = cJSON_AddArrayToObject(root, "updates");
    cJSON_AddStringToObject(root, "write_api_key", config->write_api_key);

    for (size_t i = 0; i < count; i++) {
        /* Quy doi thoi diem lay mau (uptime) sang gio UTC thuc. */
        time_t taken = now_epoch - (time_t)((now_uptime_us - batch[i].uptime_us) / 1000000);
        struct tm tm_utc;
        gmtime_r(&taken, &tm_utc);
        char created_at[32];
        strftime(created_at, sizeof(created_at), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

        cJSON *update = cJSON_CreateObject();
        cJSON_AddStringToObject(update, "created_at", created_at);
        for (size_t f = 0; f < sizeof(FIELD_MAP) / sizeof(FIELD_MAP[0]); f++) {
            float value;
            if (sample_value(&batch[i], FIELD_MAP[f].source, &value)) {
                char text[16];
                snprintf(text, sizeof(text), "%.2f", value);
                cJSON_AddStringToObject(update, FIELD_MAP[f].field, text);
            }
        }
        cJSON_AddItemToArray(updates, update);
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* Gui toan bo object dang cho trong mot request bulk_update. */
static bool send_bulk(const app_thingspeak_config_t *config, const sensor_sample_t *batch,
                      size_t count, time_t now_epoch)
{
    char *body = build_bulk_body(config, batch, count, now_epoch, esp_timer_get_time());
    if (body == NULL) {
        set_result(0, count, "Het bo nho khi tao JSON");
        return false;
    }
    char url[96];
    snprintf(url, sizeof(url), "https://api.thingspeak.com/channels/%" PRIu32 "/bulk_update.json",
             config->channel_id);

    http_response_t response;
    int status = 0;
    esp_err_t err = http_request(url, body, &status, &response);
    cJSON_free(body);
    if (err != ESP_OK) {
        set_result(status, count, esp_err_to_name(err));
        return false;
    }
    if ((status != 200 && status != 202) || strstr(response.body, "true") == NULL) {
        set_result(status, count, "ThingSpeak tu choi bulk update");
        return false;
    }
    ESP_LOGI(TAG, "Da gui %u object (#%" PRIu32 "..#%" PRIu32 ")",
             (unsigned)count, batch[0].seq, batch[count - 1].seq);
    set_result(status, count, NULL);
    return true;
}

static void worker_task(void *arg)
{
    int64_t last_attempt_us = 0;
    bool attempted = false;

    while (true) {
        xTaskNotifyWait(0, UINT32_MAX, NULL, portMAX_DELAY);

        app_thingspeak_config_t config;
        app_config_get_thingspeak(&config);
        if (!config.enabled || config.write_api_key[0] == '\0' ||
            (s_network_ready != NULL && !s_network_ready())) {
            continue;
        }
        int64_t now_us = esp_timer_get_time();
        if (attempted && now_us - last_attempt_us < (int64_t)THINGSPEAK_MIN_INTERVAL_MS * 1000) {
            continue;
        }
        size_t count = data_pool_peek(s_batch, DATA_POOL_CAPACITY);
        if (count == 0) {
            continue;
        }
        attempted = true;
        last_attempt_us = now_us;

        time_t now_epoch = time(NULL);
        bool synced = now_epoch > TIME_VALID_EPOCH;
        portENTER_CRITICAL(&s_status_lock);
        s_status.time_synced = synced;
        portEXIT_CRITICAL(&s_status_lock);

        if (synced && config.channel_id != 0) {
            if (send_bulk(&config, s_batch, count, now_epoch)) {
                data_pool_release(s_batch[count - 1].seq);
            }
        } else {
            const sensor_sample_t *newest = &s_batch[count - 1];
            if (send_single(&config, newest)) {
                if (count > 1) {
                    ESP_LOGW(TAG, "Chua co Channel ID/gio SNTP: bo %u object cu",
                             (unsigned)(count - 1));
                }
                data_pool_release(newest->seq);
            }
        }
        if (s_status.last_error[0] != '\0') {
            ESP_LOGW(TAG, "Gui that bai (HTTP %d): %s", s_status.last_http_status,
                     s_status.last_error);
        }
    }
}

esp_err_t thingspeak_worker_start(thingspeak_network_ready_fn network_ready,
                                  TaskHandle_t *out_task)
{
    s_network_ready = network_ready;

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&sntp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Khong khoi dong duoc SNTP: %s", esp_err_to_name(err));
    }

    BaseType_t ok = xTaskCreatePinnedToCore(worker_task, "thingspeak", 8192, NULL, 5,
                                            out_task, 0);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void thingspeak_get_status(thingspeak_status_t *out)
{
    portENTER_CRITICAL(&s_status_lock);
    *out = s_status;
    out->time_synced = time(NULL) > TIME_VALID_EPOCH;
    portEXIT_CRITICAL(&s_status_lock);
}
