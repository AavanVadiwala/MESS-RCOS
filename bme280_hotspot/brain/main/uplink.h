#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Optional internet uplink: the brain joins an internet Wi-Fi (eduroam or a
 * normal WPA2 network) as a station while still hosting its hotspot, and
 * uploads readings to Supabase in batches. Disabled when no uplink SSID is set. */

/* Call after esp_netif_init/esp_event_loop_create_default and before esp_wifi_start.
 * Returns true when the uplink is configured, i.e. Wi-Fi must run in AP+STA mode. */
bool uplink_init(void);
/* Call after esp_wifi_start when uplink_init returned true. */
void uplink_start(void);
/* Values per reading, in this order: temperature_c, humidity_pct, pressure_hpa, lux, tvoc_ppb, eco2_ppm.
 * NAN means the sensor sent no value; such fields are left out of the upload. */
#define UPLINK_VALUES 6
/* Queues one reading for upload; the oldest are dropped when the buffer is full. */
void uplink_add(const char *node, const char *boot, uint32_t sequence, const float values[UPLINK_VALUES], int rssi);
