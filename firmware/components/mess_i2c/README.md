# MESS I2C component

Small synchronous wrapper around ESP-IDF's modern I2C master driver. Public API is in `include/mess_i2c.h`: init, deinit, probe, and transfer. The component is independent of the console and sensor-specific decoding.

Configure defaults under `idf.py menuconfig` > Component config > MESS I2C (or find MESS I2C with menuconfig search). Defaults: SDA GPIO21, SCL GPIO22, 100 kHz. Firmware initializes the bus at startup. Use proper external pull-ups to 3.3 V; enabled internal weak pull-ups are not a substitute for a correctly wired bus.

Only normal 7-bit addresses 0x08..0x77 are accepted. Transfers are limited to 64 bytes per direction with a 100 ms timeout. Raw reads do not insert a register byte. Write-read uses a repeated START between the write and read. Device handles are created per transaction and removed afterward. There is no asynchronous queue and no application mutex: one caller owns this bus. Serialize entire command/wait/read sequences if adding another task later.

Returns ESP-IDF errors to the caller. Probe returning ESP_ERR_NOT_FOUND means no ACK; a timeout means a bus problem, not simply an absent sensor. This layer does not identify sensors, apply calibration, poll automatically, store readings, or implement mesh networking.
