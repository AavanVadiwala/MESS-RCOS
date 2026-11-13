#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "bme280.h"
#include "wifi_sender.h"

static const char *TAG = "BME280";
static i2c_master_dev_handle_t sensor;

static BME280_INTF_RET_TYPE read_regs(uint8_t reg, uint8_t *data, uint32_t len, void *ctx)
{
    return i2c_master_transmit_receive(*(i2c_master_dev_handle_t *)ctx,
        &reg, 1, data, len, 100) == ESP_OK ? 0 : -1;
}

static BME280_INTF_RET_TYPE write_regs(uint8_t reg, const uint8_t *data, uint32_t len, void *ctx)
{
    uint8_t buffer[33];
    if (len > sizeof(buffer) - 1) return -1;
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    return i2c_master_transmit(*(i2c_master_dev_handle_t *)ctx,
        buffer, len + 1, 100) == ESP_OK ? 0 : -1;
}

static void delay_us(uint32_t us, void *ctx)
{
    (void)ctx;
    /* Round task delays up: Bosch requires at least the requested duration. */
    uint32_t tick_us = portTICK_PERIOD_MS * 1000U;
    if (us >= tick_us) vTaskDelay((us + tick_us - 1) / tick_us + 1);
    else esp_rom_delay_us(us);
}

static bool checked(const char *operation, int8_t result)
{
    if (result == BME280_OK) return true;
    ESP_LOGE(TAG, "%s failed (%d); no valid reading", operation, result);
    return false;
}

void app_main(void)
{
    mess_wifi_start();
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_MESS_BME_SDA,
        .scl_io_num = CONFIG_MESS_BME_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* Use the breakout's proper pull-ups to 3.3 V. */
        .flags.enable_internal_pullup = false,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    /* Probe both BME280 addresses and verify its chip ID before configuring it. */
    for (uint16_t addr = 0x76; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, 100) != ESP_OK) continue;
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &config, &sensor));
        uint8_t reg = 0xD0, id = 0;
        esp_err_t err = i2c_master_transmit_receive(sensor, &reg, 1, &id, 1, 100);
        if (err == ESP_OK && id == BME280_CHIP_ID) {
            ESP_LOGI(TAG, "BME280 found at 0x%02x (chip ID 0x%02x)", addr, id);
            break;
        }
        ESP_LOGW(TAG, "Device at 0x%02x is not a verified BME280 (ID 0x%02x, %s)",
            addr, id, esp_err_to_name(err));
        ESP_ERROR_CHECK(i2c_master_bus_rm_device(sensor));
        sensor = NULL;
    }
    if (!sensor) {
        ESP_LOGE(TAG, "No BME280 found. Check power, GND, SDA GPIO%d, SCL GPIO%d, pull-ups and I2C mode; restart after fixing wiring.",
            CONFIG_MESS_BME_SDA, CONFIG_MESS_BME_SCL);
        return;
    }
    struct bme280_dev dev = {
        .intf = BME280_I2C_INTF, .read = read_regs, .write = write_regs,
        .delay_us = delay_us, .intf_ptr = &sensor,
    };
    if (!checked("Initialize", bme280_init(&dev))) return;
    struct bme280_settings settings = {0};
    if (!checked("Read settings", bme280_get_sensor_settings(&settings, &dev))) return;
    settings.osr_t = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_1X;
    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.filter = BME280_FILTER_COEFF_OFF;
    if (!checked("Configure", bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &dev))) return;
    uint32_t measurement_us = 0;
    if (!checked("Measurement delay", bme280_cal_meas_delay(&measurement_us, &settings))) return;
    ESP_LOGI(TAG, "Printing temperature, relative humidity and pressure; BME280 has NO gas measurement.");
    TickType_t previous = xTaskGetTickCount();
    while (true) {
        struct bme280_data data = {0};
        if (checked("Start measurement", bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &dev))) {
            delay_us(measurement_us + 2000, NULL);
            if (checked("Read measurement", bme280_get_sensor_data(BME280_ALL, &data, &dev))) {
                printf("Temperature: %.2f C | Humidity: %.2f %%RH | Pressure: %.2f hPa\n",
                    data.temperature, data.humidity, data.pressure / 100.0);
                mess_wifi_submit(data.temperature, data.humidity, data.pressure / 100.0);
            }
        }
        xTaskDelayUntil(&previous, pdMS_TO_TICKS(1000));
    }
}
