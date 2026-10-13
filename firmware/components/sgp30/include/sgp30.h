#pragma once
#include "sensor_types.h"
/* Success returns 2 values, in the units/order documented in README.md. */
esp_err_t sgp30_init(void);
esp_err_t sgp30_read(sensor_reading_t *reading);
