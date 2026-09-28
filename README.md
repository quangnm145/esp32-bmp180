# ESP32 BMP180

Project ESP-IDF đọc cảm biến nhiệt độ và áp suất BMP180 qua giao tiếp I2C. Chương trình hiển thị nhiệt độ, áp suất và độ cao ước tính trên serial monitor theo chu kỳ cấu hình.

## Yêu cầu

- ESP-IDF 5.3 trở lên
- ESP32 development board
- Cảm biến BMP180
- Flash mặc định: 4 MB

Ví dụ mặc định sử dụng ESP32 classic với GPIO 25 và GPIO 26. Nếu sử dụng ESP32-C3 hoặc một dòng chip khác, cần chọn các GPIO hợp lệ trong `menuconfig`.

## Kết nối phần cứng

| BMP180 | ESP32 mặc định |
|--------|----------------|
| VCC    | 3V3            |
| GND    | GND            |
| SDA    | GPIO 25        |
| SCL    | GPIO 26        |

Địa chỉ I2C mặc định của BMP180 là `0x77`. Module thường đã có điện trở kéo lên I2C; nếu dùng cảm biến rời, cần bổ sung điện trở kéo lên SDA và SCL.

## Cấu hình

Mở giao diện cấu hình:

```bash
idf.py menuconfig
```

Trong menu **BMP180 Configuration** có thể thay đổi:

- `I2C SDA GPIO`: chân SDA, mặc định `25`
- `I2C SCL GPIO`: chân SCL, mặc định `26`
- `Sea-level reference pressure`: áp suất tham chiếu để tính độ cao, mặc định `101325 Pa`
- `Measurement interval`: chu kỳ đọc cảm biến, mặc định `2000 ms`

Flash size được đặt mặc định là 4 MB trong `sdkconfig.defaults`. Có thể kiểm tra tại:

```text
Serial flasher config -> Flash size -> 4 MB
```

## Chọn target

Với ESP32 classic:

```bash
idf.py set-target esp32
```

Sau khi đổi target, mở lại `menuconfig` để kiểm tra chân GPIO và flash size.

## Build và nạp chương trình

```bash
idf.py build
idf.py -p COMx flash monitor
```

Thay `COMx` bằng cổng serial của board, ví dụ `COM5`. Nhấn `Ctrl+]` để thoát monitor.

Kết quả dự kiến:

```text
Nhiet do = 27.40 *C
Ap suat  = 1008.52 hPa
Do cao   = 40.12 met
--------------------------------
```

## Cấu trúc project

```text
esp32-bmp180/
|-- CMakeLists.txt
|-- sdkconfig.defaults
|-- lib/
|   `-- bmp180/
|       |-- CMakeLists.txt
|       |-- bmp180.c
|       `-- bmp180.h
|-- main/
|   |-- CMakeLists.txt
|   |-- Kconfig.projbuild
|   `-- esp32_bmp180_main.c
`-- README.md
```

Driver BMP180 được đóng gói thành component `bmp180`. Component `main` sử dụng các API:

```c
bmp180_init(sda_gpio, scl_gpio);
bmp180_read_temperature(&temperature_c);
bmp180_read_pressure(&pressure_pa);
bmp180_read_altitude(reference_pressure_pa, &altitude_m);
```

## Xử lý sự cố

### Không tìm thấy BMP180

- Kiểm tra VCC đang nối với `3V3`, không dùng 5 V nếu module không hỗ trợ.
- Kiểm tra đúng chân SDA/SCL đã chọn trong `menuconfig`.
- Kiểm tra địa chỉ `0x77` bằng chương trình quét I2C.
- Đảm bảo SDA và SCL có điện trở kéo lên.

### GPIO 25/26 không hoạt động

GPIO 25 và 26 là cấu hình mặc định cho ESP32 classic. ESP32-C3 không có hai GPIO này; hãy chọn các chân hợp lệ của board trong **BMP180 Configuration**.

### Giá trị độ cao chưa chính xác

Độ cao được suy ra từ áp suất tham chiếu. Hãy cập nhật `Sea-level reference pressure` theo áp suất mực nước biển tại vị trí và thời điểm đo để có kết quả tốt hơn.
