#pragma once
#include <stddef.h>
#include "esp_err.h"

/* A failed read has count=0; values are meaningful only when status is ESP_OK. */
typedef struct {
    esp_err_t status;
    size_t count;
    float values[3];
    const char *field_test_name; /* Stub-only text; not a physical measurement. */
} sensor_reading_t;
