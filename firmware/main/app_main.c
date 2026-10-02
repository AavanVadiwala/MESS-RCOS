#include <stdio.h>
#include "sdkconfig.h"
#include "esp_console.h"
#include "mess_i2c.h"
#include "mess_i2ctools.h"
#include "mess_comm.h"
#include "mess_laptop.h"
#include "mess_commands.h"

void app_main(void)
{
    ESP_ERROR_CHECK(mess_commands_init());
    esp_err_t err = mess_i2c_init(CONFIG_MESS_I2C_SDA, CONFIG_MESS_I2C_SCL,
                                CONFIG_MESS_I2C_FREQUENCY);
    if (err != ESP_OK) printf("I2C init: %s; comm remains available\n", esp_err_to_name(err));
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "mess>";
    config.max_cmdline_length = 512;
    config.max_cmdline_args = 68;
    const esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart, &config, &repl));
    const esp_console_cmd_t commands[] = {
        {.command="i2ctools", .help="Open I2C console; exit returns", .func=mess_i2ctools_console},
        {.command="comm", .help="Open ESP-NOW console; exit returns", .func=mess_comm_console},
        {.command="wifi", .help="Broadcast typed lines to laptops; exit returns", .func=mess_laptop_console},
    };
    for (unsigned i=0; i<sizeof(commands)/sizeof(commands[0]); ++i) ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    puts("Enter i2ctools, comm, or wifi. Each console accepts help and exit.");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
