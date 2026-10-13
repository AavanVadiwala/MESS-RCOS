#pragma once
#include "sensor_types.h"
/* Success returns 3 values, in the units/order documented in README.md. */
esp_err_t bme280_init(void);
esp_err_t bme280_read(sensor_reading_t *reading);
