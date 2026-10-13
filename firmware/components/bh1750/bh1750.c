#include "bh1750.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BH1750_I2C_PORT I2C_NUM_0
#define BH1750_SDA_PIN GPIO_NUM_21
#define BH1750_SCL_PIN GPIO_NUM_22
#define BH1750_I2C_ADDRESS 0x23
#define BH1750_MEASUREMENT_MODE 0x20
#define BH1750_CONVERSION_TIME_MS 180

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t sensor_handle;

esp_err_t bh1750_init(void)
{
    if (sensor_handle) return ESP_OK;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = BH1750_I2C_PORT,
        .sda_io_num = BH1750_SDA_PIN,
        .scl_io_num = BH1750_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t error = i2c_new_master_bus(&bus_config, &bus_handle);
    if (error != ESP_OK) return error;

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BH1750_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    error = i2c_master_bus_add_device(bus_handle, &device_config, &sensor_handle);
    if (error != ESP_OK) {
        i2c_del_master_bus(bus_handle);
        bus_handle = NULL;
        return error;
    }
    return ESP_OK;
}

esp_err_t bh1750_read(sensor_reading_t *reading)
{
    if (!reading) return ESP_ERR_INVALID_ARG;
    *reading = (sensor_reading_t){.status = ESP_FAIL};
    if (!sensor_handle) return reading->status = ESP_ERR_INVALID_STATE;

    const uint8_t command = BH1750_MEASUREMENT_MODE;
    esp_err_t error = i2c_master_transmit(sensor_handle, &command, sizeof(command), 1000);
    if (error != ESP_OK) return reading->status = error;

    vTaskDelay(pdMS_TO_TICKS(BH1750_CONVERSION_TIME_MS));

    uint8_t data[2];
    error = i2c_master_receive(sensor_handle, data, sizeof(data), 1000);
    if (error != ESP_OK) return reading->status = error;

    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    *reading = (sensor_reading_t){
        .status = ESP_OK,
        .count = 1,
        .values = {raw / 1.2f},
    };
    return ESP_OK;
}
