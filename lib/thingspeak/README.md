# ThingSpeak trong app_main

`app_main` đọc nhiệt độ BMP180 (field1, °C), áp suất BMP180 (field2, hPa),
và độ ẩm DHT11 (field3, %). Chỉ bộ số đo đọc thành công mới được đưa vào queue.
Task ThingSpeak tự khởi chạy sau khi Wi-Fi nhận IP. Sau mỗi lần gửi HTTP,
task chờ 30 giây trước lần gửi tiếp theo (cộng thời gian xử lý HTTP).
Lần đầu gửi ngay khi có mẫu hợp lệ. Log đọc cảm biến mỗi 5 giây không phải log gửi HTTP.
Wi-Fi tự kết nối lại khi mất mạng.

## Cấu hình và chạy

1. Chạy `idf.py menuconfig`, mở **ThingSpeak Configuration**.
2. Nhập **Wi-Fi SSID**, **Wi-Fi password** và **DHT11 data GPIO** (mặc định 32).
3. Nối DHT11 DATA vào GPIO đã chọn, VCC vào 3V3, GND vào GND; dùng điện trở
   kéo lên DATA nếu module chưa có. BMP180 giữ SDA 25, SCL 26 theo cấu hình.
4. Bật field1, field2, field3 trong Channel Settings của ThingSpeak.
5. Chạy `idf.py build` rồi `idf.py -p COMx flash monitor`.

Write API Key đã đặt trong `thingspeak.c`; không cần Read API Key để gửi.
Nếu SSID để trống, chương trình vẫn đọc cảm biến và nhắc cấu hình Wi-Fi.
Chu kỳ đọc cảm biến mặc định là 5 giây, được giới hạn tối thiểu 2 giây để phù hợp DHT11.

`send_data_to_thingspeak()` nhận queue một phần tử chứa `thingspeak_sample_t`.
Task chỉ xác nhận thành công khi HTTP 200 và body là entry ID dương.
Lỗi mạng không xóa task; lần gửi sau lấy mẫu mới nhất trong queue.

Tài liệu: https://www.mathworks.com/help/thingspeak/writedata.html

## Các hàm và kiểu dữ liệu

- `send_data_to_thingspeak(void *pvParameters)`: task FreeRTOS chạy liên tục.
  Tham số là `QueueHandle_t` của queue một phần tử, mỗi phần tử có kiểu
  `thingspeak_sample_t`. Tạo task đúng một lần sau khi Wi-Fi nhận IP;
  ứng dụng giữ queue tồn tại và cập nhật bằng `xQueueOverwrite()`.
  Task lấy mẫu mới nhất, bỏ số đo không hợp lệ, tạo URL GET, kiểm tra phản hồi
  rồi chờ `THINGSPEAK_SEND_INTERVAL_MS` (30000 ms), kể cả khi gửi lỗi.
  Nếu queue NULL hoặc không tạo được HTTP client, task ghi log rồi tự xóa.
- `on_http_event(esp_http_client_event_t *event)`: hàm nội bộ (`static`),
  ghép các đoạn body HTTP vào bộ đệm để đọc entry ID và phát hiện tràn bộ đệm.
  Ứng dụng không gọi trực tiếp.
- `thingspeak_sample_t`: gồm `temperature_c`, `pressure_hpa`,
  `humidity_percent`, lần lượt tương ứng field1, field2, field3.

Component phụ thuộc FreeRTOS, `esp_http_client` và `mbedtls`.
`main/CMakeLists.txt` chỉ cần khai báo phụ thuộc `thingspeak` và include
`thingspeak.h`. Bật certificate bundle để xác thực HTTPS.
