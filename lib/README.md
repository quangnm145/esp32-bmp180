# Các component thư viện

| Component | Chức năng |
|-----------|-----------|
| `app_config` | Lưu/đọc cấu hình Wi-Fi và ThingSpeak trong NVS, có mặc định từ sdkconfig. |
| `app_tick` | GPTimer phần cứng; ISR mỗi chu kỳ gửi task notify tới các task đã đăng ký. |
| `sensor_service` | Task đọc cảm biến theo tick, lọc, đóng gói một object mỗi chu kỳ; có chế độ fake data. |
| `signal_filter` | Bộ lọc một kênh: kiểm tra dải → median 5 mẫu → EMA. |
| `data_pool` | Pool tĩnh 32 object dạng FIFO, đầy thì bỏ object cũ nhất. |
| `thingspeak` | Worker gửi pool lên ThingSpeak (bulk update + created_at), giới hạn 15 s. |
| `wifi_manager` | Wi-Fi APSTA: AP cấu hình luôn bật, STA tự kết nối lại, quét mạng. |
| `web_server` | Trang cấu hình + REST API, HTTP Basic Auth. |
| `status_led` | Nháy LED trạng thái kết nối. |
| [bmp180](bmp180/README.md) | Driver BMP180 qua I2C. |
| [dht11](dht11/README.md) | Driver DHT11; khung dữ liệu đọc trong vùng critical để Wi-Fi không làm sai timing. |

CMake ở thư mục gốc khai báo `EXTRA_COMPONENT_DIRS` trỏ tới `lib`.

## REST API (`web_server`)

Mọi URI yêu cầu Basic Auth.

| Method | URI | Mô tả |
|--------|-----|-------|
| GET | `/` | Trang cấu hình |
| GET | `/api/status` | Wi-Fi, cảm biến, pool, ThingSpeak, uptime |
| GET | `/api/scan` | Danh sách mạng Wi-Fi |
| POST | `/api/wifi` | `{"ssid","password"}` lưu và kết nối |
| GET/POST | `/api/thingspeak` | `{enabled, channel_id, write_api_key, period_s}` |
| POST | `/api/reboot` | Khởi động lại |
| POST | `/api/factory-reset` | Xoá cấu hình rồi khởi động lại |
