idf_component_register(SRCS "mess_broadcast.c" INCLUDE_DIRS "include"
    REQUIRES mess_mesh
    PRIV_REQUIRES mess_hotspot esp_wifi esp_event esp_netif nvs_flash freertos esp_hw_support esp_timer)
