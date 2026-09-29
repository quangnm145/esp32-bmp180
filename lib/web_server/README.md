# Web server

`web_server_start()` khởi tạo HTTP server và các route có Basic Auth.
`web/index.html` được nhúng vào firmware qua `EMBED_TXTFILES`.

## Tab Cảm biến

Hiển thị năm biểu đồ: nhiệt độ BMP180, áp suất, độ cao, độ ẩm DHT11
và nhiệt độ DHT11. Giá trị mới nhất và đơn vị nằm phía trên mỗi biểu đồ.
Canvas không dùng CDN, hoạt động khi kết nối trực tiếp vào Wi-Fi của ESP32.

Trang dùng chung yêu cầu `/api/status` mỗi 3 giây với tab Trạng thái.
Chỉ thêm điểm khi `sensor.seq` thay đổi; không tăng tần suất đo phần cứng.
Khi mở tab, gọi `/api/sensors/history` để lấy 50 mẫu mới nhất từ ThingSpeak.
Lịch sử được làm mới mỗi phút khi tab đang mở; có nút tải lại (vẫn dùng cache
ESP32 nếu cache chưa hết hạn). Giữ thêm tối đa 120 mẫu đo trực tiếp.
Các mẫu local cũ hơn hoặc cách mẫu cloud mới nhất không quá 1 giây được loại
khỏi biểu đồ ghép để tránh lặp do sai số timestamp. Cả năm biểu đồ đều dùng
lịch sử cloud ghép với số đo trực tiếp.
Tải lại trang sẽ nạp lại lịch sử đã gửi thành công lên ThingSpeak. Mẫu chưa
gửi lên cloud chỉ có trong phiên trình duyệt. Không tiêu thụ hàng đợi upload.

Ánh xạ theo dữ liệu firmware (không theo nhãn channel): field1 = nhiệt độ
BMP180, field2 = áp suất hPa, field3 = độ ẩm %, field4 = nhiệt độ DHT11,
field5 = độ cao (m). Mẫu cũ không có field5 được giữ là null, không vẽ thành 0.

- `recordSensor(j)`: nhận status, cập nhật giá trị, loại mẫu trùng, giới hạn lịch sử.
- `loadHistory(force)`: tải lịch sử, chờ phản hồi 202 bằng polling 1 giây,
  loại entry ID trùng, chuyển field dạng chuỗi sang số và giữ dữ liệu cũ nếu lỗi.
- `rebuildHistory()`: ghép lịch sử cloud và mẫu local mới hơn, sắp theo thời gian.
- `readField(v)`: chuyển số hợp lệ; trả null cho trường rỗng hoặc không phải số.
- `chartValue(s, f)`: chỉ nhận số hữu hạn khi cờ cảm biến tương ứng hợp lệ.
- `drawCharts()`: vẽ đường, điểm, trục giá trị và thời gian; tự co giãn và đổi màu theo giao diện.
- `status()`: dùng chung request đang chạy để tránh nhiều yêu cầu status chồng nhau.

Mẫu lỗi tạo khoảng trống; mẫu bị bỏ lỡ không được nối đường. Mất mạng giữ
biểu đồ cũ và hiển thị thông báo thử lại. Thời gian đo trên trục được ước lượng
từ đồng hồ trình duyệt trừ `sensor.age_s`; đây không phải timestamp đã đồng bộ của thiết bị.
API chỉ cung cấp mẫu mới nhất, nên khi chu kỳ đo ngắn hơn polling hoặc tab bị
trình duyệt tạm dừng, biểu đồ có thể không thu được mọi mẫu.

## Tải lịch sử trên ESP32

`sensor_history.c` cung cấp `sensor_history_get()` cho route đã được xác thực
Basic Auth. Lần đầu trả HTTP 202 và khởi tạo một worker, không chặn HTTP server
trong khi chờ HTTPS. Worker dùng certificate bundle, timeout 10 giây cho I/O,
giới hạn body 32 KiB và kiểm tra JSON có mảng `feeds`.

- `fetch_history()`: GET 50 mẫu, cập nhật cache dưới mutex, giải phóng HTTP client.
- `collect_response()`: ghép các chunk vào bộ đệm có giới hạn, phát hiện tràn.
- `sensor_history_get()`: trả 202 khi đang tải, 200 khi sẵn sàng, 503 nếu lỗi.

Cache thành công giữ 60 giây; lỗi cho phép thử lại sau 15 giây. Nhiều trình
duyệt dùng chung một worker/cache. Chỉ ESP32 cần kết nối Internet; trình duyệt
có thể kết nối trực tiếp AP cấu hình. Không thay đổi tác vụ upload.

Đặt `CONFIG_APP_TS_HISTORY_CHANNEL_ID` và `CONFIG_APP_TS_HISTORY_READ_KEY`
trong `sdkconfig.secrets` (không commit), hoặc menuconfig → IoT Device Configuration.
Cấu hình đã thiết lập cho channel 3515047. Khi sửa giá trị mặc định, lưu ý
`sdkconfig` hiện tại được ưu tiên. Read Key gửi bằng header THINGSPEAKAPIKEY,
không nhúng vào HTML hay trả về qua endpoint lịch sử.

Tài liệu API: https://www.mathworks.com/help/thingspeak/readdata.html
và https://www.mathworks.com/help/thingspeak/http-headers.html
