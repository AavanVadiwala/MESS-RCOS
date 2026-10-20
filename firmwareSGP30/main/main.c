
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define I2C_PORT I2C_NUM_0
#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22

#define SGP30_ADDRESS 0x58

// SGP30 commands are 16 bits, sent MSB first.
#define SGP30_CMD_GET_SERIAL_ID      0x3682
#define SGP30_CMD_GET_FEATURE_SET    0x202F
#define SGP30_CMD_INIT_AIR_QUALITY   0x2003
#define SGP30_CMD_MEASURE_AIR_QUALITY 0x2008

// Every 16-bit word the SGP30 returns is followed by a CRC-8
// (polynomial 0x31, initial value 0xFF).
static uint8_t sgp30_crc(const uint8_t *data, int length)
{
    uint8_t crc = 0xFF;

    for (int i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }

    return crc;
}

static esp_err_t sgp30_send_command(i2c_master_dev_handle_t sensor_handle,
                                    uint16_t command)
{
    uint8_t buffer[2] = { command >> 8, command & 0xFF };

    return i2c_master_transmit(sensor_handle, buffer, sizeof(buffer), 1000);
}

// Sends a command, waits for the sensor to finish, then reads back
// word_count words, checking the CRC on each one.
static esp_err_t sgp30_read_words(i2c_master_dev_handle_t sensor_handle,
                                  uint16_t command,
                                  int delay_ms,
                                  uint16_t *words,
                                  int word_count)
{
    esp_err_t result = sgp30_send_command(sensor_handle, command);
    if (result != ESP_OK)
    {
        return result;
    }

    // pdMS_TO_TICKS rounds down, and the first tick of a delay can end
    // almost immediately, so round up and add a tick to guarantee the
    // sensor gets at least delay_ms.
    vTaskDelay(pdMS_TO_TICKS(delay_ms + portTICK_PERIOD_MS - 1) + 1);

    uint8_t data[9];

    result = i2c_master_receive(sensor_handle, data, word_count * 3, 1000);
    if (result != ESP_OK)
    {
        return result;
    }

    for (int i = 0; i < word_count; i++)
    {
        uint8_t *word = &data[i * 3];

        if (sgp30_crc(word, 2) != word[2])
        {
            return ESP_ERR_INVALID_CRC;
        }

        words[i] = (word[0] << 8) | word[1];
    }

    return ESP_OK;
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
        .device_address = SGP30_ADDRESS,
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

    // Read the serial number and feature set to confirm the sensor is there.
    uint16_t serial[3];

    ESP_ERROR_CHECK(
        sgp30_read_words(sensor_handle, SGP30_CMD_GET_SERIAL_ID, 1, serial, 3)
    );

    printf("SGP30 serial: %04X%04X%04X\n", serial[0], serial[1], serial[2]);

    uint16_t feature_set;

    ESP_ERROR_CHECK(
        sgp30_read_words(sensor_handle, SGP30_CMD_GET_FEATURE_SET, 10,
                         &feature_set, 1)
    );

    printf("SGP30 feature set: 0x%04X\n", feature_set);

    // Start the air quality algorithm. Measurements must then be
    // requested once per second to keep its baseline accurate.
    ESP_ERROR_CHECK(
        sgp30_send_command(sensor_handle, SGP30_CMD_INIT_AIR_QUALITY)
    );

    vTaskDelay(pdMS_TO_TICKS(10));

    printf("SGP30 air quality sensor started!\n");
    printf("The first ~15 readings are fixed at 400 ppm / 0 ppb while it warms up.\n");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint16_t values[2];

        esp_err_t result = sgp30_read_words(
            sensor_handle,
            SGP30_CMD_MEASURE_AIR_QUALITY,
            12,
            values,
            2
        );

        if (result == ESP_OK)
        {
            printf("eCO2: %u ppm, TVOC: %u ppb\n", values[0], values[1]);
        }
        else
        {
            printf("Sensor read failed: %s\n",
                   esp_err_to_name(result));
        }
    }
}
