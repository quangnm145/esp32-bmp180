idf_component_register(SRCS "app_tick.c"
                       INCLUDE_DIRS "."
                       REQUIRES freertos
                       PRIV_REQUIRES esp_driver_gptimer)
