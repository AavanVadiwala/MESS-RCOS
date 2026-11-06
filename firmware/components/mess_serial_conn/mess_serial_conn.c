#include "mess_serial_conn.h"

#include <inttypes.h>
#include <stdio.h>

static void format_mac(const uint8_t mac[6], char output[18])
{
    snprintf(output, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mess_serial_conn_write(const mess_mesh_record_t *record)
{
    char origin[18];
    format_mac(record->origin, origin);

    /* Mesh validation limits readings to printable ASCII. */
    char reading[2 * MESS_MESH_MAX_TEXT + 1];
    char *out = reading;
    for (size_t i = 0; i < record->length; ++i) {
        char c = record->text[i];
        if (c == '"' || c == '\\') *out++ = '\\';
        *out++ = c;
    }
    *out = '\0';

    printf("MESS_READING {\"mac\":\"%s\",\"sensor\":%u,\"time_us\":\"%" PRIu64
        "\",\"session\":\"%" PRIu64 "\",\"sequence\":%" PRIu32
        ",\"reading\":\"%s\",\"via\":null,\"hops\":null,\"rssi\":null}\n",
        origin, (unsigned)record->sensor, record->measured_us, record->session,
        record->sequence, reading);
    fflush(stdout);
}
