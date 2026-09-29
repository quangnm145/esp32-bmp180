#include "wifi_manager.h"

#include <string.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

static const char *TAG = "WIFI_MGR";

#define RECONNECT_DELAY_US      (5 * 1000 * 1000)
#define STA_DISCONNECTED_BIT    BIT0

static wifi_manager_config_t s_config;
static esp_netif_t *s_sta_netif;
static SemaphoreHandle_t s_op_lock;          /* tuan tu hoa scan/connect */
static EventGroupHandle_t s_events;
static esp_timer_handle_t s_reconnect_timer;

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_manager_status_t s_status;
static bool s_has_credentials;
static bool s_scanning;
static bool s_hold_reconnect;

static void set_state(wifi_manager_state_t state)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = state;
    if (state != WIFI_MANAGER_CONNECTED) {
        s_status.sta_ip[0] = '\0';
    }
    portEXIT_CRITICAL(&s_status_lock);
}

static bool reconnect_allowed(void)
{
    return s_has_credentials && !s_scanning && !s_hold_reconnect &&
           s_status.state != WIFI_MANAGER_CONNECTED;
}

static void connect_now(void)
{
    if (!reconnect_allowed()) {
        return;
    }
    set_state(WIFI_MANAGER_CONNECTING);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
}

static void reconnect_cb(void *arg)
{
    connect_now();
}

static void schedule_reconnect(void)
{
    if (reconnect_allowed()) {
        esp_timer_stop(s_reconnect_timer);
        esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_US);
    }
}

static void apply_sta_config(const app_wifi_config_t *credentials)
{
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, credentials->ssid, strnlen(credentials->ssid, sizeof(config.sta.ssid)));
    memcpy(config.sta.password, credentials->password,
           strnlen(credentials->password, sizeof(config.sta.password)));
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &config);

    portENTER_CRITICAL(&s_status_lock);
    strlcpy(s_status.ssid, credentials->ssid, sizeof(s_status.ssid));
    s_status.last_disconnect_reason = 0;
    portEXIT_CRITICAL(&s_status_lock);
    s_has_credentials = true;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        connect_now();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        bool was_connected = s_status.state == WIFI_MANAGER_CONNECTED;
        portENTER_CRITICAL(&s_status_lock);
        s_status.last_disconnect_reason = event->reason;
        portEXIT_CRITICAL(&s_status_lock);
        set_state(s_has_credentials ? WIFI_MANAGER_DISCONNECTED : WIFI_MANAGER_NOT_CONFIGURED);
        xEventGroupSetBits(s_events, STA_DISCONNECTED_BIT);
        if (was_connected) {
            ESP_LOGW(TAG, "Mat ket noi Wi-Fi (ly do %d)", event->reason);
            if (s_config.on_connection_changed) {
                s_config.on_connection_changed(false);
            }
        }
        schedule_reconnect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        portENTER_CRITICAL(&s_status_lock);
        s_status.state = WIFI_MANAGER_CONNECTED;
        s_status.last_disconnect_reason = 0;
        snprintf(s_status.sta_ip, sizeof(s_status.sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        portEXIT_CRITICAL(&s_status_lock);
        ESP_LOGI(TAG, "==================================================");
        ESP_LOGI(TAG, " Da ket noi Wi-Fi \"%s\"", s_status.ssid);
        ESP_LOGI(TAG, " Trang cau hinh (cung mang): http://" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "==================================================");
        if (s_config.on_connection_changed) {
            s_config.on_connection_changed(true);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *event = data;
        ESP_LOGI(TAG, "Thiet bi " MACSTR " vao %s, mo http://%s", MAC2STR(event->mac),
                 s_status.ap_ssid, s_status.ap_ip);
    }
}

static esp_err_t setup_ap_netif(esp_netif_t *netif)
{
    esp_netif_ip_info_t ip = {0};
    esp_err_t err = esp_netif_str_to_ip4(s_config.ap_ip, &ip.ip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IP AP khong hop le: %s", s_config.ap_ip);
        return err;
    }
    ip.gw = ip.ip;
    esp_netif_set_ip4_addr(&ip.netmask, 255, 255, 255, 0);

    esp_netif_dhcps_stop(netif);
    err = esp_netif_set_ip_info(netif, &ip);
    if (err == ESP_OK) {
        err = esp_netif_dhcps_start(netif);
    }
    return err;
}

esp_err_t wifi_manager_start(const wifi_manager_config_t *config)
{
    s_config = *config;
    s_op_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (s_op_lock == NULL || s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    strlcpy(s_status.ap_ssid, config->ap_ssid, sizeof(s_status.ap_ssid));
    strlcpy(s_status.ap_ip, config->ap_ip, sizeof(s_status.ap_ip));

    const esp_timer_create_args_t reconnect_args = {.callback = reconnect_cb, .name = "wifi_rc"};
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_args, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(setup_ap_netif(ap_netif));

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)ap.ap.ssid, config->ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    app_wifi_config_t credentials;
    if (app_config_get_wifi(&credentials) == ESP_OK) {
        apply_sta_config(&credentials);
        set_state(WIFI_MANAGER_CONNECTING);
    } else {
        set_state(WIFI_MANAGER_NOT_CONFIGURED);
        ESP_LOGW(TAG, "Chua cau hinh Wi-Fi");
    }
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, " Wi-Fi cau hinh: \"%s\" (khong mat khau)", config->ap_ssid);
    ESP_LOGI(TAG, " Trang cau hinh: http://%s", config->ap_ip);
    ESP_LOGI(TAG, "==================================================");
    return ESP_OK;
}

