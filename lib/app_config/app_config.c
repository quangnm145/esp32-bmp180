#include "app_config.h"

#include <ctype.h>
#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "APP_CONFIG";
static const char *NVS_NAMESPACE = "app_cfg";

static SemaphoreHandle_t s_lock;
static app_wifi_config_t s_wifi;
static bool s_wifi_valid;
static app_thingspeak_config_t s_thingspeak;

static void thingspeak_defaults(app_thingspeak_config_t *config)
{
    _Static_assert(sizeof(CONFIG_APP_DEFAULT_TS_WRITE_KEY) - 1 <= APP_TS_API_KEY_LEN,
                   "CONFIG_APP_DEFAULT_TS_WRITE_KEY qua dai");
    memset(config, 0, sizeof(*config));
    /* Mac dinh lay tu Kconfig (sdkconfig.secrets); gia tri luu trong NVS se ghi de. */
    strlcpy(config->write_api_key, CONFIG_APP_DEFAULT_TS_WRITE_KEY,
            sizeof(config->write_api_key));
    config->channel_id = (uint32_t)CONFIG_APP_DEFAULT_TS_CHANNEL_ID;
    config->period_s = CONFIG_APP_DEFAULT_PERIOD_S;
#if CONFIG_APP_DEFAULT_TS_ENABLED
    config->enabled = config->write_api_key[0] != '\0';
#else
    config->enabled = false;
#endif
    /* Key mac dinh co ky tu la thi bo, tranh ghep vao URL. */
    for (size_t i = 0; config->write_api_key[i] != '\0'; i++) {
        if (!isalnum((unsigned char)config->write_api_key[i])) {
            memset(config->write_api_key, 0, sizeof(config->write_api_key));
            config->enabled = false;
            break;
        }
    }
}

static bool thingspeak_valid(const app_thingspeak_config_t *config)
{
    size_t key_length = strnlen(config->write_api_key, sizeof(config->write_api_key));
    if (key_length == sizeof(config->write_api_key)) {
        return false;
    }
    if (config->enabled && key_length == 0) {
        return false;
    }
    /* Key duoc ghep thang vao URL nen chi nhan chu va so. */
    for (size_t i = 0; i < key_length; i++) {
        if (!isalnum((unsigned char)config->write_api_key[i])) {
            return false;
        }
    }
    return config->period_s >= APP_PERIOD_MIN_S && config->period_s <= APP_PERIOD_MAX_S;
}

static void load_from_nvs(void)
{
    thingspeak_defaults(&s_thingspeak);
    s_wifi_valid = false;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "Chua co cau hinh luu, dung mac dinh");
        return;
    }

    size_t size = sizeof(s_wifi);
    if (nvs_get_blob(nvs, "wifi", &s_wifi, &size) == ESP_OK && size == sizeof(s_wifi)) {
        s_wifi.ssid[APP_WIFI_SSID_LEN] = '\0';
        s_wifi.password[APP_WIFI_PASSWORD_LEN] = '\0';
        s_wifi_valid = s_wifi.ssid[0] != '\0';
    }

    app_thingspeak_config_t thingspeak;
    size = sizeof(thingspeak);
    if (nvs_get_blob(nvs, "thingspeak", &thingspeak, &size) == ESP_OK &&
        size == sizeof(thingspeak) && thingspeak_valid(&thingspeak)) {
        s_thingspeak = thingspeak;
    }
    nvs_close(nvs);
}

/* Wi-Fi mac dinh tu sdkconfig khi NVS chua co (vd lan nap dau tien). */
static void wifi_defaults(void)
{
    if (s_wifi_valid || CONFIG_APP_DEFAULT_WIFI_SSID[0] == '\0') {
        return;
    }
    memset(&s_wifi, 0, sizeof(s_wifi));
    strlcpy(s_wifi.ssid, CONFIG_APP_DEFAULT_WIFI_SSID, sizeof(s_wifi.ssid));
    strlcpy(s_wifi.password, CONFIG_APP_DEFAULT_WIFI_PASSWORD, sizeof(s_wifi.password));
    s_wifi_valid = true;
    ESP_LOGI(TAG, "Dung Wi-Fi mac dinh tu sdkconfig");
}

static esp_err_t save_blob(const char *key, const void *data, size_t size)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, key, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t app_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    load_from_nvs();
    wifi_defaults();
    ESP_LOGI(TAG, "Wi-Fi: %s, ThingSpeak: %s, chu ky %" PRIu32 " s",
             s_wifi_valid ? s_wifi.ssid : "(chua cau hinh)",
             s_thingspeak.enabled ? "bat" : "tat", s_thingspeak.period_s);
    return ESP_OK;
}

esp_err_t app_config_get_wifi(app_wifi_config_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = s_wifi_valid ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (s_wifi_valid) {
        *out = s_wifi;
    }
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t app_config_set_wifi(const app_wifi_config_t *config)
{
    if (config->ssid[0] == '\0' ||
        strnlen(config->ssid, sizeof(config->ssid)) == sizeof(config->ssid) ||
        strnlen(config->password, sizeof(config->password)) == sizeof(config->password)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = save_blob("wifi", config, sizeof(*config));
    if (err == ESP_OK) {
        s_wifi = *config;
        s_wifi_valid = true;
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Luu Wi-Fi that bai: %s", esp_err_to_name(err));
    }
    return err;
}

void app_config_get_thingspeak(app_thingspeak_config_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_thingspeak;
    xSemaphoreGive(s_lock);
}

esp_err_t app_config_set_thingspeak(const app_thingspeak_config_t *config)
{
    if (!thingspeak_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = save_blob("thingspeak", config, sizeof(*config));
    if (err == ESP_OK) {
        s_thingspeak = *config;
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Luu ThingSpeak that bai: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t app_config_factory_reset(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_all(nvs);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    if (err == ESP_OK) {
        s_wifi_valid = false;
        memset(&s_wifi, 0, sizeof(s_wifi));
        thingspeak_defaults(&s_thingspeak);
    }
    xSemaphoreGive(s_lock);
    return err;
}
