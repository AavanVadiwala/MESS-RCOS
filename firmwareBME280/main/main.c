#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define I2C_PORT I2C_NUM_0
#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22

// 0x76 if the SDO pin is tied to GND, 0x77 if tied to VDD.
// (Most breakout boards use 0x76 by default.)
#define BME280_ADDRESS 0x76

// Registers
#define BME280_REG_CALIB_00 0x88 // 0x88..0xA1 (26 bytes)
#define BME280_REG_ID       0xD0
#define BME280_REG_RESET    0xE0
#define BME280_REG_CALIB_26 0xE1 // 0xE1..0xE7 (7 bytes)
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS   0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG   0xF5
#define BME280_REG_DATA     0xF7 // 0xF7..0xFE (8 bytes)

#define BME280_CHIP_ID      0x60 // BMP280 reports 0x58 (no humidity)

#define I2C_TIMEOUT_MS 1000

// Factory calibration coefficients, read once from the sensor.
typedef struct
{
    uint16_t T1;
    int16_t T2, T3;
    uint16_t P1;
    int16_t P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t H1;
    int16_t H2;
    uint8_t H3;
    int16_t H4, H5;
    int8_t H6;
} bme280_calib_t;

static bme280_calib_t calib;
static int32_t t_fine;

static esp_err_t bme280_read(i2c_master_dev_handle_t dev, uint8_t reg,
                             uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

static esp_err_t bme280_write(i2c_master_dev_handle_t dev, uint8_t reg,
                              uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t bme280_read_calibration(i2c_master_dev_handle_t dev)
{
    uint8_t a[26];
    uint8_t b[7];

    esp_err_t err = bme280_read(dev, BME280_REG_CALIB_00, a, sizeof(a));
    if (err != ESP_OK) return err;
    err = bme280_read(dev, BME280_REG_CALIB_26, b, sizeof(b));
    if (err != ESP_OK) return err;

    calib.T1 = (uint16_t)(a[1] << 8 | a[0]);
    calib.T2 = (int16_t)(a[3] << 8 | a[2]);
    calib.T3 = (int16_t)(a[5] << 8 | a[4]);

    calib.P1 = (uint16_t)(a[7] << 8 | a[6]);
    calib.P2 = (int16_t)(a[9] << 8 | a[8]);
    calib.P3 = (int16_t)(a[11] << 8 | a[10]);
    calib.P4 = (int16_t)(a[13] << 8 | a[12]);
    calib.P5 = (int16_t)(a[15] << 8 | a[14]);
    calib.P6 = (int16_t)(a[17] << 8 | a[16]);
    calib.P7 = (int16_t)(a[19] << 8 | a[18]);
    calib.P8 = (int16_t)(a[21] << 8 | a[20]);
    calib.P9 = (int16_t)(a[23] << 8 | a[22]);

    calib.H1 = a[25]; // register 0xA1
    calib.H2 = (int16_t)(b[1] << 8 | b[0]);
    calib.H3 = b[2];
    calib.H4 = (int16_t)(((int8_t)b[3] << 4) | (b[4] & 0x0F));
    calib.H5 = (int16_t)(((int8_t)b[5] << 4) | (b[4] >> 4));
    calib.H6 = (int8_t)b[6];

    return ESP_OK;
}

// Returns temperature in hundredths of a degree C (e.g. 2512 = 25.12 C).
static int32_t compensate_temperature(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib.T1 << 1))) *
                    (int32_t)calib.T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)calib.T1) *
                      ((adc_T >> 4) - (int32_t)calib.T1)) >> 12) *
                    (int32_t)calib.T3) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}

// Returns pressure in Pa as Q24.8 fixed point (divide by 256 for Pa).
static uint32_t compensate_pressure(int32_t adc_P)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)calib.P6;
    var2 += (var1 * (int64_t)calib.P5) << 17;
    var2 += ((int64_t)calib.P4) << 35;
    var1 = ((var1 * var1 * (int64_t)calib.P3) >> 8) +
           ((var1 * (int64_t)calib.P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)calib.P1) >> 33;

    if (var1 == 0)
    {
        return 0; // avoid divide by zero
    }

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib.P7) << 4);
    return (uint32_t)p;
}

// Returns relative humidity in %RH as Q22.10 fixed point (divide by 1024).
static uint32_t compensate_humidity(int32_t adc_H)
{
    int32_t v = t_fine - 76800;
    v = (((((adc_H << 14) - (((int32_t)calib.H4) << 20) -
            (((int32_t)calib.H5) * v)) + 16384) >> 15) *
         (((((((v * (int32_t)calib.H6) >> 10) *
              (((v * (int32_t)calib.H3) >> 11) + 32768)) >> 10) + 2097152) *
           (int32_t)calib.H2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)calib.H1) >> 4);
    v = (v < 0) ? 0 : v;
    v = (v > 419430400) ? 419430400 : v;
    return (uint32_t)(v >> 12);
}

void app_main(void)
{
    i2c_master_bus_handle_t bus_handle;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    ESP_ERROR_CHECK(
        i2c_new_master_bus(&bus_config, &bus_handle)
    );

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_ADDRESS,
        .scl_speed_hz = 100000
    };

    i2c_master_dev_handle_t sensor_handle;

    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(
            bus_handle,
            &device_config,
            &sensor_handle
        )
    );

    // Verify we're actually talking to a BME280.
    uint8_t chip_id = 0;
    ESP_ERROR_CHECK(bme280_read(sensor_handle, BME280_REG_ID, &chip_id, 1));
    if (chip_id != BME280_CHIP_ID)
    {
        printf("Unexpected chip ID 0x%02X (expected 0x%02X).%s\n",
               chip_id, BME280_CHIP_ID,
               chip_id == 0x58 ? " That's a BMP280, which has no humidity sensor." : "");
        return;
    }

    // Soft reset, then give the sensor time to load its NVM data.
    ESP_ERROR_CHECK(bme280_write(sensor_handle, BME280_REG_RESET, 0xB6));
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(bme280_read_calibration(sensor_handle));

    // Humidity oversampling x1 (must be written before ctrl_meas).
    ESP_ERROR_CHECK(bme280_write(sensor_handle, BME280_REG_CTRL_HUM, 0x01));
    // Standby 1000 ms, IIR filter x4.
    ESP_ERROR_CHECK(bme280_write(sensor_handle, BME280_REG_CONFIG, 0xA8));
    // Temp oversampling x2, pressure oversampling x16, normal mode.
    ESP_ERROR_CHECK(bme280_write(sensor_handle, BME280_REG_CTRL_MEAS, 0x57));

    printf("BME280 sensor started!\n");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint8_t d[8];
        esp_err_t result = bme280_read(sensor_handle, BME280_REG_DATA,
                                       d, sizeof(d));

        if (result == ESP_OK)
        {
            int32_t adc_P = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
            int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
            int32_t adc_H = ((int32_t)d[6] << 8) | d[7];

            // Temperature must be computed first: it sets t_fine.
            float temp_c = compensate_temperature(adc_T) / 100.0f;
            float pressure_hpa = compensate_pressure(adc_P) / 256.0f / 100.0f;
            float humidity = compensate_humidity(adc_H) / 1024.0f;

            printf("Temperature: %.2f C, Humidity: %.2f %%RH, Pressure: %.2f hPa\n",
                   temp_c, humidity, pressure_hpa);
        }
        else
        {
            printf("Sensor read failed: %s\n", esp_err_to_name(result));
        }
    }
}