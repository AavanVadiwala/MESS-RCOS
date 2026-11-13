To run any individual sensor, connet pins as follows:
BH1750 --> `I2C0 SDA` GPIO 21, `I2C0 SCL` GPIO 22, `VIN` --> `3.3V`, `GND` --> `GND`
BME280 --> `I2C0 SDI` GPIO 21, `I2C0 SCA` GPIO 22, `VIN` --> `3.3V`, `GND` --> `GND`
SGP30 --> `I2C0 SDA` GPIO 21, `I2C0 SCL` GPIO 22, `VIN` --> `3.3V`, `GND` --> `GND`

Then in the ESP-IDF terminal, run the following commands to build and flash the firmware:

```bash
cd firmware
idf.py build
idf.py flash
idf.py monitor

Or 
cd firmware
idf.py build flash monitor
```
This will build the firmware, flash it to the ESP32 board, and start monitoring the output. You should see sensor readings being printed in the terminal. The readings will include temperature, pressure, humidity, CO2-equivalent, TVOC, and illuminance values depending on which sensors are connected. Each reading will also include a timestamp and the MAC address of the board that generated it, along with a status message indicating whether the reading was accepted or rejected by the mesh network.

When connecting all three sensors, ensure that the I2C addresses do not conflict. The BME280 can be set to either `0x76` or `0x77`, while the SGP30 is fixed at `0x58` and the BH1750 at `0x23`. If you encounter any issues with sensor readings, double-check the wiring and I2C addresses. Also make sure that on the bread board, the BME280 is first in the line of the I2C bus, followed by the SGP30, or the BH1750. This order can help prevent address conflicts and ensure proper communication with each sensor. The BME280 is the most finicky sensor, the other two sensors can come up any point after it in the chain. The SGP30 and BH1750 can be swapped in order, but the BME280 should always be first.