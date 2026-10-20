idf_component_register(SRCS "bme280.c" INCLUDE_DIRS "include"
    REQUIRES sensor_types esp_driver_i2c esp_driver_gpio freertos)
