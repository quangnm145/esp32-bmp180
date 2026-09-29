# Thư viện BMP180

Component ESP-IDF đọc nhiệt độ, áp suất và tính độ cao qua I2C.
Include `bmp180.h`, khai báo `REQUIRES bmp180` trong CMake của component sử dụng.
Ứng dụng mặc định nối SDA vào GPIO 25, SCL vào GPIO 26, VCC vào 3V3.

## API công khai

Các hàm trả `ESP_OK` khi thành công hoặc mã lỗi `esp_err_t`. Chỉ dùng kết quả
đầu ra khi hàm thành công. Gọi khởi tạo trước khi đọc, truyền con trỏ hợp lệ.

| Hàm | Chức năng |
| --- | --- |
| `bmp180_init(int pin_sda, int pin_scl)` | Khởi tạo I2C, kiểm tra cảm biến và nạp hệ số hiệu chuẩn. |
| `bmp180_read_temperature(float *temperature)` | Đọc và bù nhiệt độ, ghi kết quả °C vào con trỏ. |
| `bmp180_read_pressure(uint32_t *pressure)` | Đọc và bù áp suất, ghi kết quả Pa; chia cho `100.0f` để đổi sang hPa khi gửi ThingSpeak. |
| `bmp180_read_altitude(uint32_t reference_pressure, float *altitude)` | Đọc áp suất và tính độ cao (m) từ áp suất mực nước biển tham chiếu (Pa), thường dùng 101325. |

## Hàm nội bộ

Các hàm `static` chỉ phục vụ driver, không gọi từ ứng dụng:

| Hàm | Vai trò |
| --- | --- |
| `bmp180_master_write_slave()` | Gửi dữ liệu qua bus I2C đến cảm biến. |
| `bmp180_write_reg()` | Ghi lệnh vào thanh ghi. |
| `bmp180_master_read_slave()` | Nhận dữ liệu qua I2C. |
| `bmp180_read_int16()` | Đọc giá trị 16 bit có dấu từ thanh ghi. |
| `bmp180_read_uint16()` | Đọc giá trị 16 bit không dấu. |
| `bmp180_read_uint32()` | Đọc dữ liệu thanh ghi vào giá trị 32 bit. |
| `bmp180_read_uncompensated_temperature()` | Kích hoạt và đọc nhiệt độ thô. |
| `bmp180_calculate_b5()` | Tính hệ số trung gian B5 dùng để bù nhiệt độ và áp suất. |
| `bmp180_read_uncompensated_pressure()` | Kích hoạt và đọc áp suất thô. |

## Ví dụ

```c
ESP_ERROR_CHECK(bmp180_init(25, 26));
float temperature;
uint32_t pressure;
if (bmp180_read_temperature(&temperature) == ESP_OK &&
    bmp180_read_pressure(&pressure) == ESP_OK) {
    printf("T=%.2f C, P=%.2f hPa\n", temperature, pressure / 100.0f);
}
```

Driver dùng trạng thái chung cho cảm biến; ứng dụng hiện đọc từ một task.
Độ cao là giá trị ước tính, phụ thuộc áp suất tham chiếu đã cấu hình.
