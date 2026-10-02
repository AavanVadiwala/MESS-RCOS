#include "mess_i2c.h"
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"

static i2c_master_bus_handle_t bus;
static uint32_t frequency;
static const int timeout_ms = 100;

static bool valid_address(uint8_t address)
{
    return address >= 0x08 && address <= 0x77;
}

esp_err_t mess_i2c_init(int sda, int scl, uint32_t frequency_hz)
{
    if (bus) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!GPIO_IS_VALID_OUTPUT_GPIO(sda) || !GPIO_IS_VALID_OUTPUT_GPIO(scl) ||
        sda == scl || frequency_hz < 10000 || frequency_hz > 400000) {
        return ESP_ERR_INVALID_ARG;
    }
    const i2c_master_bus_config_t config = {
        .i2c_port = -1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t result = i2c_new_master_bus(&config, &bus);
    if (result == ESP_OK) {
        frequency = frequency_hz;
    }
    return result;
}

esp_err_t mess_i2c_deinit(void)
{
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = i2c_del_master_bus(bus);
    if (result == ESP_OK) {
        bus = NULL;
    }
    return result;
}

esp_err_t mess_i2c_probe(uint8_t address)
{
    if (!valid_address(address)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(bus, address, timeout_ms);
}

esp_err_t mess_i2c_transfer(uint8_t address,
                          const uint8_t *tx, size_t tx_length,
                          uint8_t *rx, size_t rx_length)
{
    if (!valid_address(address) || (!tx_length && !rx_length) ||
        tx_length > MESS_I2C_MAX_TRANSFER || rx_length > MESS_I2C_MAX_TRANSFER ||
        (tx_length && !tx) || (rx_length && !rx)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = frequency,
    };
    i2c_master_dev_handle_t device = NULL;
    esp_err_t result = i2c_master_bus_add_device(bus, &config, &device);
    if (result != ESP_OK) {
        return result;
    }
    if (tx_length && rx_length) {
        result = i2c_master_transmit_receive(device, tx, tx_length, rx, rx_length, timeout_ms);
    } else if (tx_length) {
        result = i2c_master_transmit(device, tx, tx_length, timeout_ms);
    } else {
        result = i2c_master_receive(device, rx, rx_length, timeout_ms);
    }
    esp_err_t cleanup = i2c_master_bus_rm_device(device);
    return result == ESP_OK ? cleanup : result;
}
