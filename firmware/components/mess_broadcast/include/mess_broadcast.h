#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "mess_mesh.h"

#define MESS_BROADCAST_MAX_TEXT MESS_MESH_MAX_TEXT
/* Start automatic discovery and reception on channel 1, ESP-NOW LR 500 Kbps. */
esp_err_t mess_broadcast_start(void);
/* Queue a local measurement for best-effort flooding. Time is origin uptime in us. */
esp_err_t mess_broadcast_publish(uint8_t sensor, uint64_t measured_us,
    const char *text, size_t length);
