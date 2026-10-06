#pragma once
#include <stdio.h>
int mess_comm_run(int argc, char **argv, FILE *out);
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
/* Callers serialize start/send via mess_commands. Automatic UART receive task;
 * 250 Kbps Espressif LR on mess_wifi's shared station radio, not a mesh stack. */
esp_err_t mess_comm_start(void);
esp_err_t mess_comm_send(const uint8_t destination[6], const uint8_t *data, size_t length);
int mess_comm_console(int argc, char **argv);
