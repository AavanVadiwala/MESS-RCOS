#pragma once
#include <stdio.h>
/* argv[0] is the mode name; executes one command, without reading UART input. */
int mess_i2ctools_run(int argc, char **argv, FILE *out);
int mess_i2ctools_console(int argc, char **argv);
