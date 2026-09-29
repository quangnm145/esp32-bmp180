idf_component_register(SRCS "web_server.c" "sensor_history.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES "web/index.html"
                       REQUIRES esp_http_server
                       PRIV_REQUIRES app_config wifi_manager sensor_service data_pool thingspeak
                                     json mbedtls esp_timer esp_system esp_wifi esp_http_client)
