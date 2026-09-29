#pragma once

#include "esp_err.h"

typedef struct {
    const char *username;   /* HTTP Basic Auth, vd "admin" */
    const char *password;   /* vd "admin" */
} web_server_config_t;

/* Khoi dong trang cau hinh (esp_http_server, cong 80). Moi URI deu yeu cau
 * Basic Auth. Goi sau wifi_manager_start(). */
esp_err_t web_server_start(const web_server_config_t *config);
