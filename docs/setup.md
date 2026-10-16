# Setup Guide

*Adapted from the mentor-provided `MESS-quickstart-guide.pdf` (screenshots omitted).*

## 1. Connect the ESP32

1. Plug the ESP32 in over USB and check that the power LED is on.
2. **Windows:** open Device Manager.
   - A new device under **Ports (COM & LPT)** (e.g. *Silicon Labs CP210x USB to UART Bridge (COM4)*) means the driver is already installed. Note the COM port.
   - A device with a yellow warning under **Other devices** (e.g. *CP2102N USB to UART Bridge Controller*) means you need the [Silicon Labs CP210x VCP driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers).
   - *macOS/Linux:* the board appears as `/dev/cu.SLAB_USBtoUART` / `/dev/ttyUSB0` (or similar).

## 2. Install the ESP-IDF toolchain

You build code on your machine, then flash it to the board. We use Espressif's official ESP-IDF toolchain through its VS Code extension. Full docs: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html>

1. Install the **ESP-IDF** extension (publisher: Espressif Systems) from the VS Code Marketplace. It adds a sidebar icon and `ESP-IDF:` commands under `Ctrl+Shift+P`.
2. Run **`ESP-IDF: Open ESP-IDF Installation Manager`** and choose the automatic install. A recent Python version is required.
3. Use the command line via **`ESP-IDF: Open ESP-IDF Terminal`**. The New Project GUI has been unreliable and prone to crashing.

## 3. Build and flash an example

1. Clone the ESP-IDF repo (examples included): <https://github.com/espressif/esp-idf/tree/v6.1>
2. In the ESP-IDF terminal:

   ```sh
   cd esp-idf/examples/get-started/hello_world
   idf.py set-target esp32
   idf.py build
   idf.py -p PORT flash      # PORT = e.g. COM4 or /dev/ttyUSB0
   ```

   A successful flash ends with `Hash of data verified.` and `Hard resetting via RTS pin...`.

## 4. Watch the serial output

```sh
idf.py -p PORT monitor
```

For `hello_world` you should see `Hello world!`, a chip summary (2 CPU cores, WiFi/BT/BLE), and a 10-second restart countdown. Exit the monitor with `Ctrl+]`.

## 5. Building this repo's firmware

Same commands, run from [`firmware/`](../firmware/):

```sh
cd firmware
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

## Reference material

- **ESP-IDF documentation** — the first place to look for anything ESP32-related (APIs, guides, datasheets).
- **Sensor datasheets/guides:** BME280 (pressure/humidity/temperature), SGP30 (air quality), BH1750 (light). Wiring and APIs in guides written for other boards may differ slightly on the ESP32.
- **Background reading:** I2C and SPI protocol explanations, and ESP32 Wi-Fi mesh topologies.
- More in [references.md](references.md).

## Soldering

Sensor headers need to be soldered. Soldering irons are in the **MercerX Lab, JEC 6th floor**; MercerX volunteers can help if you're new to it.
