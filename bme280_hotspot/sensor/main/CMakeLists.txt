idf_component_register(SRCS "bme280_main.c" "wifi_sender.c" INCLUDE_DIRS "."
    PRIV_REQUIRES bme280 esp_driver_i2c esp_rom esp_wifi esp_event esp_netif nvs_flash esp_http_client)
