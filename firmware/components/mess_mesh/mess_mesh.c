#include "mess_mesh.h"
#include <string.h>

static bool valid(const mess_mesh_record_t *r)
{
    if (!r || r->type > MESS_MESH_READING || !r->session ||
        r->hops > MESS_MESH_MAX_HOPS || r->length > MESS_MESH_MAX_TEXT)
        return false;
    if (r->type == MESS_MESH_HELLO)
        return !r->length && !r->hops && r->sensor == 255 && !r->age_ms;
    if (r->sensor > MESS_SENSOR_BH1750 || !r->length ||
        r->age_ms >= MESS_MESH_LIFETIME_MS) return false;
    for (size_t i = 0; i < r->length; ++i)
        if ((unsigned char)r->text[i] < 32 || (unsigned char)r->text[i] > 126)
            return false;
    return true;
}

static void put(uint8_t *p, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i) p[i] = (uint8_t)(value >> (8 * i));
}
static uint64_t get(const uint8_t *p, unsigned bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= (uint64_t)p[i] << (8 * i);
    return value;
}

size_t mess_mesh_encode(const mess_mesh_record_t *r, uint8_t *frame, size_t capacity)
{
    if (!valid(r) || !frame || capacity < MESS_MESH_HEADER_SIZE + r->length) return 0;
    memset(frame, 0, MESS_MESH_HEADER_SIZE);
    memcpy(frame, "MESS", 4);
    frame[4] = 2; frame[5] = r->type; frame[6] = (uint8_t)r->length; frame[7] = r->hops;
    memcpy(frame + 8, r->origin, 6);
    put(frame + 14, r->session, 8); put(frame + 22, r->sequence, 4);
    put(frame + 26, r->measured_us, 8); put(frame + 34, r->age_ms, 4);
    frame[38] = r->sensor;
    memcpy(frame + MESS_MESH_HEADER_SIZE, r->text, r->length);
    return MESS_MESH_HEADER_SIZE + r->length;
}

bool mess_mesh_decode(const uint8_t *frame, size_t length, mess_mesh_record_t *r)
{
    if (!frame || !r || length < MESS_MESH_HEADER_SIZE || length > MESS_MESH_MAX_FRAME ||
        memcmp(frame, "MESS", 4) || frame[4] != 2 || frame[39] ||
        frame[6] != length - MESS_MESH_HEADER_SIZE) return false;
    *r = (mess_mesh_record_t){.type = frame[5], .hops = frame[7],
        .sensor = frame[38], .session = get(frame + 14, 8),
        .sequence = (uint32_t)get(frame + 22, 4), .measured_us = get(frame + 26, 8),
        .age_ms = (uint32_t)get(frame + 34, 4), .length = frame[6]};
    memcpy(r->origin, frame + 8, 6);
    memcpy(r->text, frame + MESS_MESH_HEADER_SIZE, r->length);
    return valid(r);
}

mess_mesh_result_t mess_mesh_accept(mess_mesh_cache_t *cache,
    const mess_mesh_record_t *r, uint64_t now, uint32_t delay)
{
    if (!cache || !valid(r) || r->type != MESS_MESH_READING) return MESS_MESH_INVALID;
    int origin = -1, empty_origin = -1, entry = -1, empty_entry = -1;
    for (unsigned i = 0; i < MESS_MESH_MAX_ORIGINS; ++i) {
        mess_mesh_origin_t *o = &cache->origins[i];
        if (o->used && now - o->seen_ms >= MESS_MESH_HISTORY_MS) {
            for (unsigned j = 0; j < MESS_MESH_MAX_READINGS; ++j)
                if (cache->entries[j].used &&
                    !memcmp(cache->entries[j].record.origin, o->mac, 6))
                    cache->entries[j].used = false;
            o->used = false;
        }
        if (o->used && !memcmp(o->mac, r->origin, 6)) origin = (int)i;
        if (!o->used && empty_origin < 0) empty_origin = (int)i;
    }
    if (origin >= 0) {
        mess_mesh_origin_t *o = &cache->origins[origin];
        if (o->session != r->session) {
            for (unsigned i = 0; i < o->retired_count; ++i)
                if (o->retired[i] == r->session) return MESS_MESH_IGNORED;
        }
    } else if (empty_origin < 0) return MESS_MESH_FULL;
    for (unsigned i = 0; i < MESS_MESH_MAX_READINGS; ++i) {
        mess_mesh_entry_t *e = &cache->entries[i];
        if (e->used && !memcmp(e->record.origin, r->origin, 6) &&
            e->record.sensor == r->sensor) entry = (int)i;
        if (!e->used && empty_entry < 0) empty_entry = (int)i;
    }
    if (entry >= 0 && cache->entries[entry].record.session == r->session) {
        const mess_mesh_record_t *old = &cache->entries[entry].record;
        uint32_t forward = r->sequence - old->sequence;
        if (!forward || forward >= UINT32_C(0x80000000) ||
            r->measured_us < old->measured_us) return MESS_MESH_IGNORED;
    }
    if (entry < 0 && empty_entry < 0) return MESS_MESH_FULL;
    if (origin < 0) {
        origin = empty_origin;
        cache->origins[origin] = (mess_mesh_origin_t){.used = true, .session = r->session};
        memcpy(cache->origins[origin].mac, r->origin, 6);
    }
    mess_mesh_origin_t *o = &cache->origins[origin];
    if (o->session != r->session) {
        o->retired[o->retired_next] = o->session;
        o->retired_next = (o->retired_next + 1) % 4;
        if (o->retired_count < 4) ++o->retired_count;
        o->session = r->session;
        /* A reboot invalidates every pending sensor reading from the old session. */
        for (unsigned i = 0; i < MESS_MESH_MAX_READINGS; ++i)
            if (cache->entries[i].used &&
                !memcmp(cache->entries[i].record.origin, r->origin, 6))
                cache->entries[i].used = false;
    }
    o->seen_ms = now;
    if (entry < 0) entry = empty_entry;
    cache->entries[entry] = (mess_mesh_entry_t){
        .used = true, .pending = true, .record = *r, .received_ms = now, .due_ms = now + delay
    };
    return MESS_MESH_ACCEPTED;
}

bool mess_mesh_next(mess_mesh_cache_t *cache, uint64_t now,
    const uint8_t own_mac[6], mess_mesh_record_t *record)
{
    if (!cache || !own_mac || !record) return false;
    for (unsigned i = 0; i < MESS_MESH_MAX_READINGS; ++i) {
        mess_mesh_entry_t *e = &cache->entries[i];
        if (!e->used || !e->pending || now < e->due_ms) continue;
        e->pending = false; /* One submission attempt; errors do not create retry loops. */
        bool local = !memcmp(e->record.origin, own_mac, 6);
        uint64_t age = e->record.age_ms + (now - e->received_ms);
        if (age >= MESS_MESH_LIFETIME_MS || (!local && e->record.hops >= MESS_MESH_MAX_HOPS))
            continue;
        *record = e->record;
        record->age_ms = (uint32_t)age;
        if (!local) ++record->hops;
        return true;
    }
    return false;
}
