idf_component_register(SRCS "sensor_service.c"
                       INCLUDE_DIRS "."
                       REQUIRES data_pool freertos
                       PRIV_REQUIRES app_config bmp180 dht11 signal_filter esp_timer)
