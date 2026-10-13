#include "bme280.h"

/* TODO:
 * Configure I2C and validate the chip ID; load factory calibration and select a measurement mode.
 * Acquire raw measurements with conversion timing, then compensate into C, Pa and %RH.
 */
esp_err_t bme280_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bme280_read(sensor_reading_t *reading)
{
    if (!reading) return ESP_ERR_INVALID_ARG;
    *reading = (sensor_reading_t){.status = ESP_ERR_NOT_SUPPORTED, .field_test_name = "BME280"};
    return ESP_ERR_NOT_SUPPORTED;
}
