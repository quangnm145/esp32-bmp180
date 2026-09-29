#include "sensor_service.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

#include "app_config.h"
#include "bmp180.h"
#include "dht11.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "signal_filter.h"

static const char *TAG = "SENSOR";

#define DHT11_READ_EVERY_TICKS  2   /* DHT11 can >= 2 s giua hai lan do */
#define BMP180_RETRY_TICKS      10
#define STALE_TICKS             10  /* qua so tick nay khong doc duoc thi coi la hong */
#define FILTER_ALPHA            0.3f

typedef struct {
    signal_filter_t temperature;
    signal_filter_t pressure_pa;
    uint32_t last_ok_tick;
    bool present;
} bmp180_state_t;

typedef struct {
    dht11_t device;
    signal_filter_t temperature;
    signal_filter_t humidity;
    uint32_t last_ok_tick;
} dht11_state_t;

static sensor_service_config_t s_config;
static bmp180_state_t s_bmp;
static dht11_state_t s_dht;
static sensor_sample_t s_latest;
static portMUX_TYPE s_latest_lock = portMUX_INITIALIZER_UNLOCKED;

static bool is_fresh(uint32_t last_ok_tick, uint32_t tick)
{
    return last_ok_tick != 0 && tick - last_ok_tick <= STALE_TICKS;
}

#if CONFIG_APP_SENSOR_FAKE_DATA
/* Che do test khong co cam bien: sinh du lieu song tam giac (tang deu roi giam deu),
 * cong nhieu nho va mot gai nhieu dinh ky, roi cho qua dung bo loc nhu du lieu that. */
#define FAKE_CYCLE_TICKS        120     /* mot chu ky tang + giam (tick = 1 s) */
#define FAKE_SPIKE_EVERY_TICKS  25      /* chen mot gai nhieu moi 25 tick */
#define FAKE_TEMP_MIN_C         20.0f
#define FAKE_TEMP_MAX_C         35.0f
#define FAKE_PRESSURE_MIN_PA    100000.0f
#define FAKE_PRESSURE_MAX_PA    102000.0f
#define FAKE_HUMIDITY_MIN       40.0f
#define FAKE_HUMIDITY_MAX       80.0f
#define FAKE_DHT_TEMP_OFFSET_C  (-0.5f)
#define FAKE_SPIKE_TEMP_C       8.0f
#define FAKE_SPIKE_PRESSURE_PA  400.0f
#define FAKE_SPIKE_HUMIDITY     5.0f

static uint32_t s_fake_seed = 12345;
static float s_fake_raw_temperature;
static float s_fake_raw_humidity;

/* LCG don gian, tat dinh: tra ve nhieu trong [-amplitude, +amplitude]. */
static float fake_noise(float amplitude)
{
    s_fake_seed = s_fake_seed * 1664525u + 1013904223u;
    float unit = (float)(s_fake_seed >> 8) / (float)(1u << 24);
    return (unit * 2.0f - 1.0f) * amplitude;
}

/* 0 -> 1 -> 0 tuyen tinh trong FAKE_CYCLE_TICKS tick. */
static float fake_level(uint32_t tick)
{
    const uint32_t half = FAKE_CYCLE_TICKS / 2;
    uint32_t phase = tick % FAKE_CYCLE_TICKS;
    return phase <= half ? (float)phase / half : (float)(FAKE_CYCLE_TICKS - phase) / half;
}

static void read_bmp180(uint32_t tick)
{
    float level = fake_level(tick);
    float temperature = FAKE_TEMP_MIN_C + (FAKE_TEMP_MAX_C - FAKE_TEMP_MIN_C) * level +
                        fake_noise(0.1f);
    float pressure = FAKE_PRESSURE_MIN_PA + (FAKE_PRESSURE_MAX_PA - FAKE_PRESSURE_MIN_PA) * level +
                     fake_noise(5.0f);
    bool spike = tick % FAKE_SPIKE_EVERY_TICKS == 0;
    if (spike) {
        temperature += FAKE_SPIKE_TEMP_C;
        pressure += FAKE_SPIKE_PRESSURE_PA;
    }
    s_fake_raw_temperature = temperature;

    if (signal_filter_update(&s_bmp.temperature, temperature) &&
        signal_filter_update(&s_bmp.pressure_pa, pressure)) {
        s_bmp.last_ok_tick = tick;
    } else {
        ESP_LOGW(TAG, "FAKE BMP180 bi loai");
    }
    float filtered = 0.0f;
    signal_filter_get(&s_bmp.temperature, &filtered);
    ESP_LOGD(TAG, "FAKE tick %" PRIu32 " T raw=%.2f loc=%.2f P raw=%.0f%s", tick,
             temperature, filtered, pressure, spike ? " (gai nhieu)" : "");
}

