/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdio.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cmd_i2ctools.h"
#include <string.h>
#include "argtable3/argtable3.h"
#include "driver/i2c_master.h"
#include "esp_console.h"
#include "esp_log.h"

static int local_i2cconfig(int argc, char **argv);
static int local_i2cdetect(int argc, char **argv);
static int local_i2cget(int argc, char **argv);
static int local_i2cset(int argc, char **argv);
static int local_i2cdump(int argc, char **argv);
static FILE *s_output;
static SemaphoreHandle_t s_lock;

#define I2C_TOOL_TIMEOUT_VALUE_MS   50
#define I2C_DEFAULT_FREQUENCY       (100 * 1000) // use this safe value if the "--freq" is skipped in `i2cconfig` command

static int s_i2c_bus_frequency;
static i2c_master_bus_handle_t s_i2c_bus;

static struct {
    struct arg_int *scl;
    struct arg_int *sda;
    struct arg_int *freq;
    struct arg_end *end;
} i2cconfig_args;

static int do_i2cconfig_cmd(int argc, char **argv)
{
    esp_err_t err = ESP_OK;
    int nerrors = arg_parse(argc, argv, (void **)&i2cconfig_args);
    if (nerrors != 0) {
        arg_print_errors(s_output, i2cconfig_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    int requested_frequency = i2cconfig_args.freq->count ? i2cconfig_args.freq->ival[0] : I2C_DEFAULT_FREQUENCY;
    int requested_sda = i2cconfig_args.sda->ival[0], requested_scl = i2cconfig_args.scl->ival[0];
    if (!GPIO_IS_VALID_OUTPUT_GPIO(requested_sda) || !GPIO_IS_VALID_OUTPUT_GPIO(requested_scl) ||
        requested_sda == requested_scl || requested_frequency < 10000 || requested_frequency > 400000)
        return ESP_ERR_INVALID_ARG;
    /* Check "--freq" option */
    if (i2cconfig_args.freq->count) {
        s_i2c_bus_frequency = i2cconfig_args.freq->ival[0];
    } else {
        s_i2c_bus_frequency = I2C_DEFAULT_FREQUENCY;
    }
    /* Check "--sda" option */
    int i2c_gpio_sda = i2cconfig_args.sda->ival[0];
    /* Check "--scl" option */
    int i2c_gpio_scl = i2cconfig_args.scl->ival[0];

    // if the I2C bus is already initialized, delete it first
    if (s_i2c_bus) {
        err = i2c_del_master_bus(s_i2c_bus);
        if (err != ESP_OK) {
            // propagate the error
            return err;
        }
        s_i2c_bus = NULL;
    }

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,  // select a free I2C port automatically
        .scl_io_num = i2c_gpio_scl,
        .sda_io_num = i2c_gpio_sda,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true, // in case external pull-up resistors are not available, enable the internal weak pull-up
        }
    };

    err = i2c_new_master_bus(&i2c_bus_config, &s_i2c_bus);
    if (err != ESP_OK) {
        // propagate the error
        return err;
    }

    return ESP_OK;
}

static void register_i2cconfig(void)
{
    i2cconfig_args.sda = arg_int1(NULL, "sda", "<gpio>", "Set the gpio for I2C SDA"); // mandatory
    i2cconfig_args.scl = arg_int1(NULL, "scl", "<gpio>", "Set the gpio for I2C SCL"); // mandatory
    i2cconfig_args.freq = arg_int0(NULL, "freq", "<Hz>", "Set the frequency(Hz) of I2C bus"); // optional
    i2cconfig_args.end = arg_end(2);
    const esp_console_cmd_t i2cconfig_cmd = {
        .command = "i2cconfig",
        .help = "Config I2C bus frequency and IOs",
        .hint = NULL, // generate the hint from argtable automatically
        .func = &local_i2cconfig,
        .argtable = &i2cconfig_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cconfig_cmd));
}

