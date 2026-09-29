idf_component_register(SRCS "thingspeak.c"
                       INCLUDE_DIRS "."
                       REQUIRES freertos
                       PRIV_REQUIRES esp_http_client mbedtls)
