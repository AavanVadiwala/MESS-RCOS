#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "mess_i2c.h"
#include "mess_i2ctools.h"
#include "mess_commands.h"

static void usage(FILE *out)
{
    fprintf(out, "%s\n", "scan");
    fprintf(out, "%s\n", "read ADDRESS LENGTH");
    fprintf(out, "%s\n", "write ADDRESS BYTE [BYTE ...]");
    fprintf(out, "%s\n", "write-read ADDRESS READ_LENGTH BYTE [BYTE ...]");
    fprintf(out, "%s\n", "Numbers: decimal or 0x-prefixed hex; address 0x08..0x77; transfers 1..64 bytes.");
}

static bool number(const char *text, unsigned maximum, unsigned *value)
{
    if (!text || !*text || *text == '-') {
        return false;
    }
    errno = 0;
    char *end;
    int base = (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) ? 16 : 10;
    unsigned long parsed = strtoul(text, &end, base);
    if (errno || end == text || *end || parsed > maximum) {
        return false;
    }
    *value = (unsigned)parsed;
    return true;
}

static int i2c_command(int argc, char **argv, FILE *out)
{
    if (argc == 2 && strcmp(argv[1], "scan") == 0) {
        unsigned found = 0;
        for (unsigned address = 0x08; address <= 0x77; ++address) {
            esp_err_t result = mess_i2c_probe(address);
            if (result == ESP_OK) {
                fprintf(out, "ACK at 0x%02x\n", address);
                ++found;
            } else if (result != ESP_ERR_NOT_FOUND) {
                fprintf(out, "Scan stopped at 0x%02x: %s (check wiring/pull-ups)\n",
                       address, esp_err_to_name(result));
                return 1;
            }
        }
        fprintf(out, "%u responding address(es). An ACK does not identify the sensor.\n", found);
        return 0;
    }
    if (argc < 4) {
        usage(out);
        return 1;
    }
    unsigned address;
    if (!number(argv[2], 0x77, &address) || address < 0x08) {
        fprintf(out, "%s\n", "Invalid 7-bit device address.");
        return 1;
    }
    uint8_t tx[MESS_I2C_MAX_TRANSFER], rx[MESS_I2C_MAX_TRANSFER];
    unsigned rx_length = 0;
    int data_start;
    if (strcmp(argv[1], "read") == 0 || strcmp(argv[1], "write-read") == 0) {
        if (!number(argv[3], sizeof(rx), &rx_length) || rx_length == 0) {
            fprintf(out, "%s\n", "Read length must be 1..64.");
            return 1;
        }
        bool raw_read = strcmp(argv[1], "read") == 0;
        if ((raw_read && argc != 4) || (!raw_read && argc < 5)) {
            usage(out);
            return 1;
        }
        data_start = 4;
    } else if (strcmp(argv[1], "write") == 0) {
        data_start = 3;
    } else {
        usage(out);
        return 1;
    }
    size_t tx_length = argc - data_start;
    if (tx_length > sizeof(tx)) {
        fprintf(out, "%s\n", "Write length must be 1..64.");
        return 1;
    }
    for (size_t i = 0; i < tx_length; ++i) {
        unsigned byte;
        if (!number(argv[data_start + i], 255, &byte)) {
            fprintf(out, "%s\n", "Each byte must be 0..255 (or 0x00..0xff).");
            return 1;
        }
        tx[i] = byte;
    }
    esp_err_t result = mess_i2c_transfer(address, tx, tx_length, rx, rx_length);
    if (result != ESP_OK) {
        fprintf(out, "I2C failed: %s\n", esp_err_to_name(result));
        return 1;
    }
    for (unsigned i = 0; i < rx_length; ++i) {
        fprintf(out, "0x%02x ", rx[i]);
    }
    fprintf(out, "%s\n", rx_length ? "" : "Write OK");
    return 0;
}


int mess_i2ctools_run(int argc, char **argv, FILE *out)
{
    mess_commands_lock();
    int result = 0;
    if (argc <= 1 || (argc == 2 && !strcmp(argv[1], "help"))) usage(out);
    else result = i2c_command(argc, argv, out);
    mess_commands_unlock();
    return result;
}

int mess_i2ctools_console(int argc, char **argv)
{
    (void)argc; (void)argv;
    usage(stdout);
    puts("Enter commands without the i2c prefix; exit returns to mess>.");
    for (;;) {
        char *line = linenoise("i2ctools> ");
        if (!line) break;
        char *args[70] = {"i2c"};
        int count = esp_console_split_argv(line, args + 1, 69);
        if (count && strcmp(args[1], "exit") == 0) { linenoiseFree(line); break; }
        if (count && strcmp(args[1], "help") == 0) usage(stdout);
        else if (count) mess_i2ctools_run(count + 1, args, stdout);
        linenoiseFree(line);
    }
    return 0;
}