static void read_dht11(uint32_t tick)
{
    if (tick % DHT11_READ_EVERY_TICKS != 0) {
        return;
    }
    float level = fake_level(tick);
    float humidity = FAKE_HUMIDITY_MIN + (FAKE_HUMIDITY_MAX - FAKE_HUMIDITY_MIN) * level +
                     fake_noise(0.3f);
    float temperature = FAKE_TEMP_MIN_C + (FAKE_TEMP_MAX_C - FAKE_TEMP_MIN_C) * level +
                        FAKE_DHT_TEMP_OFFSET_C + fake_noise(0.1f);
    /* DHT chi doc tick chan: lan doc dau tien sau moc gai nhieu thi chen gai. */
    bool spike = tick % FAKE_SPIKE_EVERY_TICKS < DHT11_READ_EVERY_TICKS;
    if (spike) {
        humidity += FAKE_SPIKE_HUMIDITY;
    }
    s_fake_raw_humidity = humidity;

    if (signal_filter_update(&s_dht.humidity, humidity) &&
        signal_filter_update(&s_dht.temperature, temperature)) {
        s_dht.last_ok_tick = tick;
    } else {
        ESP_LOGW(TAG, "FAKE DHT11 bi loai");
    }
    ESP_LOGD(TAG, "FAKE tick %" PRIu32 " H raw=%.1f T2 raw=%.2f%s", tick, humidity,
             temperature, spike ? " (gai nhieu)" : "");
}
#else
static void read_bmp180(uint32_t tick)
{
    if (!s_bmp.present) {
        if (tick % BMP180_RETRY_TICKS != 1) {
            return;
        }
        s_bmp.present = bmp180_init(s_config.bmp180_sda_gpio, s_config.bmp180_scl_gpio) == ESP_OK;
        if (!s_bmp.present) {
            ESP_LOGE(TAG, "Khong tim thay BMP180 (SDA=%d, SCL=%d)",
                     s_config.bmp180_sda_gpio, s_config.bmp180_scl_gpio);
            return;
        }
        ESP_LOGI(TAG, "Ket noi BMP180 thanh cong");
    }

    float temperature;
    uint32_t pressure;
    if (bmp180_read_temperature(&temperature) == ESP_OK &&
        bmp180_read_pressure(&pressure) == ESP_OK &&
        signal_filter_update(&s_bmp.temperature, temperature) &&
        signal_filter_update(&s_bmp.pressure_pa, (float)pressure)) {
        s_bmp.last_ok_tick = tick;
    } else {
        ESP_LOGW(TAG, "Loi doc BMP180");
    }
}

static void read_dht11(uint32_t tick)
{
    if (tick % DHT11_READ_EVERY_TICKS != 0) {
        return;
    }
    if (dht11_read(&s_dht.device, 1) == 0 &&
        signal_filter_update(&s_dht.humidity, s_dht.device.humidity) &&
        signal_filter_update(&s_dht.temperature, s_dht.device.temperature)) {
        s_dht.last_ok_tick = tick;
    } else {
        ESP_LOGW(TAG, "Loi doc DHT11");
    }
}

#endif /* CONFIG_APP_SENSOR_FAKE_DATA */

/* Mat cam bien qua lau thi xoa bo loc, tranh tron du lieu cu khi cam bien hoat dong lai. */
static void drop_stale(uint32_t tick)
{
    if (s_bmp.last_ok_tick != 0 && !is_fresh(s_bmp.last_ok_tick, tick)) {
        signal_filter_reset(&s_bmp.temperature);
        signal_filter_reset(&s_bmp.pressure_pa);
        s_bmp.last_ok_tick = 0;
    }
    if (s_dht.last_ok_tick != 0 && !is_fresh(s_dht.last_ok_tick, tick)) {
        signal_filter_reset(&s_dht.temperature);
        signal_filter_reset(&s_dht.humidity);
        s_dht.last_ok_tick = 0;
    }
}

