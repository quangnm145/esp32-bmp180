#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi_types.h"

typedef enum {
    WIFI_MANAGER_NOT_CONFIGURED,
    WIFI_MANAGER_CONNECTING,
    WIFI_MANAGER_CONNECTED,
    WIFI_MANAGER_DISCONNECTED,
} wifi_manager_state_t;

typedef void (*wifi_manager_connection_cb_t)(bool connected);

typedef struct {
    const char *ap_ssid;          /* Wi-Fi cau hinh ESP phat ra, luon bat */
    const char *ap_ip;            /* vd "192.168.1.14", mask /24 */
    wifi_manager_connection_cb_t on_connection_changed;
} wifi_manager_config_t;

typedef struct {
    wifi_manager_state_t state;
    char ssid[33];
    char sta_ip[16];
    char ap_ssid[33];
    char ap_ip[16];
    int8_t rssi;
    uint8_t last_disconnect_reason;   /* wifi_err_reason_t, 0 = chua co */
} wifi_manager_status_t;

/* Khoi tao netif + Wi-Fi APSTA va ket noi Wi-Fi da luu trong NVS (neu co). */
esp_err_t wifi_manager_start(const wifi_manager_config_t *config);

/* Luu SSID/password vao NVS roi ket noi lai. Ket qua theo doi qua get_status. */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/* Quet dong bo (~2-3 s). *count vao: kich thuoc mang, ra: so ban ghi. */
esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count);

bool wifi_manager_is_connected(void);
void wifi_manager_get_status(wifi_manager_status_t *out);
