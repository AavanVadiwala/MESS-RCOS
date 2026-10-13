#include "bh1750.h"

/* TODO:
 * Configure I2C and the selected device address; choose a measurement mode and measurement time.
 * Respect conversion timing and convert the raw result into lux; handle bus errors and recovery.
 */
esp_err_t bh1750_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bh1750_read(sensor_reading_t *reading)
{
    if (!reading) return ESP_ERR_INVALID_ARG;
    *reading = (sensor_reading_t){.status = ESP_ERR_NOT_SUPPORTED, .field_test_name = "BH1750"};
    return ESP_ERR_NOT_SUPPORTED;
}
