idf_component_register(SRCS "cmd_i2ctools.c" INCLUDE_DIRS "include" REQUIRES esp_common PRIV_REQUIRES esp_driver_i2c esp_driver_gpio console freertos)
