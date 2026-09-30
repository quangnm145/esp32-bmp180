# Sensor service

`sensor_service_start()` tạo task đọc BMP180/DHT11, lọc nhiễu và đóng gói mẫu
đầu vào data_pool sau khoảng 30 giây khởi động. Các mẫu tiếp theo được đóng
gói theo chu kỳ cấu hình, tính từ mẫu đầu. `sensor_service_get_latest()` trả bản sao
mẫu mới nhất cho web; cờ `valid` cho biết cảm biến nào có dữ liệu hợp lệ.

## Hiệu chỉnh theo số đo tham chiếu

Áp dụng offset một lần trong `pack_sample()`, sau bộ lọc, trước khi đưa vào
data_pool và cập nhật mẫu mới nhất:

| Đại lượng | Công thức |
| --- | --- |
| Nhiệt độ BMP180 | Giá trị lọc − 0,90 °C |
| Nhiệt độ DHT11 | Giá trị lọc − 3,50 °C |
| Độ ẩm DHT11 | Giá trị lọc + 15,20 điểm %RH, giới hạn 0–100% |

Offset độ ẩm ban đầu +21,80 cho kết quả 83,6%RH khi mốc tham chiếu là
77%RH. Giảm 6,60 điểm %RH nên offset mới là +15,20. Đây là hiệu chỉnh
theo một điểm đo mới; nên đối chiếu thêm ở vài mức độ ẩm khác.

Các hằng số offset đặt ở đầu `sensor_service.c`. Offset chỉ áp dụng với cảm
biến hợp lệ và dữ liệu thật; chế độ `CONFIG_APP_SENSOR_FAKE_DATA` bỏ qua bù
để giữ nguyên dữ liệu kiểm tra bộ lọc.

Web, log mẫu và ThingSpeak đều sử dụng cùng mẫu đã hiệu chỉnh. Driver BMP180
vẫn dùng nhiệt độ gốc trong thuật toán bù áp suất của chip; áp suất
không bị đổi bởi offset nhiệt độ hiển thị. Không bù thêm ở web hay lúc upload.
Lịch sử cũ trên ThingSpeak không được sửa hồi tố.

Các offset nhiệt độ được suy ra từ khoảng đo tham chiếu 11h–19h; sai số còn lại
trong bảng không phải cam kết độ chính xác ở nhiệt độ/độ ẩm khác.

## Hàm nội bộ

- `read_bmp180()` / `read_dht11()`: đọc cảm biến hoặc tạo dữ liệu giả, cập nhật bộ lọc.
- `is_fresh()` / `drop_stale()`: phát hiện dữ liệu quá cũ, reset bộ lọc khi mất cảm biến.
- `pack_sample()`: lấy giá trị lọc, hiệu chỉnh và đặt cờ hợp lệ.
- `sensor_task()`: xử lý tick, đóng gói mẫu và thông báo cho task gửi dữ liệu.
- `fake_noise()` / `fake_level()`: sinh dữ liệu kiểm tra khi bật chế độ giả lập.
