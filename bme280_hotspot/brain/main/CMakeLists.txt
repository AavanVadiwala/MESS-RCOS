idf_component_register(SRCS "gateway_main.c" "uplink.c" INCLUDE_DIRS "."
    EMBED_FILES "web/index.html"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_http_server esp_timer
                  esp_http_client mbedtls wpa_supplicant)
