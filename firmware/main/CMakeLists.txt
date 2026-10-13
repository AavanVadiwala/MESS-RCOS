idf_component_register(SRCS "app_main.c" PRIV_REQUIRES
    mess_broadcast bme280 sgp30 bh1750 freertos esp_timer esp_hw_support)
