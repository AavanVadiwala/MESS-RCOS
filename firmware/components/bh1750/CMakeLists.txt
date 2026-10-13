idf_component_register(SRCS "bh1750.c" INCLUDE_DIRS "include"
    REQUIRES sensor_types esp_driver_i2c esp_driver_gpio)
