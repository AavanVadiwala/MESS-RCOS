idf_component_register(SRCS "mess_i2c.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_driver_i2c esp_driver_gpio)
