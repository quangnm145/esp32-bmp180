# Các component thư viện

- [bmp180](bmp180/README.md): đọc nhiệt độ, áp suất và tính độ cao qua I2C.
- [dht11](dht11/README.md): đọc nhiệt độ và độ ẩm qua GPIO.
- [thingspeak](thingspeak/README.md): task gửi bộ số đo qua HTTPS, chờ 30 giây giữa các yêu cầu.

Mỗi thư mục có `.c`, `.h`, `CMakeLists.txt` và `README.md` giải thích API.
CMake ở thư mục gốc khai báo `EXTRA_COMPONENT_DIRS` trỏ tới `lib`;
ứng dụng khai báo các component cần dùng trong `main/CMakeLists.txt`.
