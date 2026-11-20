#pragma once
#include <stdbool.h>

/* One reading from the node's sensors. A field is NAN when its sensor is missing, failed or
 * (SGP30) still warming up; the Wi-Fi message leaves such fields out. */
typedef struct {
    float temperature, humidity, pressure; /* BME280: C, %RH, hPa */
    float lux;                             /* BH1750 */
    float tvoc, eco2;                      /* SGP30: ppb, ppm (estimates, not direct CO2) */
} mess_reading_t;

/* Starts Wi-Fi and the HTTP sender task when an SSID is configured; returns false in serial-only mode. */
bool mess_wifi_start(void);
/* Hands the newest reading to the sender; older unsent readings are replaced, never queued. */
void mess_wifi_submit(const mess_reading_t *reading);
