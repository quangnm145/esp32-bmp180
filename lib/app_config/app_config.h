#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_WIFI_SSID_LEN      32
#define APP_WIFI_PASSWORD_LEN  64
#define APP_TS_API_KEY_LEN     16

#define APP_PERIOD_MIN_S       5
#define APP_PERIOD_MAX_S       3600

typedef struct {
    char ssid[APP_WIFI_SSID_LEN + 1];
    char password[APP_WIFI_PASSWORD_LEN + 1];
} app_wifi_config_t;

typedef struct {
    bool enabled;
    uint32_t channel_id;                      /* 0: khong dung bulk update */
    char write_api_key[APP_TS_API_KEY_LEN + 1];
    uint32_t period_s;                        /* chu ky lay mau = chu ky gui */
    char read_api_key[APP_TS_API_KEY_LEN + 1]; /* NVS; chi dung cho lich su */
} app_thingspeak_config_t;

/* Khoi tao NVS va nap cau hinh vao bo nho dem. Goi mot lan truoc cac module khac. */
esp_err_t app_config_init(void);

/* Tra ve ESP_ERR_NOT_FOUND khi chua luu Wi-Fi nao. */
esp_err_t app_config_get_wifi(app_wifi_config_t *out);
esp_err_t app_config_set_wifi(const app_wifi_config_t *config);

void app_config_get_thingspeak(app_thingspeak_config_t *out);
/* Tra ve ESP_ERR_INVALID_ARG neu thong so ngoai gioi han. */
esp_err_t app_config_set_thingspeak(const app_thingspeak_config_t *config);

/* Xoa toan bo cau hinh da luu (Wi-Fi + ThingSpeak). */
esp_err_t app_config_factory_reset(void);