static int do_i2cdetect_cmd(int argc, char **argv)
{
    if (!s_i2c_bus) {
        fprintf(s_output, "I2C bus is not initialized. Please run 'i2cconfig' first\n");
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t address;
    fprintf(s_output, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        fprintf(s_output, "%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(s_output);
            address = i + j;
            esp_err_t ret = i2c_master_probe(s_i2c_bus, address, I2C_TOOL_TIMEOUT_VALUE_MS);
            if (ret == ESP_OK) {
                fprintf(s_output, "%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                fprintf(s_output, "UU ");
            } else {
                fprintf(s_output, "-- ");
            }
        }
        fprintf(s_output, "\r\n");
    }

    return ESP_OK;
}

static void register_i2cdetect(void)
{
    const esp_console_cmd_t i2cdetect_cmd = {
        .command = "i2cdetect",
        .help = "Scan I2C bus for devices",
        .hint = NULL,
        .func = &local_i2cdetect,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdetect_cmd));
}

static struct {
    struct arg_int *chip_address;
    struct arg_int *register_address;
    struct arg_int *data_length;
    struct arg_end *end;
} i2cget_args;

static int do_i2cget_cmd(int argc, char **argv)
{
    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err  = ESP_OK;

    int nerrors = arg_parse(argc, argv, (void **)&i2cget_args);
    if (nerrors != 0) {
        arg_print_errors(s_output, i2cget_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_i2c_bus) {
        fprintf(s_output, "I2C bus is not initialized. Please run 'i2cconfig' first\n");
        return ESP_ERR_INVALID_STATE;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cget_args.chip_address->ival[0];
    /* Check register address: "-r" option */
    int data_addr = -1;
    if (i2cget_args.register_address->count) {
        data_addr = i2cget_args.register_address->ival[0];
    }
    /* Check data length: "-l" option */
    int len = 1;
    if (i2cget_args.data_length->count) {
        len = i2cget_args.data_length->ival[0];
    }
    if (chip_addr < 0x08 || chip_addr > 0x77 || len < 1 || len > 256 || data_addr < -1 || data_addr > 255) return ESP_ERR_INVALID_ARG;
    uint8_t *data = malloc(len);
    if (!data) {
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = s_i2c_bus_frequency,
        .device_address = chip_addr,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &i2c_dev_conf, &dev_handle);
    if (err != ESP_OK) {
        free(data);
        return err;
    }

    uint8_t register_byte = data_addr;
    esp_err_t ret = data_addr < 0
        ? i2c_master_receive(dev_handle, data, len, I2C_TOOL_TIMEOUT_VALUE_MS)
        : i2c_master_transmit_receive(dev_handle, &register_byte, 1, data, len, I2C_TOOL_TIMEOUT_VALUE_MS);
    if (ret == ESP_OK) {
        for (int i = 0; i < len; i++) {
            fprintf(s_output, "0x%02x ", data[i]);
            if ((i + 1) % 16 == 0) {
                fprintf(s_output, "\r\n");
            }
        }
        if (len % 16) {
            fprintf(s_output, "\r\n");
        }
    } else if (ret == ESP_ERR_TIMEOUT) {
        fprintf(s_output, "Bus is busy\n");
    } else {
        fprintf(s_output, "Read failed\n");
    }
    free(data);

    err = i2c_master_bus_rm_device(dev_handle);
    if (err != ESP_OK) {
        // propagate the error
        return err;
    }
    return ret;
}

static void register_i2cget(void)
{
    i2cget_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cget_args.register_address = arg_int0("r", "register", "<register_addr>", "Specify the address on that chip to read from");
    i2cget_args.data_length = arg_int0("l", "length", "<length>", "Specify the length to read from that data address");
    i2cget_args.end = arg_end(1);
    const esp_console_cmd_t i2cget_cmd = {
        .command = "i2cget",
        .help = "Read registers visible through the I2C bus",
        .hint = NULL,
        .func = &local_i2cget,
        .argtable = &i2cget_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cget_cmd));
}

static struct {
    struct arg_int *chip_address;
    struct arg_int *register_address;
    struct arg_int *data;
    struct arg_end *end;
} i2cset_args;

static int do_i2cset_cmd(int argc, char **argv)
{
    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err  = ESP_OK;

    int nerrors = arg_parse(argc, argv, (void **)&i2cset_args);
    if (nerrors != 0) {
        arg_print_errors(s_output, i2cset_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_i2c_bus) {
        fprintf(s_output, "I2C bus is not initialized. Please run 'i2cconfig' first\n");
        return ESP_ERR_INVALID_STATE;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cset_args.chip_address->ival[0];
    /* Check register address: "-r" option */
    int data_addr = 0;
    if (i2cset_args.register_address->count) {
        data_addr = i2cset_args.register_address->ival[0];
    }
    /* Check data: "-d" option */
    int len = i2cset_args.data->count;

    if (chip_addr < 0x08 || chip_addr > 0x77 || data_addr < 0 || data_addr > 255) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < len; ++i)
        if (i2cset_args.data->ival[i] < 0 || i2cset_args.data->ival[i] > 255) return ESP_ERR_INVALID_ARG;
    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = s_i2c_bus_frequency,
        .device_address = chip_addr,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &i2c_dev_conf, &dev_handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t *data = malloc(len + 1);
    if (!data) {
        i2c_master_bus_rm_device(dev_handle);
        return ESP_ERR_NO_MEM;
    }

    data[0] = data_addr;
    for (int i = 0; i < len; i++) {
        data[i + 1] = i2cset_args.data->ival[i];
    }
    esp_err_t ret = i2c_master_transmit(dev_handle, data, len + 1, I2C_TOOL_TIMEOUT_VALUE_MS);
    if (ret == ESP_OK) {
        fprintf(s_output, "Write OK\n");
    } else if (ret == ESP_ERR_TIMEOUT) {
        fprintf(s_output, "Bus is busy\n");
    } else {
        fprintf(s_output, "Write Failed\n");
    }

    free(data);
    err = i2c_master_bus_rm_device(dev_handle);
    if (err != ESP_OK) {
        return err;
    }
    return ret;
}

static void register_i2cset(void)
{
    i2cset_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cset_args.register_address = arg_int0("r", "register", "<register_addr>", "Specify the address on that chip to read from");
    i2cset_args.data = arg_intn(NULL, NULL, "<data>", 0, 256, "Specify the data to write to that data address");
    i2cset_args.end = arg_end(2);
    const esp_console_cmd_t i2cset_cmd = {
        .command = "i2cset",
        .help = "Set registers visible through the I2C bus",
        .hint = NULL,
        .func = &local_i2cset,
        .argtable = &i2cset_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cset_cmd));
}

static struct {
    struct arg_int *chip_address;
    struct arg_int *size;
    struct arg_end *end;
} i2cdump_args;

static int do_i2cdump_cmd(int argc, char **argv)
{
    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err  = ESP_OK;

    int nerrors = arg_parse(argc, argv, (void **)&i2cdump_args);
    if (nerrors != 0) {
        arg_print_errors(s_output, i2cdump_args.end, argv[0]);
        return ESP_ERR_INVALID_ARG;
    }

    /* Check chip address: "-c" option */
    int chip_addr = i2cdump_args.chip_address->ival[0];
    if (chip_addr < 0x08 || chip_addr > 0x77) return ESP_ERR_INVALID_ARG;
    /* Check read size: "-s" option */
    int size = 1;
    if (i2cdump_args.size->count) {
        size = i2cdump_args.size->ival[0];
    }
    if (size != 1 && size != 2 && size != 4) {
        fprintf(s_output, "Wrong read size. Only support 1,2,4\n");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_i2c_bus) {
        fprintf(s_output, "I2C bus is not initialized. Please run 'i2cconfig' first\n");
        return ESP_ERR_INVALID_STATE;
    }

    i2c_device_config_t i2c_dev_conf = {
        .scl_speed_hz = s_i2c_bus_frequency,
        .device_address = chip_addr,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &i2c_dev_conf, &dev_handle);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t data_addr;
    uint8_t data[4];
    int32_t block[16];
    fprintf(s_output, "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f"
           "    0123456789abcdef\r\n");
    for (int i = 0; i < 128; i += 16) {
        fprintf(s_output, "%02x: ", i);
        for (int j = 0; j < 16; j += size) {
            fflush(s_output);
            data_addr = i + j;
            esp_err_t ret = i2c_master_transmit_receive(dev_handle, &data_addr, 1, data, size, I2C_TOOL_TIMEOUT_VALUE_MS);
            if (ret == ESP_OK) {
                for (int k = 0; k < size; k++) {
                    fprintf(s_output, "%02x ", data[k]);
                    block[j + k] = data[k];
                }
            } else {
                for (int k = 0; k < size; k++) {
                    fprintf(s_output, "XX ");
                    block[j + k] = -1;
                }
            }
        }
        fprintf(s_output, "   ");
        for (int k = 0; k < 16; k++) {
            if (block[k] < 0) {
                fprintf(s_output, "X");
                continue;
            }
            if ((block[k] & 0xff) == 0x00 || (block[k] & 0xff) == 0xff) {
                fprintf(s_output, ".");
            } else if ((block[k] & 0xff) < 32 || (block[k] & 0xff) >= 127) {
                fprintf(s_output, "?");
            } else {
                fprintf(s_output, "%c", (char)(block[k] & 0xff));
            }
        }
        fprintf(s_output, "\r\n");
    }

    err = i2c_master_bus_rm_device(dev_handle);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

static void register_i2cdump(void)
{
    i2cdump_args.chip_address = arg_int1("c", "chip", "<chip_addr>", "Specify the address of the chip on that bus");
    i2cdump_args.size = arg_int0("s", "size", "<size>", "Specify the size of each read");
    i2cdump_args.end = arg_end(1);
    const esp_console_cmd_t i2cdump_cmd = {
        .command = "i2cdump",
        .help = "Examine registers visible through the I2C bus",
        .hint = NULL,
        .func = &local_i2cdump,
        .argtable = &i2cdump_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&i2cdump_cmd));
}

void register_i2ctools(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_lock ? ESP_OK : ESP_ERR_NO_MEM);
    register_i2cconfig();
    register_i2cdetect();
    register_i2cget();
    register_i2cset();
    register_i2cdump();
}

/* MESS adaptation: no global stdout redirection; serialize local/remote I2C. */
static int execute(int argc, char **argv, FILE *out)
{
    if (!argc) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_output = out;
    int result = ESP_ERR_INVALID_ARG;
    if (!strcmp(argv[0], "i2cconfig")) result = do_i2cconfig_cmd(argc, argv);
    else if (!strcmp(argv[0], "i2cdetect") && argc == 1) result = do_i2cdetect_cmd(argc, argv);
    else if (!strcmp(argv[0], "i2cget")) result = do_i2cget_cmd(argc, argv);
    else if (!strcmp(argv[0], "i2cset")) result = do_i2cset_cmd(argc, argv);
    else if (!strcmp(argv[0], "i2cdump")) result = do_i2cdump_cmd(argc, argv);
    if (result != ESP_OK) fprintf(out, "I2C result: %s\n", esp_err_to_name(result));
    s_output = NULL;
    xSemaphoreGive(s_lock);
    return result;
}
bool i2ctools_command_allowed(const char *name)
{
    return !strcmp(name, "i2cconfig") || !strcmp(name, "i2cdetect") ||
           !strcmp(name, "i2cget") || !strcmp(name, "i2cset") || !strcmp(name, "i2cdump");
}
int i2ctools_run_line(const char *line, FILE *out)
{
    char copy[160];
    if (!line || strlen(line) >= sizeof(copy)) return ESP_ERR_INVALID_ARG;
    strcpy(copy, line);
    char *argv[70];
    int argc = esp_console_split_argv(copy, argv, 70);
    return execute(argc, argv, out);
}
static int local_i2cconfig(int argc, char **argv) { return execute(argc, argv, stdout); }
static int local_i2cdetect(int argc, char **argv) { return execute(argc, argv, stdout); }
static int local_i2cget(int argc, char **argv) { return execute(argc, argv, stdout); }
static int local_i2cset(int argc, char **argv) { return execute(argc, argv, stdout); }
static int local_i2cdump(int argc, char **argv) { return execute(argc, argv, stdout); }
