#pragma once
#include "esp_http_server.h"
/* Caller must authenticate the request first. Downloads run in a worker task. */
esp_err_t sensor_history_get(httpd_req_t *req);
