#pragma once
#include <stddef.h>
#include <stdint.h>
/* Transport callbacks return zero on success. One task must own each device.
 * write/read address the SGP30 at 0x58. sleep_ms must sleep AT LEAST that long. */
typedef struct {
    void *context;
    int (*write)(void *, const uint8_t *, size_t);
    int (*read)(void *, uint8_t *, size_t);
    void (*sleep_ms)(unsigned);
} sgp30_t;
enum { SGP_OK=0, SGP_IO=-1, SGP_CRC=-2, SGP_ARGUMENT=-3, SGP_SELFTEST=-4 };
uint8_t sgp30_crc(const uint8_t *data, size_t length);
int sgp30_init(sgp30_t *s);
int sgp30_identify(sgp30_t *s, uint64_t *serial, uint16_t *features);
int sgp30_selftest(sgp30_t *s);
int sgp30_measure(sgp30_t *s, uint16_t *eco2_ppm, uint16_t *tvoc_ppb);
int sgp30_get_baseline(sgp30_t *s, uint16_t *eco2, uint16_t *tvoc);
int sgp30_set_baseline(sgp30_t *s, uint16_t eco2, uint16_t tvoc);
/* Absolute humidity in g/m^3, NOT relative humidity percent. 0 disables it. */
int sgp30_set_humidity(sgp30_t *s, float absolute_g_m3);
