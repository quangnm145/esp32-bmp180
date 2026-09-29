# Ứng dụng chính

`main.c` điều phối ba component trong `lib`: `bmp180`, `dht11`, `thingspeak`.

| Hàm | Chức năng |
| --- | --- |
| `app_main()` | Khởi tạo cảm biến, queue một phần tử và Wi-Fi. Đọc cảm biến khoảng mỗi 5 giây theo cấu hình mặc định; tạo task ThingSpeak một lần khi có IP; đưa bộ số đo hợp lệ mới nhất vào queue. |
| `wifi_init()` | Khởi tạo NVS, giao diện mạng, event loop và Wi-Fi station; lấy SSID/password từ menuconfig rồi bắt đầu kết nối. |
| `wifi_event_handler()` | Bắt sự kiện bắt đầu Wi-Fi, mất kết nối và nhận IP; cập nhật bit trạng thái mạng, yêu cầu kết nối lại khi mất mạng. |

Chạy `idf.py menuconfig` → **ThingSpeak Configuration** để nhập Wi-Fi và
chân DHT11. Mục **BMP180 Configuration** cấu hình chân I2C và chu kỳ đo.
Nếu để SSID trống, ứng dụng chỉ đọc cảm biến.

Chu kỳ đo và chu kỳ gửi độc lập: log cảm biến khoảng 5 giây/lần;
ThingSpeak chờ 30 giây sau mỗi lần gửi HTTP. Không tạo thêm task gửi trong
vòng lặp vì mỗi task sẽ có bộ đếm thời gian riêng.

Xem chi tiết API tại [BMP180](../lib/bmp180/README.md),
[DHT11](../lib/dht11/README.md), [ThingSpeak](../lib/thingspeak/README.md).
