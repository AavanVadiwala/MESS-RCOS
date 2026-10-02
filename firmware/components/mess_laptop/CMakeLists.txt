idf_component_register(SRCS "mess_laptop.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES mess_commands mess_i2ctools mess_comm mess_wifi esp_http_server lwip console freertos)
