#include "bme280.h"
#include <math.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BME280_I2C_PORT I2C_NUM_0
#define BME280_SDA_PIN GPIO_NUM_21
#define BME280_SCL_PIN GPIO_NUM_22
#define BME280_ADDRESS_PRIMARY 0x76
#define BME280_ADDRESS_SECONDARY 0x77
#define BME280_CHIP_ID 0x60
#define BME280_REG_CALIB_TP 0x88
#define BME280_REG_CALIB_H1 0xA1
#define BME280_REG_CALIB_H2 0xE1
#define BME280_REG_ID 0xD0
#define BME280_REG_RESET 0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_DATA 0xF7
#define BME280_CONVERSION_TIME_MS 20

typedef struct {
    uint16_t t1;
    int16_t t2, t3;
    uint16_t p1;
    int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1;
    int16_t h2;
    uint8_t h3;
    int16_t h4, h5;
    int8_t h6;
} bme280_calibration_t;

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t sensor_handle;
static bme280_calibration_t calibration;

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static int16_t read_i16_le(const uint8_t *data)
{
    return (int16_t)read_u16_le(data);
}

static int16_t sign_extend_12(uint16_t value)
{
    return (int16_t)(value & 0x0800 ? value | 0xF000 : value);
}

static esp_err_t read_registers(i2c_master_dev_handle_t device,
    uint8_t register_address, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(device, &register_address, 1,
        data, length, 1000);
}

static esp_err_t write_register(uint8_t register_address, uint8_t value)
{
    uint8_t data[] = {register_address, value};
    return i2c_master_transmit(sensor_handle, data, sizeof(data), 1000);
}

static esp_err_t get_or_create_bus(void)
{
    esp_err_t error = i2c_master_get_bus_handle(BME280_I2C_PORT, &bus_handle);
    if (error == ESP_OK) return ESP_OK;
    if (error != ESP_ERR_INVALID_STATE) return error;

    i2c_master_bus_config_t config = {
        .i2c_port = BME280_I2C_PORT,
        .sda_io_num = BME280_SDA_PIN,
        .scl_io_num = BME280_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, &bus_handle);
}

static esp_err_t load_calibration(void)
{
    uint8_t tp[24], h1, humidity[7];
    esp_err_t error = read_registers(sensor_handle, BME280_REG_CALIB_TP, tp, sizeof(tp));
    if (error != ESP_OK) return error;
    error = read_registers(sensor_handle, BME280_REG_CALIB_H1, &h1, sizeof(h1));
    if (error != ESP_OK) return error;
    error = read_registers(sensor_handle, BME280_REG_CALIB_H2, humidity, sizeof(humidity));
    if (error != ESP_OK) return error;

    calibration = (bme280_calibration_t){
        .t1 = read_u16_le(&tp[0]),
        .t2 = read_i16_le(&tp[2]),
        .t3 = read_i16_le(&tp[4]),
        .p1 = read_u16_le(&tp[6]),
        .p2 = read_i16_le(&tp[8]),
        .p3 = read_i16_le(&tp[10]),
        .p4 = read_i16_le(&tp[12]),
        .p5 = read_i16_le(&tp[14]),
        .p6 = read_i16_le(&tp[16]),
        .p7 = read_i16_le(&tp[18]),
        .p8 = read_i16_le(&tp[20]),
        .p9 = read_i16_le(&tp[22]),
        .h1 = h1,
        .h2 = read_i16_le(&humidity[0]),
        .h3 = humidity[2],
        .h4 = sign_extend_12(((uint16_t)humidity[3] << 4) | (humidity[4] & 0x0F)),
        .h5 = sign_extend_12(((uint16_t)humidity[5] << 4) | (humidity[4] >> 4)),
        .h6 = (int8_t)humidity[6],
    };
    return ESP_OK;
}

