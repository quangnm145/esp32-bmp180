#include "sensor_history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_manager.h"

#define HISTORY_MAX_BYTES 32768
#define HISTORY_CACHE_US (60LL * 1000000)
#define HISTORY_RETRY_US (15LL * 1000000)
static SemaphoreHandle_t s_lock;
static char *s_cache;
static bool s_loading;
static int64_t s_next_fetch;
static const char *s_error;

typedef struct { char *body; size_t length; bool overflow; } response_t;

static esp_err_t collect_response(esp_http_client_event_t *event)
{
    response_t *r = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t n = (size_t)event->data_len;
        if (n > HISTORY_MAX_BYTES - 1 - r->length) {
            r->overflow = true;
            return ESP_FAIL;
        }
        memcpy(r->body + r->length, event->data, n);
        r->length += n;
        r->body[r->length] = '\0';
    }
    return ESP_OK;
}

static void fetch_history(void *arg)
{
    (void)arg;
    response_t r = {.body = calloc(1, HISTORY_MAX_BYTES)};
    const char *error = "Khong du bo nho de tai lich su";
    bool success = false;
    if (r.body != NULL) {
        char url[256];
        snprintf(url, sizeof(url),
                 "https://api.thingspeak.com/channels/%d/feeds.json?results=50",
                 CONFIG_APP_TS_HISTORY_CHANNEL_ID);
        esp_http_client_config_t config = {
            .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 10000, .event_handler = collect_response,
            .user_data = &r, .disable_auto_redirect = true,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client != NULL) {
            /* Keep the read key out of URLs and browser responses. */
            esp_err_t err = esp_http_client_set_header(client, "THINGSPEAKAPIKEY",
                                                       CONFIG_APP_TS_HISTORY_READ_KEY);
            if (err == ESP_OK) err = esp_http_client_perform(client);
            int status = esp_http_client_get_status_code(client);
            error = "Khong tai duoc lich su ThingSpeak. Kiem tra Internet, channel va Read API Key";
            if (err == ESP_OK && status == 200 && !r.overflow) {
                cJSON *json = cJSON_Parse(r.body);
                success = cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(json, "feeds"));
                cJSON_Delete(json);
                if (!success) error = "Phan hoi ThingSpeak khong hop le";
            }
            if (r.overflow) error = "Lich su vuot gioi han bo dem";
            esp_http_client_cleanup(client);
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (success) {
        free(s_cache);
        s_cache = r.body;
        r.body = NULL;
    }
    s_error = success ? NULL : error;
    s_loading = false;
    s_next_fetch = esp_timer_get_time() + (success ? HISTORY_CACHE_US : HISTORY_RETRY_US);
    xSemaphoreGive(s_lock);
    free(r.body);
    vTaskDelete(NULL);
}

esp_err_t sensor_history_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (CONFIG_APP_TS_HISTORY_CHANNEL_ID == 0 || CONFIG_APP_TS_HISTORY_READ_KEY[0] == '\0') {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"error\":\"Chua cau hinh channel va Read API Key\"}");
    }
    /* Only the HTTP server task creates the mutex. */
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) return httpd_resp_send_500(req);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_loading && esp_timer_get_time() >= s_next_fetch) {
        if (wifi_manager_is_connected()) {
            s_loading = true;
            if (xTaskCreate(fetch_history, "ts_history", 8192, NULL, 3, NULL) != pdPASS) {
                s_loading = false;
                s_error = "Khong tao duoc task tai lich su";
                s_next_fetch = esp_timer_get_time() + HISTORY_RETRY_US;
            }
        } else {
            s_error = "ESP32 chua ket noi Internet qua Wi-Fi";
        }
    }
    bool loading = s_loading;
    const char *error = s_error;
    char *copy = !loading && !error && s_cache != NULL ? strdup(s_cache) : NULL;
    xSemaphoreGive(s_lock);
    if (loading) {
        httpd_resp_set_status(req, "202 Accepted");
        return httpd_resp_sendstr(req, "{\"loading\":true}");
    }
    if (copy != NULL) {
        esp_err_t result = httpd_resp_sendstr(req, copy);
        free(copy);
        return result;
    }
    httpd_resp_set_status(req, "503 Service Unavailable");
    char body[256];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", error ? error : "Khong du bo nho");
    return httpd_resp_sendstr(req, body);
}
