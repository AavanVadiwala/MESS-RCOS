#include "bme280.h"
#include <stdint.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Bosch BME280 datasheet (BST-BME280-DS002): registers, forced mode, and the
 * floating-point compensation formulas from section 8.1. Values are returned
 * as C, Pa and %RH, in that order. */
#define REG_CALIB_TP 0x88 /* 0x88..0xA1: temperature/pressure calibration, then H1 */
#define REG_CHIP_ID 0xD0
#define REG_RESET 0xE0
#define REG_CALIB_H 0xE1  /* 0xE1..0xE7: humidity calibration */
#define REG_CTRL_HUM 0xF2
#define REG_STATUS 0xF3
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG 0xF5
#define REG_DATA 0xF7     /* 0xF7..0xFE: pressure, temperature, humidity */
#define CHIP_ID 0x60
#define TIMEOUT_MS 100

static const char *TAG = "bme280";
static i2c_master_dev_handle_t device;
static struct {
    uint16_t t1; int16_t t2, t3;
    uint16_t p1; int16_t p2, p3, p4, p5, p6, p7, p8, p9;
    uint8_t h1, h3; int16_t h2, h4, h5; int8_t h6;
} cal;

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(device, &reg, 1, data, length, TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(device, data, sizeof(data), TIMEOUT_MS);
}

/* The BME280 component owns the bus and shares it with the scanner. */
static esp_err_t get_bus(i2c_master_bus_handle_t *bus)
{
    static i2c_master_bus_handle_t master_bus;
    const i2c_master_bus_config_t config = {
        .i2c_port = CONFIG_MESS_I2C_PORT,
        .sda_io_num = CONFIG_MESS_I2C_SDA,
        .scl_io_num = CONFIG_MESS_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* Weak; breakout pull-ups do the real work. */
    };
    if (!master_bus) {
        esp_err_t error = i2c_new_master_bus(&config, &master_bus);
        if (error != ESP_OK) return error;
    }
    *bus = master_bus;
    return ESP_OK;
}

esp_err_t mess_i2c_scan(void)
{
    printf("I2C scan on port %d (SDA GPIO %d, SCL GPIO %d)\n",
        CONFIG_MESS_I2C_PORT, CONFIG_MESS_I2C_SDA, CONFIG_MESS_I2C_SCL);
    i2c_master_bus_handle_t bus;
    esp_err_t error = get_bus(&bus);
    if (error != ESP_OK) return error;

    unsigned int found = 0;
    for (uint16_t address = 0x08; address <= 0x77; ++address) {
        error = i2c_master_probe(bus, address, TIMEOUT_MS);
        if (error == ESP_ERR_NOT_FOUND) continue;
        if (error != ESP_OK) {
            printf("I2C probe failed at 0x%02x\n", address);
            return error;
        }
        printf("I2C device at 0x%02x\n", address);
        ++found;
    }
    printf("I2C scan complete: %u device(s) found\n", found);
    return ESP_OK;
}

static esp_err_t load_calibration(void)
{
    uint8_t a[26], h[7];
    esp_err_t error = read_regs(REG_CALIB_TP, a, sizeof(a));
    if (error == ESP_OK) error = read_regs(REG_CALIB_H, h, sizeof(h));
    if (error != ESP_OK) return error;
#define U16(i) ((uint16_t)(a[(i) + 1] << 8 | a[i]))
    cal.t1 = U16(0); cal.t2 = (int16_t)U16(2); cal.t3 = (int16_t)U16(4);
    cal.p1 = U16(6); cal.p2 = (int16_t)U16(8); cal.p3 = (int16_t)U16(10);
    cal.p4 = (int16_t)U16(12); cal.p5 = (int16_t)U16(14); cal.p6 = (int16_t)U16(16);
    cal.p7 = (int16_t)U16(18); cal.p8 = (int16_t)U16(20); cal.p9 = (int16_t)U16(22);
#undef U16
    cal.h1 = a[25];
    cal.h2 = (int16_t)(h[1] << 8 | h[0]);
    cal.h3 = h[2];
    /* H4 and H5 are signed 12-bit values that share register 0xE5. */
    cal.h4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F));
    cal.h5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4));
    cal.h6 = (int8_t)h[6];
    return cal.p1 ? ESP_OK : ESP_ERR_INVALID_RESPONSE; /* p1 = 0 would divide by zero. */
}

