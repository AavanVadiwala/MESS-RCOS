#pragma once

#include "mess_mesh.h"

/* Emit one prefixed JSON line on the ESP-IDF console. */
void mess_serial_conn_write(const mess_mesh_record_t *record);
