# Ứng dụng chính

`app_main()` khởi tạo theo thứ tự:

1. `app_config_init()` – NVS và cấu hình đã lưu
2. `status_led_init()` – LED ở trạng thái mất kết nối
3. `data_pool_init()`
4. `wifi_manager_start()` – AP `IOT_Device` + kết nối Wi-Fi đã lưu; callback đổi LED
5. `web_server_start()` – trang cấu hình, tài khoản từ Kconfig
6. `thingspeak_worker_start()` – worker + SNTP
7. `sensor_service_start()` – task cảm biến, báo worker khi có object mới
8. `app_tick_subscribe()` cho hai task rồi `app_tick_start(1000)`

Sau đó task chính in nhịp tim mỗi 60 s gồm IP Wi-Fi, IP AP và số object trong pool.

Cấu hình build trong `idf.py menuconfig`:

- **BMP180 Configuration**: chân SDA/SCL, áp suất mực nước biển
- **ThingSpeak Configuration**: chân DHT11
- **IoT Device Configuration**: tên/IP AP, tài khoản web, GPIO LED, chu kỳ mặc định,
  ThingSpeak/Wi-Fi mặc định (điền trong `sdkconfig.secrets`), chế độ fake data
