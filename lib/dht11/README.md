# Thư viện DHT11

Component ESP-IDF đọc nhiệt độ và độ ẩm qua một chân GPIO.
Include `dht11.h`, khai báo `REQUIRES dht11` trong CMake của component sử dụng.

## Kiểu dữ liệu

`dht11_t` chứa `dht11_pin` (GPIO DATA), `temperature` (°C), `humidity` (%).
Trong ứng dụng, chân DATA mặc định là GPIO 27, cấu hình bằng menuconfig.
Chân DATA cần kéo lên 3V3 và phải hỗ trợ cả đầu vào lẫn đầu ra.

## Các hàm

| Hàm | Chức năng và kết quả |
| --- | --- |
| `dht11_read(dht11_t *dht11, int connection_timeout)` | Đọc cảm biến và kiểm tra checksum; trả `0` khi thành công, `-1` khi lỗi. Chỉ dùng các giá trị trong struct khi trả `0`. `connection_timeout` là số lần thử bắt tay, không phải thời gian tính bằng ms. |
| `wait_for_state(dht11_t dht11, int state, int timeout)` | Hàm hỗ trợ: chờ GPIO đạt mức `state`; trả thời gian chờ theo µs hoặc `-1` nếu vượt `timeout` (µs). |
| `hold_low(dht11_t dht11, int hold_time_us)` | Hàm hỗ trợ: chuyển GPIO sang đầu ra, giữ mức thấp trong số µs chỉ định rồi đưa lên cao để bắt đầu giao tiếp. Không trả giá trị. |

## Cách sử dụng

```c
dht11_t sensor = {.dht11_pin = 27};
gpio_set_direction(sensor.dht11_pin, GPIO_MODE_INPUT);
gpio_set_pull_mode(sensor.dht11_pin, GPIO_PULLUP_ONLY);
if (dht11_read(&sensor, 1) == 0) {
    printf("T=%.2f C, H=%.2f %%\n", sensor.temperature, sensor.humidity);
}
```

Chờ ít nhất 2 giây giữa các lần đọc. Ứng dụng dùng một lần thử mỗi chu kỳ;
nếu lỗi thì bỏ mẫu và đọc lại ở chu kỳ sau. Hàm đọc chạy đồng bộ, có chờ bận
để đo xung; không gọi đồng thời trên cùng cảm biến từ nhiều task.
