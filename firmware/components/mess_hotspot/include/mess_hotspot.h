#pragma once
#include "esp_err.h"
#include "mess_mesh.h"

#define MESS_HOTSPOT_PASSWORD "MESS12345"
/* Fixed global ring capacity: at least 3 boards x 3 sensors x 5 samples. */
#define MESS_HOTSPOT_HISTORY 64
/* Call after Wi-Fi initialization, before Wi-Fi starts. */
esp_err_t mess_hotspot_prepare(void);
/* Start the JSON HTTP API after Wi-Fi starts. */
esp_err_t mess_hotspot_start(void);
/* Store each accepted reading; relayed copies retain the origin UUID. */
void mess_hotspot_collect(const mess_mesh_record_t *record,
    const uint8_t relay[6], int rssi, bool local);
