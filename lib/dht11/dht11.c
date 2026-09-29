#include "dht11.h"
#include "freertos/FreeRTOS.h"

int wait_for_state(dht11_t dht11,int state,int timeout)
{
    gpio_set_direction(dht11.dht11_pin, GPIO_MODE_INPUT);
    int count = 0;
    
    while(gpio_get_level(dht11.dht11_pin) != state)
    {
        if(count >= timeout) return -1;
        count += 2;
        ets_delay_us(2);
        
    }

    return  count;
}

void hold_low(dht11_t dht11,int hold_time_us)
{
    gpio_set_direction(dht11.dht11_pin,GPIO_MODE_OUTPUT);
    gpio_set_level(dht11.dht11_pin,0);
    ets_delay_us(hold_time_us);
    gpio_set_level(dht11.dht11_pin,1);
}

/* Tin hieu DHT11 dai ~4,5 ms, moi bit phan biet bang do rong xung 26/70 us.
 * Ngat Wi-Fi chen vao se lam sai do rong xung, nen phan pha hoi dap va 40 bit
 * duoc doc trong vung critical (tat ngat tren core hien tai). Xung keo thap
 * 18 ms nam ngoai vung nay de khong chan ngat qua lau. */
static portMUX_TYPE s_dht11_lock = portMUX_INITIALIZER_UNLOCKED;

typedef enum {
    DHT11_OK = 0,
    DHT11_ERR_PHASE1,
    DHT11_ERR_PHASE2,
    DHT11_ERR_PHASE3,
    DHT11_ERR_DATA,
} dht11_frame_result_t;

static dht11_frame_result_t read_frame(dht11_t *dht11, uint8_t data[5])
{
    dht11_frame_result_t result = DHT11_OK;

    gpio_set_direction(dht11->dht11_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(dht11->dht11_pin, 0);
    ets_delay_us(18000);

    portENTER_CRITICAL(&s_dht11_lock);
    gpio_set_level(dht11->dht11_pin, 1);
    if (wait_for_state(*dht11, 0, 40) < 0) {
        result = DHT11_ERR_PHASE1;
    } else if (wait_for_state(*dht11, 1, 90) < 0) {
        result = DHT11_ERR_PHASE2;
    } else if (wait_for_state(*dht11, 0, 90) < 0) {
        result = DHT11_ERR_PHASE3;
    } else {
        for (int i = 0; i < 5 && result == DHT11_OK; i++) {
            for (int j = 0; j < 8; j++) {
                int zero_duration = wait_for_state(*dht11, 1, 58);
                int one_duration = zero_duration < 0 ? -1 : wait_for_state(*dht11, 0, 74);
                if (one_duration < 0) {
                    result = DHT11_ERR_DATA;
                    break;
                }
                data[i] |= (one_duration > zero_duration) << (7 - j);
            }
        }
    }
    portEXIT_CRITICAL(&s_dht11_lock);
    return result;
}

int dht11_read(dht11_t *dht11,int connection_timeout)
{
    uint8_t received_data[5] = {0};
    dht11_frame_result_t result = DHT11_ERR_PHASE1;

    for (int attempt = 0; attempt < connection_timeout; attempt++) {
        memset(received_data, 0, sizeof(received_data));
        result = read_frame(dht11, received_data);
        if (result == DHT11_OK || result == DHT11_ERR_DATA) {
            break;
        }
        ESP_LOGE("DHT11:", "Failed at phase %d", result);
        ets_delay_us(20000);
    }

    gpio_set_direction(dht11->dht11_pin, GPIO_MODE_INPUT);
    if (result != DHT11_OK) {
        if (result == DHT11_ERR_DATA) {
            ESP_LOGE("DHT11:", "Timeout while reading data bits");
        }
        return -1;
    }

    int crc = received_data[0]+received_data[1]+received_data[2]+received_data[3];
    crc = crc & 0xff;
    if(crc == received_data[4]) {
      dht11->humidity = received_data[0] + received_data[1] / 10.0;
      dht11->temperature = received_data[2] + received_data[3] / 10.0;
      return 0;
    }
    else {
        ESP_LOGE("DHT11:", "Wrong checksum");
        return -1;
    }
}
