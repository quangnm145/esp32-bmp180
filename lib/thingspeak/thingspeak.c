#include "thingspeak.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "THINGSPEAK";
static const char *WRITE_API_KEY = "H7U2JHZW6FSZC4LM";

typedef struct {
    char body[32];
    size_t length;
    bool overflow;
} thingspeak_response_t;

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    thingspeak_response_t *response = event->user_data;
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

void send_data_to_thingspeak(void *pvParameters)
{
    QueueHandle_t samples = (QueueHandle_t)pvParameters;
    thingspeak_response_t response = {0};
    esp_http_client_config_t config = {
        .url = "https://api.thingspeak.com/update",
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .event_handler = on_http_event,
        .user_data = &response,
        .disable_auto_redirect = true,
    };

    if (samples == NULL) {
        ESP_LOGE(TAG, "Sensor queue is required");
        vTaskDelete(NULL);
        return;
    }
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Cannot initialize HTTP client");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        thingspeak_sample_t sample;
        if (xQueueReceive(samples, &sample, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!isfinite(sample.temperature_c) || !isfinite(sample.pressure_hpa) ||
            !isfinite(sample.humidity_percent) || sample.pressure_hpa <= 0 ||
            sample.humidity_percent < 0 || sample.humidity_percent > 100) {
            ESP_LOGW(TAG, "Skipping invalid sensor reading");
            continue;
        }

        char url[256];
        int length = snprintf(url, sizeof(url),
                              "https://api.thingspeak.com/update?api_key=%s"
                              "&field1=%.2f&field2=%.2f&field3=%.2f",
                              WRITE_API_KEY, sample.temperature_c,
                              sample.pressure_hpa, sample.humidity_percent);
        if (length < 0 || length >= sizeof(url)) {
            ESP_LOGE(TAG, "Request URL is too long");
            continue;
        }

        memset(&response, 0, sizeof(response));
        esp_err_t err = esp_http_client_set_url(client, url);
        if (err == ESP_OK) {
            err = esp_http_client_perform(client);
        }
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            char *end;
            long long entry_id = strtoll(response.body, &end, 10);
            bool accepted = !response.overflow && end != response.body &&
                            *end == '\0' && entry_id > 0;
            if (status == 200 && accepted) {
                ESP_LOGI(TAG, "Data saved, entry ID: %lld", entry_id);
            } else {
                ESP_LOGW(TAG, "Data rejected (HTTP %d)", status);
            }
        } else {
            ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(err));
        }
        /* Retry with the next latest sample, including after network errors. */
        vTaskDelay(pdMS_TO_TICKS(THINGSPEAK_SEND_INTERVAL_MS));
    }
}
