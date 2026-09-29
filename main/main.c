#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bmp180.h"
#include "dht11.h"
#include "thingspeak.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "ESP32_MAIN";
static EventGroupHandle_t wifi_events;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Mat Wi-Fi, dang ket noi lai...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi da nhan IP");
    }
}

static void wifi_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    wifi_events = xEventGroupCreate();
    configASSERT(wifi_events != NULL);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    configASSERT(netif != NULL);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));
    wifi_config_t config = {
        .sta = {
            .ssid = CONFIG_THINGSPEAK_WIFI_SSID,
            .password = CONFIG_THINGSPEAK_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "--- Test BMP180: SDA=%d, SCL=%d ---",
             CONFIG_BMP180_SDA_GPIO, CONFIG_BMP180_SCL_GPIO);

    esp_err_t err = bmp180_init(CONFIG_BMP180_SDA_GPIO,
                                CONFIG_BMP180_SCL_GPIO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khong tim thay cam bien BMP180: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "Kiem tra SDA -> GPIO %d, SCL -> GPIO %d",
                 CONFIG_BMP180_SDA_GPIO, CONFIG_BMP180_SCL_GPIO);
        ESP_LOGE(TAG, "Kiem tra VCC -> 3V3 va GND -> GND");

        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "Ket noi BMP180 thanh cong!");

    dht11_t dht = {.dht11_pin = CONFIG_DHT11_GPIO};
    if (!GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_DHT11_GPIO) ||
        CONFIG_DHT11_GPIO == CONFIG_BMP180_SDA_GPIO ||
        CONFIG_DHT11_GPIO == CONFIG_BMP180_SCL_GPIO) {
        ESP_LOGE(TAG, "GPIO DHT11 khong hop le hoac trung chan I2C");
        return;
    }
    ESP_ERROR_CHECK(gpio_set_direction(dht.dht11_pin, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(dht.dht11_pin, GPIO_PULLUP_ONLY));

    QueueHandle_t samples = xQueueCreate(1, sizeof(thingspeak_sample_t));
    configASSERT(samples != NULL);
    bool sender_started = false;
    bool wifi_configured = strlen(CONFIG_THINGSPEAK_WIFI_SSID) > 0;
    if (wifi_configured) {
        wifi_init();
    } else {
        ESP_LOGW(TAG, "Cau hinh Wi-Fi trong menuconfig -> ThingSpeak Configuration");
    }

    while (true) {
        bool valid_sample = true;
        float temperature_c;
        uint32_t pressure_pa;
        float altitude_m;

        err = bmp180_read_temperature(&temperature_c);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Loi doc nhiet do: %s", esp_err_to_name(err));
            valid_sample = false;
        } else {
            printf("Nhiet do = %.2f *C\n", temperature_c);
        }

        err = bmp180_read_pressure(&pressure_pa);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Loi doc ap suat: %s", esp_err_to_name(err));
            valid_sample = false;
        } else {
            printf("Ap suat  = %.2f hPa\n", pressure_pa / 100.0f);
        }

        err = bmp180_read_altitude(CONFIG_BMP180_SEA_LEVEL_PRESSURE_PA,
                                   &altitude_m);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Loi tinh do cao: %s", esp_err_to_name(err));
        } else {
            printf("Do cao   = %.2f met\n", altitude_m);
        }

        if (dht11_read(&dht, 1) == 0) {
            printf("Do am    = %.2f %%\n", dht.humidity);
        } else {
            ESP_LOGW(TAG, "Loi doc DHT11, bo qua mau gui ThingSpeak");
            valid_sample = false;
        }

        if (wifi_configured &&
            (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT)) {
            if (!sender_started) {
                sender_started = xTaskCreate(send_data_to_thingspeak, "thingspeak",
                                             8192, samples, 5, NULL) == pdPASS;
                if (!sender_started) {
                    ESP_LOGE(TAG, "Khong tao duoc task ThingSpeak");
                }
            }
            if (valid_sample && sender_started) {
                thingspeak_sample_t sample = {
                    .temperature_c = temperature_c,
                    .pressure_hpa = pressure_pa / 100.0f,
                    .humidity_percent = dht.humidity,
                };
                xQueueOverwrite(samples, &sample);
            }
        }
        printf("--------------------------------\n");
        /* DHT11 requires at least two seconds between measurements. */
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BMP180_UPDATE_INTERVAL_MS < 2000
                                    ? 2000 : CONFIG_BMP180_UPDATE_INTERVAL_MS));
    }
}
