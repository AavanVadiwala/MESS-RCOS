#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESS_MESH_MAX_TEXT 200
#define MESS_MESH_HEADER_SIZE 40
#define MESS_MESH_MAX_FRAME (MESS_MESH_HEADER_SIZE + MESS_MESH_MAX_TEXT)
#define MESS_MESH_MAX_ORIGINS 32
#define MESS_MESH_MAX_READINGS (MESS_MESH_MAX_ORIGINS * 3)
#define MESS_MESH_MAX_HOPS 8
#define MESS_MESH_LIFETIME_MS 10000
#define MESS_MESH_HISTORY_MS 60000
#define MESS_MESH_HELLO 0
#define MESS_MESH_READING 1
#define MESS_SENSOR_BME280 0
#define MESS_SENSOR_SGP30 1
#define MESS_SENSOR_BH1750 2

typedef struct {
    uint8_t type, sensor, hops, origin[6];
    uint64_t session, measured_us;
    uint32_t sequence, age_ms;
    size_t length;
    char text[MESS_MESH_MAX_TEXT + 1];
} mess_mesh_record_t;

typedef struct {
    bool used;
    uint8_t mac[6];
    uint64_t session, retired[4], seen_ms;
    unsigned retired_count, retired_next;
} mess_mesh_origin_t;

typedef struct {
    bool used, pending;
    mess_mesh_record_t record;
    uint64_t received_ms, due_ms;
} mess_mesh_entry_t;

typedef struct {
    mess_mesh_origin_t origins[MESS_MESH_MAX_ORIGINS];
    mess_mesh_entry_t entries[MESS_MESH_MAX_READINGS];
} mess_mesh_cache_t;

typedef enum {
    MESS_MESH_ACCEPTED, MESS_MESH_IGNORED, MESS_MESH_INVALID, MESS_MESH_FULL
} mess_mesh_result_t;

/* Wire integers use little endian, independent of struct padding. */
size_t mess_mesh_encode(const mess_mesh_record_t *record, uint8_t *frame, size_t capacity);
bool mess_mesh_decode(const uint8_t *frame, size_t length, mess_mesh_record_t *record);
/* Single task owns the cache. Pending older readings are replaced by newer ones. */
mess_mesh_result_t mess_mesh_accept(mess_mesh_cache_t *cache,
    const mess_mesh_record_t *record, uint64_t now_ms, uint32_t delay_ms);
/* Consume one due transmission. local records retain hop 0; relays increment it. */
bool mess_mesh_next(mess_mesh_cache_t *cache, uint64_t now_ms,
    const uint8_t own_mac[6], mess_mesh_record_t *record);
