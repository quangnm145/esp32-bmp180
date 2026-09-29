idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_config status_led data_pool wifi_manager web_server
                                thingspeak sensor_service app_tick)
