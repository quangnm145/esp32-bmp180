idf_component_register(SRCS "thingspeak.c"
                       INCLUDE_DIRS "."
                       REQUIRES freertos
                       PRIV_REQUIRES app_config data_pool esp_http_client esp_netif
                                     esp_timer json mbedtls)
