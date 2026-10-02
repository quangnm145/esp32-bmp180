# ThingSpeak worker

```c
esp_err_t thingspeak_worker_start(thingspeak_network_ready_fn network_ready,
                                  TaskHandle_t *out_task);
void thingspeak_get_status(thingspeak_status_t *out);
```

Worker được đánh thức bằng task notify:

- `THINGSPEAK_NOTIFY_TICK`: từ ISR `app_tick` mỗi 1 s
- `THINGSPEAK_NOTIFY_DATA`: `sensor_service` vừa đẩy object mới vào `data_pool`

Khi bật `CONFIG_APP_SENSOR_FAKE_DATA`, worker không gửi dữ liệu lên ThingSpeak;
mẫu giả vẫn nằm trong pool để web hiển thị.

Mỗi lần thức, worker chỉ gửi khi ThingSpeak được bật, có API key, `network_ready()`
trả về true, pool có dữ liệu và đã qua `THINGSPEAK_MIN_INTERVAL_MS` (15 s) từ lần gửi trước.
Mẫu khởi động được tạo sau khoảng 30 giây và gửi ngay khi Wi-Fi sẵn sàng;
chu kỳ gửi tiếp theo được tính từ mẫu đầu. Mẫu chờ trong pool nếu chưa có mạng.

| Điều kiện | Cách gửi |
|-----------|----------|
| Có Channel ID và đã đồng bộ SNTP | `POST /channels/<id>/bulk_update.json` toàn bộ pool, mỗi object có `created_at` UTC. Thành công: HTTP 202 `{"success":true}` |
| Còn lại | `GET /update` object mới nhất, bỏ các object cũ hơn |

Object chỉ bị xoá khỏi pool sau khi gửi thành công; lỗi mạng thì giữ lại để lần sau gửi tiếp.

Anh xạ field nằm trong bảng `FIELD_MAP` (`thingspeak.c`):
field1 nhiệt độ BMP180, field2 áp suất hPa, field3 độ ẩm, field4 nhiệt độ DHT11.
Áp dụng cho cả gửi đơn và bulk update. Firmware không gửi field5 hoặc status
độ cao; các bản ghi cũ trên ThingSpeak vẫn tồn tại nhưng web bỏ qua field5.
