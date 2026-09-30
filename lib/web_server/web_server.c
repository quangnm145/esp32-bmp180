#include "web_server.h"
#include "sensor_history.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "data_pool.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mbedtls/base64.h"
#include "sensor_service.h"
#include "thingspeak.h"
#include "wifi_manager.h"

static const char *TAG = "WEB_SERVER";

#define BODY_MAX_LEN        512
#define SCAN_MAX_RECORDS    20
#define AUTH_MAX_LEN        128
#define REBOOT_DELAY_US     (1000 * 1000)

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server;
static esp_timer_handle_t s_reboot_timer;
static char s_expected_auth[AUTH_MAX_LEN];

/* ------------------------------------------------------------------------ */
/* Tien ich                                                                  */
/* ------------------------------------------------------------------------ */

static bool check_auth(httpd_req_t *req)
{
    char value[AUTH_MAX_LEN];
    size_t length = httpd_req_get_hdr_value_len(req, "Authorization");
    if (length > 0 && length < sizeof(value) &&
        httpd_req_get_hdr_value_str(req, "Authorization", value, sizeof(value)) == ESP_OK &&
        strcmp(value, s_expected_auth) == 0) {
        return true;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"IOT_Device\"");
    httpd_resp_sendstr(req, "Can dang nhap");
    return false;
}

static esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    if (status != NULL) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    return send_json(req, status, root);
}

static esp_err_t send_ok(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_json(req, NULL, root);
}

/* Doc body POST vao buffer co gioi han va parse JSON.
 * Tra ve NULL (da gui phan hoi loi) neu that bai. */
static cJSON *read_json_body(httpd_req_t *req)
{
    if (req->content_len > BODY_MAX_LEN) {
        send_error(req, "413 Payload Too Large", "Du lieu qua lon");
        return NULL;
    }

    char buffer[BODY_MAX_LEN + 1];
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buffer + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            send_error(req, "400 Bad Request", "Doc du lieu that bai");
            return NULL;
        }
        received += (size_t)ret;
    }
    buffer[received] = '\0';

    cJSON *root = cJSON_Parse(buffer);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_error(req, "400 Bad Request", "JSON khong hop le");
        return NULL;
    }
    return root;
}

static void reboot_timer_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void schedule_reboot(void)
{
    ESP_LOGW(TAG, "Khoi dong lai sau 1 s");
    esp_timer_stop(s_reboot_timer);
    esp_timer_start_once(s_reboot_timer, REBOOT_DELAY_US);
}

static const char *wifi_state_name(wifi_manager_state_t state)
{
    switch (state) {
    case WIFI_MANAGER_CONNECTING:
        return "connecting";
    case WIFI_MANAGER_CONNECTED:
        return "connected";
    case WIFI_MANAGER_DISCONNECTED:
        return "disconnected";
    case WIFI_MANAGER_NOT_CONFIGURED:
    default:
        return "not_configured";
    }
}

