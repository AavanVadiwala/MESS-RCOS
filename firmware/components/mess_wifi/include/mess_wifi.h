#pragma once
#include <stdio.h>
#include "esp_err.h"
/* Shared radio owner: station interface for ESP-NOW, AP for laptops. */
esp_err_t mess_wifi_start(void);
esp_err_t mess_wifi_set_channel(unsigned channel);
void mess_wifi_status(void);
void mess_wifi_print_status(FILE *out);
