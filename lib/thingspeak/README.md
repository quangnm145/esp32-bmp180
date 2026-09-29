# ThingSpeak worker

```c
esp_err_t thingspeak_worker_start(thingspeak_network_ready_fn network_ready,
                                  TaskHandle_t *out_task);
void thingspeak_get_status(thingspeak_status_t *out);
```

Worker được đánh thức bằng task notify:

- `THINGSPEAK_NOTIFY_TICK`: từ ISR `app_tick` mỗi 1 s
- `THINGSPEAK_NOTIFY_DATA`: `sensor_service` vừa đẩy object mới vào `data_pool`

Mỗi lần thức, worker chỉ gửi khi ThingSpeak được bật, có API key, `network_ready()`
trả về true, pool có dữ liệu và đã qua `THINGSPEAK_MIN_INTERVAL_MS` (15 s) từ lần gửi trước.

| Điều kiện | Cách gửi |
|-----------|----------|
| Có Channel ID và đã đồng bộ SNTP | `POST /channels/<id>/bulk_update.json` toàn bộ pool, mỗi object có `created_at` UTC. Thành công: HTTP 202 `{"success":true}` |
| Còn lại | `GET /update` object mới nhất, bỏ các object cũ hơn |

Object chỉ bị xoá khỏi pool sau khi gửi thành công; lỗi mạng thì giữ lại để lần sau gửi tiếp.

Anh xạ field nằm trong bảng `FIELD_MAP` (`thingspeak.c`):
field1 nhiệt độ BMP180, field2 áp suất hPa, field3 độ ẩm, field4 nhiệt độ DHT11,
field5 độ cao (m). Độ cao chỉ được gửi khi mẫu BMP180 hợp lệ, áp dụng cả
gửi đơn và bulk update. Bật field5 trên channel trước khi dùng firmware này.
