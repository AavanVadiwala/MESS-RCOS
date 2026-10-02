#include "mess_commands.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t lock;
/* Initialize before starting the serial REPL or HTTP server. */
esp_err_t mess_commands_init(void)
{
    if (!lock) lock = xSemaphoreCreateMutex();
    return lock ? ESP_OK : ESP_ERR_NO_MEM;
}
void mess_commands_lock(void) { xSemaphoreTake(lock, portMAX_DELAY); }
void mess_commands_unlock(void) { xSemaphoreGive(lock); }
