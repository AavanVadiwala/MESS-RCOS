idf_component_register(SRCS "mess_hotspot.c" INCLUDE_DIRS "include"
    REQUIRES mess_mesh
    PRIV_REQUIRES esp_wifi esp_netif esp_http_server freertos esp_timer
    EMBED_TXTFILES "index.html")
