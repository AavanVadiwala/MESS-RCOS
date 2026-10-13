#pragma once
#include "sensor_types.h"
/* Success returns 1 values, in the units/order documented in README.md. */
esp_err_t bh1750_init(void);
esp_err_t bh1750_read(sensor_reading_t *reading);
