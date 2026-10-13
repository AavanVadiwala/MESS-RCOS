
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define I2C_PORT I2C_NUM_0
#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22

#define BH1750_ADDRESS 0x23

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
        .device_address = BH1750_ADDRESS,
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

    // Set the BH1750 to continuous high-resolution mode.
    uint8_t command = 0x10;

    ESP_ERROR_CHECK(
        i2c_master_transmit(
            sensor_handle,
            &command,
            1,
            1000
        )
    );

    printf("BH1750 light sensor started!\n");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(180));

        uint8_t data[2];

        esp_err_t result = i2c_master_receive(
            sensor_handle,
            data,
            sizeof(data),
            1000
        );

        if (result == ESP_OK)
        {
            int raw_value = (data[0] << 8) | data[1];
            float lux = raw_value / 1.2f;

            printf("Light level: %.2f lux\n", lux);
        }
        else
        {
            printf("Sensor read failed: %s\n",
                   esp_err_to_name(result));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