/* ------------------------------------------------------------------------ */
/* Handler                                                                   */
/* ------------------------------------------------------------------------ */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    /* EMBED_TXTFILES them ky tu NUL o cuoi. */
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    int64_t now_us = esp_timer_get_time();
    cJSON *root = cJSON_CreateObject();

    wifi_manager_status_t wifi;
    wifi_manager_get_status(&wifi);
    cJSON *j_wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddStringToObject(j_wifi, "state", wifi_state_name(wifi.state));
    cJSON_AddStringToObject(j_wifi, "ssid", wifi.ssid);
    cJSON_AddStringToObject(j_wifi, "sta_ip", wifi.sta_ip);
    cJSON_AddStringToObject(j_wifi, "ap_ssid", wifi.ap_ssid);
    cJSON_AddStringToObject(j_wifi, "ap_ip", wifi.ap_ip);
    cJSON_AddNumberToObject(j_wifi, "rssi", wifi.rssi);
    cJSON_AddNumberToObject(j_wifi, "last_disconnect_reason", wifi.last_disconnect_reason);

    sensor_sample_t sample;
    sensor_service_get_latest(&sample);
    cJSON *j_sensor = cJSON_AddObjectToObject(root, "sensor");
    bool bmp_ok = (sample.valid & SAMPLE_VALID_BMP180) != 0;
    bool dht_ok = (sample.valid & SAMPLE_VALID_DHT11) != 0;
    cJSON_AddBoolToObject(j_sensor, "bmp180_valid", bmp_ok);
    cJSON_AddBoolToObject(j_sensor, "dht11_valid", dht_ok);
    if (bmp_ok) {
        cJSON_AddNumberToObject(j_sensor, "bmp_temperature_c", sample.bmp_temperature_c);
        cJSON_AddNumberToObject(j_sensor, "pressure_hpa", sample.pressure_hpa);
    } else {
        cJSON_AddNullToObject(j_sensor, "bmp_temperature_c");
        cJSON_AddNullToObject(j_sensor, "pressure_hpa");
    }
    if (dht_ok) {
        cJSON_AddNumberToObject(j_sensor, "humidity_percent", sample.humidity_percent);
        cJSON_AddNumberToObject(j_sensor, "dht_temperature_c", sample.dht_temperature_c);
    } else {
        cJSON_AddNullToObject(j_sensor, "humidity_percent");
        cJSON_AddNullToObject(j_sensor, "dht_temperature_c");
    }
    cJSON_AddNumberToObject(j_sensor, "seq", sample.seq);
    if (sample.valid != 0) {
        cJSON_AddNumberToObject(j_sensor, "age_s", (double)((now_us - sample.uptime_us) / 1000000));
    } else {
        cJSON_AddNumberToObject(j_sensor, "age_s", -1);
    }

    data_pool_stats_t pool;
    data_pool_get_stats(&pool);
    cJSON *j_pool = cJSON_AddObjectToObject(root, "pool");
    cJSON_AddNumberToObject(j_pool, "count", (double)pool.count);
    cJSON_AddNumberToObject(j_pool, "capacity", (double)pool.capacity);
    cJSON_AddNumberToObject(j_pool, "pushed", pool.pushed);
    cJSON_AddNumberToObject(j_pool, "dropped", pool.dropped);
    cJSON_AddNumberToObject(j_pool, "sent", pool.sent);

    thingspeak_status_t ts;
    thingspeak_get_status(&ts);
    cJSON *j_ts = cJSON_AddObjectToObject(root, "thingspeak");
    cJSON_AddBoolToObject(j_ts, "time_synced", ts.time_synced);
    cJSON_AddNumberToObject(j_ts, "requests", ts.requests);
    cJSON_AddNumberToObject(j_ts, "failures", ts.failures);
    cJSON_AddNumberToObject(j_ts, "last_http_status", ts.last_http_status);
    cJSON_AddNumberToObject(j_ts, "last_batch_size", ts.last_batch_size);
    cJSON_AddNumberToObject(j_ts, "last_success_s",
                            ts.last_success_uptime_us > 0
                                ? (double)((now_us - ts.last_success_uptime_us) / 1000000)
                                : -1);
    ts.last_error[sizeof(ts.last_error) - 1] = '\0';
    cJSON_AddStringToObject(j_ts, "last_error", ts.last_error);

    cJSON_AddNumberToObject(root, "uptime_s", (double)(now_us / 1000000));
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    return send_json(req, NULL, root);
}

static esp_err_t history_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) return ESP_OK;
    return sensor_history_get(req);
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    wifi_ap_record_t *records = calloc(SCAN_MAX_RECORDS, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        return send_error(req, "500 Internal Server Error", "Het bo nho");
    }
    uint16_t count = SCAN_MAX_RECORDS;
    esp_err_t err = wifi_manager_scan(records, &count);
    if (err != ESP_OK) {
        free(records);
        ESP_LOGW(TAG, "Quet Wi-Fi that bai: %s", esp_err_to_name(err));
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    /* Loai SSID rong va trung lap (giu ban ghi manh nhat). */
    uint16_t kept = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        bool merged = false;
        for (uint16_t j = 0; j < kept; j++) {
            if (strcmp((const char *)records[j].ssid, (const char *)records[i].ssid) == 0) {
                if (records[i].rssi > records[j].rssi) {
                    records[j] = records[i];
                }
                merged = true;
                break;
            }
        }
        if (!merged) {
            records[kept++] = records[i];
        }
    }

    /* Sap xep theo RSSI giam dan (insertion sort, it phan tu). */
    for (uint16_t i = 1; i < kept; i++) {
        wifi_ap_record_t item = records[i];
        int j = i - 1;
        while (j >= 0 && records[j].rssi < item.rssi) {
            records[j + 1] = records[j];
            j--;
        }
        records[j + 1] = item;
    }

    cJSON *root = cJSON_CreateArray();
    for (uint16_t i = 0; i < kept; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(item, "channel", records[i].primary);
        cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(root, item);
    }
    free(records);
    return send_json(req, NULL, root);
}