static esp_err_t find_device(i2c_master_bus_handle_t bus)
{
    /* SDO to GND selects 0x76, to VDDIO selects 0x77. */
    for (uint16_t address = 0x76; address <= 0x77; ++address) {
        esp_err_t error = i2c_master_probe(bus, address, TIMEOUT_MS);
        if (error == ESP_ERR_NOT_FOUND) continue;
        if (error != ESP_OK) return error;

        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = 100000,
        };
        error = i2c_master_bus_add_device(bus, &config, &device);
        if (error != ESP_OK) return error;

        uint8_t id;
        error = read_regs(REG_CHIP_ID, &id, 1);
        if (error == ESP_OK && id == CHIP_ID) return ESP_OK;
        if (error == ESP_OK) {
            ESP_LOGW(TAG, "I2C address 0x%02x returned chip ID 0x%02x (expected 0x60)", address, id);
        }
        i2c_master_bus_rm_device(device);
        device = NULL;
        if (error != ESP_OK) return error;
    }
    ESP_LOGE(TAG, "No BME280 at 0x76 or 0x77 on I2C port %d (SDA GPIO %d, SCL GPIO %d)",
        CONFIG_MESS_I2C_PORT, CONFIG_MESS_I2C_SDA, CONFIG_MESS_I2C_SCL);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bme280_init(void)
{
    if (device) return ESP_OK;

    i2c_master_bus_handle_t bus;
    esp_err_t error = get_bus(&bus);
    if (error == ESP_OK) error = find_device(bus);
    if (error != ESP_OK) return error;
    uint8_t status = 0;
    error = write_reg(REG_RESET, 0xB6);
    for (int tries = 0; error == ESP_OK && tries < 10; ++tries) {
        vTaskDelay(pdMS_TO_TICKS(2) + 1);
        error = read_regs(REG_STATUS, &status, 1);
        if (error == ESP_OK && !(status & 0x01)) break; /* im_update: calibration copied to registers */
    }
    if (error == ESP_OK && (status & 0x01)) error = ESP_ERR_TIMEOUT;
    if (error == ESP_OK) error = load_calibration();
    /* Oversampling x1 for all three, filter off; ctrl_hum applies after the next ctrl_meas write. */
    if (error == ESP_OK) error = write_reg(REG_CTRL_HUM, 0x01);
    if (error == ESP_OK) error = write_reg(REG_CONFIG, 0x00);
    if (error != ESP_OK) {
        i2c_master_bus_rm_device(device);
        device = NULL;
    }
    return error;
}

esp_err_t bme280_read(sensor_reading_t *reading)
{
    if (!reading) return ESP_ERR_INVALID_ARG;
    *reading = (sensor_reading_t){.status = ESP_ERR_INVALID_STATE};
    if (!device) return reading->status;
    /* Forced mode: one conversion, then sleep. Worst case at x1 oversampling is 9.3 ms. */
    esp_err_t error = write_reg(REG_CTRL_MEAS, 0x25);
    uint8_t status = 0x08, d[8];
    for (int tries = 0; error == ESP_OK && (status & 0x08) && tries < 5; ++tries) {
        vTaskDelay(pdMS_TO_TICKS(10) + 1);
        error = read_regs(REG_STATUS, &status, 1);
    }
    if (error == ESP_OK && (status & 0x08)) error = ESP_ERR_TIMEOUT;
    if (error == ESP_OK) error = read_regs(REG_DATA, d, sizeof(d));
    if (error != ESP_OK) return reading->status = error;
    int32_t adc_p = (int32_t)d[0] << 12 | d[1] << 4 | d[2] >> 4;
    int32_t adc_t = (int32_t)d[3] << 12 | d[4] << 4 | d[5] >> 4;
    int32_t adc_h = (int32_t)d[6] << 8 | d[7];
    if (adc_t == 0x80000 || adc_p == 0x80000 || adc_h == 0x8000)
        return reading->status = ESP_ERR_INVALID_RESPONSE; /* Measurement skipped. */

    double v1 = (adc_t / 16384.0 - cal.t1 / 1024.0) * cal.t2;
    double v2 = (adc_t / 131072.0 - cal.t1 / 8192.0) * (adc_t / 131072.0 - cal.t1 / 8192.0) * cal.t3;
    double t_fine = v1 + v2;
    double temperature = t_fine / 5120.0;

    v1 = t_fine / 2.0 - 64000.0;
    v2 = v1 * v1 * cal.p6 / 32768.0 + v1 * cal.p5 * 2.0;
    v2 = v2 / 4.0 + cal.p4 * 65536.0;
    v1 = (cal.p3 * v1 * v1 / 524288.0 + cal.p2 * v1) / 524288.0;
    v1 = (1.0 + v1 / 32768.0) * cal.p1;
    if (v1 == 0.0) return reading->status = ESP_ERR_INVALID_RESPONSE;
    double pressure = (1048576.0 - adc_p - v2 / 4096.0) * 6250.0 / v1;
    pressure += (cal.p9 * pressure * pressure / 2147483648.0 + pressure * cal.p8 / 32768.0 + cal.p7) / 16.0;

    double h = t_fine - 76800.0;
    h = (adc_h - (cal.h4 * 64.0 + cal.h5 / 16384.0 * h)) *
        (cal.h2 / 65536.0 * (1.0 + cal.h6 / 67108864.0 * h * (1.0 + cal.h3 / 67108864.0 * h)));
    h *= 1.0 - cal.h1 * h / 524288.0;
    double humidity = h < 0 ? 0 : h > 100 ? 100 : h;

    reading->values[0] = (float)temperature;
    reading->values[1] = (float)pressure;
    reading->values[2] = (float)humidity;
    reading->count = 3;
    return reading->status = ESP_OK;
}
