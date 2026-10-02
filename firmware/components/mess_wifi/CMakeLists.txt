idf_component_register(SRCS "mess_wifi.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash)