static esp_err_t wifi_post_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    cJSON *root = read_json_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || (password != NULL && !cJSON_IsString(password) &&
                                  !cJSON_IsNull(password))) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "Thieu SSID");
    }
    const char *pass = cJSON_IsString(password) ? password->valuestring : "";

    esp_err_t err = wifi_manager_connect(ssid->valuestring, pass);
    cJSON_Delete(root);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request",
                          "SSID 1-32 ky tu; mat khau de trong hoac 8-64 ky tu");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Luu cau hinh Wi-Fi that bai: %s", esp_err_to_name(err));
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    return send_ok(req);
}

static esp_err_t thingspeak_get_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    app_thingspeak_config_t config;
    app_config_get_thingspeak(&config);
    config.write_api_key[sizeof(config.write_api_key) - 1] = '\0';
    config.read_api_key[sizeof(config.read_api_key) - 1] = '\0';

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", config.enabled);
    cJSON_AddNumberToObject(root, "channel_id", config.channel_id);
    cJSON_AddStringToObject(root, "write_api_key", config.write_api_key);
    cJSON_AddStringToObject(root, "read_api_key", config.read_api_key);
    cJSON_AddNumberToObject(root, "period_s", config.period_s);
    cJSON_AddNumberToObject(root, "min_period_s", APP_PERIOD_MIN_S);
    cJSON_AddNumberToObject(root, "max_period_s", APP_PERIOD_MAX_S);
    cJSON_AddNumberToObject(root, "min_send_interval_s", THINGSPEAK_MIN_INTERVAL_MS / 1000);
    return send_json(req, NULL, root);
}