esp_err_t bme280_init(void)
{
    if (sensor_handle) return ESP_OK;

    esp_err_t error = get_or_create_bus();
    if (error != ESP_OK) return error;

    const uint8_t addresses[] = {BME280_ADDRESS_PRIMARY, BME280_ADDRESS_SECONDARY};
    bool found = false;
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        error = i2c_master_probe(bus_handle, addresses[i], 1000);
        if (error == ESP_ERR_NOT_FOUND) continue;
        if (error != ESP_OK) return error;

        i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[i],
            .scl_speed_hz = 100000,
        };
        error = i2c_master_bus_add_device(bus_handle, &device_config, &sensor_handle);
        if (error != ESP_OK) return error;

        uint8_t chip_id = 0;
        error = read_registers(sensor_handle, BME280_REG_ID, &chip_id, sizeof(chip_id));
        if (error != ESP_OK) {
            esp_err_t remove_error = i2c_master_bus_rm_device(sensor_handle);
            sensor_handle = NULL;
            return remove_error == ESP_OK ? error : remove_error;
        }
        if (chip_id == BME280_CHIP_ID) {
            found = true;
            break;
        }

        esp_err_t remove_error = i2c_master_bus_rm_device(sensor_handle);
        sensor_handle = NULL;
        if (remove_error != ESP_OK) return remove_error;
    }
    if (!found) return ESP_ERR_NOT_FOUND;

    error = write_register(BME280_REG_RESET, 0xB6);
    if (error != ESP_OK) goto fail;
    vTaskDelay(pdMS_TO_TICKS(5));

    error = load_calibration();
    if (error != ESP_OK) goto fail;
    if (!calibration.p1) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }

    error = write_register(BME280_REG_CTRL_HUM, 0x01);
    if (error != ESP_OK) goto fail;
    error = write_register(BME280_REG_CTRL_MEAS, 0x25);
    if (error != ESP_OK) goto fail;
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(sensor_handle);
    sensor_handle = NULL;
    return error;
}

esp_err_t bme280_read(sensor_reading_t *reading)
{
    if (!reading) return ESP_ERR_INVALID_ARG;
    *reading = (sensor_reading_t){.status = ESP_FAIL};
    if (!sensor_handle) return reading->status = ESP_ERR_INVALID_STATE;

    esp_err_t error = write_register(BME280_REG_CTRL_HUM, 0x01);
    if (error != ESP_OK) return reading->status = error;
    error = write_register(BME280_REG_CTRL_MEAS, 0x25);
    if (error != ESP_OK) return reading->status = error;
    vTaskDelay(pdMS_TO_TICKS(BME280_CONVERSION_TIME_MS));

    uint8_t data[8];
    error = read_registers(sensor_handle, BME280_REG_DATA, data, sizeof(data));
    if (error != ESP_OK) return reading->status = error;

    int32_t adc_pressure = ((int32_t)data[0] << 12) |
        ((int32_t)data[1] << 4) | (data[2] >> 4);
    int32_t adc_temperature = ((int32_t)data[3] << 12) |
        ((int32_t)data[4] << 4) | (data[5] >> 4);
    int32_t adc_humidity = ((int32_t)data[6] << 8) | data[7];

    double var1 = (((adc_temperature / 16384.0) - (calibration.t1 / 1024.0)) *
        calibration.t2);
    double var2 = (((adc_temperature / 131072.0) - (calibration.t1 / 8192.0)) *
        ((adc_temperature / 131072.0) - (calibration.t1 / 8192.0))) * calibration.t3;
    double temperature_fine = var1 + var2;
    double temperature_c = temperature_fine / 5120.0;

    var1 = temperature_fine / 2.0 - 64000.0;
    var2 = var1 * var1 * calibration.p6 / 32768.0;
    var2 += var1 * calibration.p5 * 2.0;
    var2 = var2 / 4.0 + calibration.p4 * 65536.0;
    var1 = (calibration.p3 * var1 * var1 / 524288.0 +
        calibration.p2 * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0) * calibration.p1;
    if (var1 == 0.0) return reading->status = ESP_ERR_INVALID_RESPONSE;
    double pressure = 1048576.0 - adc_pressure;
    pressure = (pressure - var2 / 4096.0) * 6250.0 / var1;
    var1 = calibration.p9 * pressure * pressure / 2147483648.0;
    var2 = pressure * calibration.p8 / 32768.0;
    pressure += (var1 + var2 + calibration.p7) / 16.0;

    double humidity = temperature_fine - 76800.0;
    humidity = (adc_humidity - (calibration.h4 * 64.0 +
        calibration.h5 / 16384.0 * humidity)) *
        (calibration.h2 / 65536.0 *
        (1.0 + calibration.h6 / 67108864.0 * humidity *
        (1.0 + calibration.h3 / 67108864.0 * humidity)));
    humidity *= 1.0 - calibration.h1 * humidity / 524288.0;
    if (humidity < 0.0) humidity = 0.0;
    if (humidity > 100.0) humidity = 100.0;
    if (!isfinite(temperature_c) || !isfinite(pressure) || !isfinite(humidity))
        return reading->status = ESP_ERR_INVALID_RESPONSE;

    *reading = (sensor_reading_t){
        .status = ESP_OK,
        .count = 3,
        .values = {(float)temperature_c, (float)pressure, (float)humidity},
    };
    return ESP_OK;
}