static bool pack_sample(uint32_t tick, sensor_sample_t *sample)
{
    memset(sample, 0, sizeof(*sample));
    sample->uptime_us = esp_timer_get_time();

    float pressure_pa;
    if (is_fresh(s_bmp.last_ok_tick, tick) &&
        signal_filter_get(&s_bmp.temperature, &sample->bmp_temperature_c) &&
        signal_filter_get(&s_bmp.pressure_pa, &pressure_pa)) {
        sample->pressure_hpa = pressure_pa / 100.0f;
        sample->altitude_m = 44330.0f *
            (1.0f - powf(pressure_pa / (float)s_config.sea_level_pressure_pa, 0.190295f));
        sample->valid |= SAMPLE_VALID_BMP180;
    }
    if (is_fresh(s_dht.last_ok_tick, tick) &&
        signal_filter_get(&s_dht.humidity, &sample->humidity_percent) &&
        signal_filter_get(&s_dht.temperature, &sample->dht_temperature_c)) {
        sample->valid |= SAMPLE_VALID_DHT11;
    }
    return sample->valid != 0;
}

static void sensor_task(void *arg)
{
    uint32_t tick = 0;
    uint32_t elapsed_s = 0;

    while (true) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        if (!(bits & SENSOR_NOTIFY_TICK)) {
            continue;
        }
        tick++;

        read_bmp180(tick);
        read_dht11(tick);
        drop_stale(tick);

        app_thingspeak_config_t ts;
        app_config_get_thingspeak(&ts);
        if (++elapsed_s < ts.period_s) {
            continue;
        }
        elapsed_s = 0;

        sensor_sample_t sample;
        if (!pack_sample(tick, &sample)) {
            ESP_LOGW(TAG, "Khong co cam bien nao hop le, bo qua chu ky");
            continue;
        }
        bool dropped = data_pool_push(&sample);

        portENTER_CRITICAL(&s_latest_lock);
        s_latest = sample;
        portEXIT_CRITICAL(&s_latest_lock);

        ESP_LOGI(TAG, "#%" PRIu32 " T=%.2f*C P=%.2fhPa H=%.1f%% T2=%.1f*C%s",
                 sample.seq, sample.bmp_temperature_c, sample.pressure_hpa,
                 sample.humidity_percent, sample.dht_temperature_c,
                 dropped ? " (pool day, da bo object cu nhat)" : "");
#if CONFIG_APP_SENSOR_FAKE_DATA
        ESP_LOGI(TAG, "FAKE raw=%.2f loc=%.2f *C | H raw=%.1f loc=%.1f %%",
                 s_fake_raw_temperature, sample.bmp_temperature_c,
                 s_fake_raw_humidity, sample.humidity_percent);
#endif
        if (s_config.consumer_task != NULL) {
            xTaskNotify(s_config.consumer_task, s_config.consumer_notify_bits, eSetBits);
        }
    }
}

esp_err_t sensor_service_start(const sensor_service_config_t *config, TaskHandle_t *out_task)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(config->dht11_gpio) ||
        config->dht11_gpio == config->bmp180_sda_gpio ||
        config->dht11_gpio == config->bmp180_scl_gpio) {
        ESP_LOGE(TAG, "GPIO DHT11 khong hop le hoac trung chan I2C");
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

    signal_filter_init(&s_bmp.temperature, FILTER_ALPHA, -40.0f, 85.0f);
    signal_filter_init(&s_bmp.pressure_pa, FILTER_ALPHA, 30000.0f, 110000.0f);
    signal_filter_init(&s_dht.temperature, FILTER_ALPHA, 0.0f, 50.0f);
    signal_filter_init(&s_dht.humidity, FILTER_ALPHA, 0.0f, 100.0f);

#if CONFIG_APP_SENSOR_FAKE_DATA
    ESP_LOGW(TAG, "CHE DO FAKE DATA - khong doc cam bien that");
#else
    s_dht.device.dht11_pin = config->dht11_gpio;
    gpio_set_direction(config->dht11_gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(config->dht11_gpio, GPIO_PULLUP_ONLY);
#endif

    /* Core 1: vung critical cua DHT11 khong chan ngat Wi-Fi tren core 0. */
    BaseType_t ok = xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, NULL, 6,
                                            out_task, 1);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void sensor_service_get_latest(sensor_sample_t *out)
{
    portENTER_CRITICAL(&s_latest_lock);
    *out = s_latest;
    portEXIT_CRITICAL(&s_latest_lock);
}
