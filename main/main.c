#include <stdbool.h>

#include "app_config.h"
#include "app_tick.h"
#include "data_pool.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "sensor_service.h"
#include "status_led.h"
#include "thingspeak.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "ESP32_MAIN";

#define APP_TICK_PERIOD_MS  1000
#define HEARTBEAT_PERIOD_MS (60 * 1000)

static void on_connection_changed(bool connected)
{
    status_led_set_mode(connected ? STATUS_LED_CONNECTED : STATUS_LED_DISCONNECTED);
}

/* Ghi log loi va tra ve true neu buoc khoi dong that bai. */
static bool failed(esp_err_t err, const char *step)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s that bai: %s", step, esp_err_to_name(err));
        return true;
    }
    return false;
}

/* Dinh ky in IP de nguoi dung luon thay dia chi trang cau hinh trong log. */
static void heartbeat_loop(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));

        wifi_manager_status_t wifi;
        wifi_manager_get_status(&wifi);
        data_pool_stats_t pool;
        data_pool_get_stats(&pool);
        if (wifi.state == WIFI_MANAGER_CONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi \"%s\" IP http://%s (RSSI %d) | AP \"%s\" http://%s | pool %u/%u",
                     wifi.ssid, wifi.sta_ip, wifi.rssi, wifi.ap_ssid, wifi.ap_ip,
                     (unsigned)pool.count, (unsigned)pool.capacity);
        } else {
            ESP_LOGI(TAG, "Chua ket noi Wi-Fi | AP \"%s\" http://%s | pool %u/%u",
                     wifi.ap_ssid, wifi.ap_ip, (unsigned)pool.count, (unsigned)pool.capacity);
        }
    }
}

void app_main(void)
{
    if (failed(app_config_init(), "app_config_init")) {
        return;
    }

    if (!failed(status_led_init((gpio_num_t)CONFIG_APP_STATUS_LED_GPIO), "status_led_init")) {
        status_led_set_mode(STATUS_LED_DISCONNECTED);
    }

    if (failed(data_pool_init(), "data_pool_init")) {
        return;
    }

    const wifi_manager_config_t wifi_config = {
        .ap_ssid = CONFIG_APP_AP_SSID,
        .ap_ip = CONFIG_APP_AP_IP,
        .on_connection_changed = on_connection_changed,
    };
    if (failed(wifi_manager_start(&wifi_config), "wifi_manager_start")) {
        return;
    }

    const web_server_config_t web_config = {
        .username = CONFIG_APP_WEB_USERNAME,
        .password = CONFIG_APP_WEB_PASSWORD,
    };
    /* Web loi van tiep tuc do va gui du lieu. */
    failed(web_server_start(&web_config), "web_server_start");

    TaskHandle_t ts_task = NULL;
    if (failed(thingspeak_worker_start(wifi_manager_is_connected, &ts_task),
               "thingspeak_worker_start")) {
        return;
    }

    const sensor_service_config_t sensor_config = {
        .bmp180_sda_gpio = CONFIG_BMP180_SDA_GPIO,
        .bmp180_scl_gpio = CONFIG_BMP180_SCL_GPIO,
        .dht11_gpio = CONFIG_DHT11_GPIO,
        .sea_level_pressure_pa = CONFIG_BMP180_SEA_LEVEL_PRESSURE_PA,
        .consumer_task = ts_task,
        .consumer_notify_bits = THINGSPEAK_NOTIFY_DATA,
    };
    TaskHandle_t sensor_task = NULL;
    if (failed(sensor_service_start(&sensor_config, &sensor_task), "sensor_service_start")) {
        return;
    }

    if (failed(app_tick_subscribe(sensor_task, SENSOR_NOTIFY_TICK), "app_tick_subscribe(sensor)") ||
        failed(app_tick_subscribe(ts_task, THINGSPEAK_NOTIFY_TICK), "app_tick_subscribe(thingspeak)") ||
        failed(app_tick_start(APP_TICK_PERIOD_MS), "app_tick_start")) {
        return;
    }

    ESP_LOGI(TAG, "Khoi dong xong: tick %d ms, AP \"%s\" http://%s",
             APP_TICK_PERIOD_MS, CONFIG_APP_AP_SSID, CONFIG_APP_AP_IP);
    heartbeat_loop();
}
