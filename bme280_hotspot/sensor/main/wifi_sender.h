#pragma once
#include <stdbool.h>

/* Starts Wi-Fi and the HTTP sender task when an SSID is configured; returns false in serial-only mode. */
bool mess_wifi_start(void);
/* Hands the newest reading to the sender; older unsent readings are replaced, never queued. */
void mess_wifi_submit(float temperature, float humidity, float pressure);