static bool json_to_u32(const cJSON *item, uint32_t *out)
{
    double value;
    if (cJSON_IsNumber(item)) {
        value = item->valuedouble;
    } else if (cJSON_IsString(item)) {
        char *end = NULL;
        value = strtod(item->valuestring, &end);
        if (end == item->valuestring || *end != '\0') {
            return false;
        }
    } else {
        return false;
    }
    if (value < 0 || value > 4294967295.0 || value != (double)(uint32_t)value) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static esp_err_t thingspeak_post_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    cJSON *root = read_json_body(req);
    if (root == NULL) {
        return ESP_OK;
    }

    /* Truong nao khong gui thi giu gia tri hien tai. */
    app_thingspeak_config_t config;
    app_config_get_thingspeak(&config);
    uint32_t old_channel_id = config.channel_id;
    char old_read_key[APP_TS_API_KEY_LEN + 1];
    memcpy(old_read_key, config.read_api_key, sizeof(old_read_key));
    const char *error = NULL;

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (item != NULL) {
        if (cJSON_IsBool(item)) {
            config.enabled = cJSON_IsTrue(item);
        } else {
            error = "enabled phai la true/false";
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "channel_id");
    if (error == NULL && item != NULL && !cJSON_IsNull(item)) {
        if (cJSON_IsString(item) && item->valuestring[0] == '\0') {
            config.channel_id = 0;
        } else if (!json_to_u32(item, &config.channel_id)) {
            error = "Channel ID khong hop le";
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "write_api_key");
    if (error == NULL && item != NULL) {
        if (cJSON_IsString(item) && strlen(item->valuestring) < sizeof(config.write_api_key)) {
            strlcpy(config.write_api_key, item->valuestring, sizeof(config.write_api_key));
        } else {
            error = "Write API Key toi da 16 ky tu";
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "read_api_key");
    if (error == NULL && item != NULL) {
        if (cJSON_IsString(item) && strlen(item->valuestring) < sizeof(config.read_api_key)) {
            strlcpy(config.read_api_key, item->valuestring, sizeof(config.read_api_key));
        } else {
            error = "Read API Key toi da 16 ky tu";
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "period_s");
    if (error == NULL && item != NULL) {
        if (!json_to_u32(item, &config.period_s)) {
            error = "Chu ky khong hop le";
        }
    }
    cJSON_Delete(root);

    if (error != NULL) {
        return send_error(req, "400 Bad Request", error);
    }
    esp_err_t err = app_config_set_thingspeak(&config);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request",
                          "Thong so khong hop le (can API Key chu/so khi bat, chu ky trong gioi han)");
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Luu cau hinh ThingSpeak that bai: %s", esp_err_to_name(err));
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    if (old_channel_id != config.channel_id ||
        strcmp(old_read_key, config.read_api_key) != 0) {
        sensor_history_invalidate();
    }
    ESP_LOGI(TAG, "Da luu cau hinh ThingSpeak");
    return send_ok(req);
}

static esp_err_t reboot_post_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    esp_err_t err = send_ok(req);
    schedule_reboot();
    return err;
}

static esp_err_t factory_reset_post_handler(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    esp_err_t err = app_config_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi phuc cai dat goc that bai: %s", esp_err_to_name(err));
        return send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "Da khoi phuc cai dat goc");
    err = send_ok(req);
    schedule_reboot();
    return err;
}

/* ------------------------------------------------------------------------ */
/* Khoi dong                                                                 */
/* ------------------------------------------------------------------------ */

static esp_err_t build_expected_auth(const char *username, const char *password)
{
    char plain[AUTH_MAX_LEN];
    int length = snprintf(plain, sizeof(plain), "%s:%s", username, password);
    if (length < 0 || (size_t)length >= sizeof(plain)) {
        return ESP_ERR_INVALID_SIZE;
    }

    static const char prefix[] = "Basic ";
    size_t written = 0;
    int ret = mbedtls_base64_encode((unsigned char *)s_expected_auth + strlen(prefix),
                                    sizeof(s_expected_auth) - strlen(prefix), &written,
                                    (const unsigned char *)plain, (size_t)length);
    memset(plain, 0, sizeof(plain));
    if (ret != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_expected_auth, prefix, strlen(prefix));
    s_expected_auth[strlen(prefix) + written] = '\0';
    return ESP_OK;
}

esp_err_t web_server_start(const web_server_config_t *config)
{
    if (s_server != NULL) {
        return ESP_OK;
    }
    if (config == NULL || config->username == NULL || config->password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = build_expected_auth(config->username, config->password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Tai khoan dang nhap qua dai");
        return err;
    }

    if (s_reboot_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = reboot_timer_cb,
            .name = "web_reboot",
        };
        err = esp_timer_create(&timer_args, &s_reboot_timer);
        if (err != ESP_OK) {
            return err;
        }
    }

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.stack_size = 8192;
    http_config.max_uri_handlers = 12;
    http_config.lru_purge_enable = true;

    err = httpd_start(&s_server, &http_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi dong HTTP server that bai: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler},
        {.uri = "/api/sensors/history", .method = HTTP_GET, .handler = history_get_handler},
        {.uri = "/api/scan", .method = HTTP_GET, .handler = scan_get_handler},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post_handler},
        {.uri = "/api/thingspeak", .method = HTTP_GET, .handler = thingspeak_get_handler},
        {.uri = "/api/thingspeak", .method = HTTP_POST, .handler = thingspeak_post_handler},
        {.uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_post_handler},
        {.uri = "/api/factory-reset", .method = HTTP_POST, .handler = factory_reset_post_handler},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        err = httpd_register_uri_handler(s_server, &uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Dang ky %s that bai: %s", uris[i].uri, esp_err_to_name(err));
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }

    ESP_LOGI(TAG, "Trang cau hinh da san sang (cong %d)", http_config.server_port);
    return ESP_OK;
}
