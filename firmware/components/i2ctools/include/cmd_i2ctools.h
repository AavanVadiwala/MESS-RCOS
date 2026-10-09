/* Adapted from Espressif i2c_tools example; Unlicense OR CC0-1.0. */
#pragma once
#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"
void register_i2ctools(void);
bool i2ctools_command_allowed(const char *name);
int i2ctools_run_line(const char *line, FILE *out);
