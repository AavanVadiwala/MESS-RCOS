#include "sgp30.h"

/* TODO:
 * Configure I2C, validate identity and initialize the IAQ algorithm with its warm-up state.
 * Measure every second, validate CRCs and return CO2-equivalent ppm and TVOC ppb; add baseline/humidity handling.
 */
esp_err_t sgp30_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sgp30_read(sensor_reading_t *reading)
{
    if (!reading) return ESP_ERR_INVALID_ARG;
    *reading = (sensor_reading_t){.status = ESP_ERR_NOT_SUPPORTED, .field_test_name = "SGP30"};
    return ESP_ERR_NOT_SUPPORTED;
}
