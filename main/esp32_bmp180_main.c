#include <inttypes.h>
#include <stdio.h>

#include "bmp180.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BMP180_MAIN";

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

    while (true) {
        float temperature_c;
        uint32_t pressure_pa;
        float altitude_m;

        err = bmp180_read_temperature(&temperature_c);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Loi doc nhiet do: %s", esp_err_to_name(err));
        } else {
            printf("Nhiet do = %.2f *C\n", temperature_c);
        }

        err = bmp180_read_pressure(&pressure_pa);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Loi doc ap suat: %s", esp_err_to_name(err));
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

        printf("--------------------------------\n");
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BMP180_UPDATE_INTERVAL_MS));
    }
}