/* Ngat STA hien tai va cho su kien disconnect de khong bi lap lich ket noi lai cu. */
static void stop_sta_and_wait(void)
{
    esp_timer_stop(s_reconnect_timer);
    xEventGroupClearBits(s_events, STA_DISCONNECTED_BIT);
    if (esp_wifi_disconnect() == ESP_OK && s_status.state != WIFI_MANAGER_NOT_CONFIGURED) {
        xEventGroupWaitBits(s_events, STA_DISCONNECTED_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(1000));
    }
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    app_wifi_config_t credentials = {0};
    if (ssid == NULL || strlen(ssid) == 0 || strlen(ssid) > APP_WIFI_SSID_LEN ||
        password == NULL || strlen(password) > APP_WIFI_PASSWORD_LEN ||
        (strlen(password) > 0 && strlen(password) < 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(credentials.ssid, ssid, sizeof(credentials.ssid));
    strlcpy(credentials.password, password, sizeof(credentials.password));

    xSemaphoreTake(s_op_lock, portMAX_DELAY);
    esp_err_t err = app_config_set_wifi(&credentials);
    if (err == ESP_OK) {
        s_hold_reconnect = true;
        stop_sta_and_wait();
        apply_sta_config(&credentials);
        set_state(WIFI_MANAGER_DISCONNECTED);
        s_hold_reconnect = false;
        ESP_LOGI(TAG, "Ket noi toi \"%s\"...", credentials.ssid);
        connect_now();
    }
    xSemaphoreGive(s_op_lock);
    return err;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *records, uint16_t *count)
{
    xSemaphoreTake(s_op_lock, portMAX_DELAY);
    s_scanning = true;
    /* Dang thu ket noi thi driver tu choi scan: tam dung vong ket noi lai. */
    if (s_status.state != WIFI_MANAGER_CONNECTED) {
        stop_sta_and_wait();
    }

    wifi_scan_config_t config = {.show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&config, true);
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(count, records);
    } else {
        *count = 0;
        esp_wifi_clear_ap_list();
    }
    s_scanning = false;
    connect_now();
    xSemaphoreGive(s_op_lock);
    return err;
}

bool wifi_manager_is_connected(void)
{
    return s_status.state == WIFI_MANAGER_CONNECTED;
}

void wifi_manager_get_status(wifi_manager_status_t *out)
{
    portENTER_CRITICAL(&s_status_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_status_lock);

    out->rssi = 0;
    wifi_ap_record_t ap;
    if (out->state == WIFI_MANAGER_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out->rssi = ap.rssi;
    }
}
