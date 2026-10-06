idf_component_register(SRCS "mess_comm.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES mess_commands mess_wifi esp_wifi esp_event esp_netif nvs_flash console freertos esp_app_format esp_hw_support)
