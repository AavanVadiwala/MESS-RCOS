#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MESS_I2C_MAX_TRANSFER 64

/* One bus, one caller at a time. Serialize access if adding polling tasks.
 * Pins must be output-capable and available on the physical board.
 * External SDA/SCL pull-ups are required for a reliable bus.
 */
esp_err_t mess_i2c_init(int sda, int scl, uint32_t frequency_hz);
esp_err_t mess_i2c_deinit(void);
/* ESP_ERR_NOT_FOUND means no ACK; timeouts are reported separately. */
esp_err_t mess_i2c_probe(uint8_t address);
/* tx only: write; rx only: raw read; both: write then repeated-START read.
 * No register or command bytes are inserted automatically.
 */
esp_err_t mess_i2c_transfer(uint8_t address,
                          const uint8_t *tx, size_t tx_length,
                          uint8_t *rx, size_t rx_length);

#ifdef __cplusplus
}
#endif
