#pragma once
#include "esp_err.h"
esp_err_t mess_commands_init(void);
void mess_commands_lock(void);
void mess_commands_unlock(void);
