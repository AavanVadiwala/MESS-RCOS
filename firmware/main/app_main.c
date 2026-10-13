#include <inttypes.h>
#include <stdio.h>
#include "bme280.h"
#include "sgp30.h"
#include "bh1750.h"
#include "mess_broadcast.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    const char *name;
    const char *units[3];
    size_t expected_count;
    esp_err_t (*init)(void);
    esp_err_t (*read)(sensor_reading_t *);
    esp_err_t initialization;
} sensor_t;

static sensor_t sensors[] = {
    {"bme280", {"C", "Pa", "%RH"}, 3, bme280_init, bme280_read, ESP_FAIL},
    {"sgp30", {"ppm_CO2eq", "ppb_TVOC"}, 2, sgp30_init, sgp30_read, ESP_FAIL},
    {"bh1750", {"lux"}, 1, bh1750_init, bh1750_read, ESP_FAIL},
};

static void acquire(void *argument)
{
    (void)argument;
    for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); ++i)
        sensors[i].initialization = sensors[i].init();
    /* Spread boot-time transmissions across boards while retaining a 1 s cadence. */
    vTaskDelay(pdMS_TO_TICKS(20 + esp_random() % 980));
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        for (size_t i = 0; i < sizeof(sensors) / sizeof(sensors[0]); ++i) {
            sensor_t *sensor = &sensors[i];
            sensor_reading_t reading = {0};
            uint64_t measured_us = (uint64_t)esp_timer_get_time();
            esp_err_t status = sensor->read(&reading);
            if (sensor->initialization != ESP_OK) status = sensor->initialization;
            if (status == ESP_OK) status = reading.status;
            if (status == ESP_OK && reading.count != sensor->expected_count)
                status = ESP_ERR_INVALID_SIZE;
            char text[MESS_BROADCAST_MAX_TEXT + 1];
            int used = snprintf(text, sizeof(text), "sensor=%s status=%s",
                sensor->name, reading.field_test_name ? "placeholder" : esp_err_to_name(status));
            if (reading.field_test_name) {
                used += snprintf(text + used, sizeof(text) - used, " value=%s", reading.field_test_name);
            } else if (status == ESP_OK) {
                for (size_t j = 0; j < reading.count && used < (int)sizeof(text); ++j)
                    used += snprintf(text + used, sizeof(text) - used, " %s=%.6g",
                        sensor->units[j], (double)reading.values[j]);
            } else if (used < (int)sizeof(text)) {
                used += snprintf(text + used, sizeof(text) - used, " value=null");
            }
            if (used > 0 && used < (int)sizeof(text)) {
                esp_err_t error = mess_broadcast_publish((uint8_t)i, measured_us, text, (size_t)used);
                if (error != ESP_OK) printf("Broadcast submission: %s\n", esp_err_to_name(error));
            }
        }
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(mess_broadcast_start());
    ESP_ERROR_CHECK(xTaskCreate(acquire, "sensor_read", 4096, NULL, 2, NULL)
        == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
