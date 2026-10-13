idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                        REQUIRES "esp_driver_i2c" )
# idf_component_register(
#     SRCS "main.c"
#     INCLUDE_DIRS "."
#     REQUIRES "esp_driver_i2c"  # <-- Required for I2C Master in v6.x
# )