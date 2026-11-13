idf_component_register(SRCS "bme280.c" INCLUDE_DIRS "include" REQUIRES sensor_types
    PRIV_REQUIRES esp_driver_i2c freertos)
