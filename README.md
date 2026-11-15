# MESS

ESP32 firmware that discovers nearby compatible boards and shares the latest
sensor readings through an ESP-WIFI-MESH network configured for Long Range.

## Folder structure

```text
MESS/
  firmware/
    builds/           Generated firmware and build files
    components/       Sensor drivers, shared types, radio and mesh logic
    main/             Application startup and one-second sensor loop
    CMakeLists.txt    Project configuration
    sdkconfig         ESP-IDF settings
    sdkconfig.defaults
  software/               
    web/   data viewer web application 
    db/     postgresql database
    dockerfile.YAML docker file to start everything for the server
    README.md How to use 
  LICENSE
  README.md
  build_and_flash.ps1
```

Hidden Git files remain for version control. There is no records or tests folder.

## What each part does

| Part | What it does |
|---|---|
| `main/app_main.c` | Starts the mesh, initializes the sensors, and queues their readings or errors once per second. |
| `components/bme280` | Reads temperature, pressure, and humidity over I2C0 on GPIO 21/22 and publishes the live values once per second. |
| `components/sgp30` | Reads CO2-equivalent and TVOC over I2C0 on GPIO 21/22, initializes the air-quality algorithm, and publishes the live values once per second. |
| `components/bh1750` | Reads illuminance in lux over I2C0 on GPIO 21/22 and publishes the live value once per second. |
| `components/sensor_types` | Common result containing status, numeric values, and an optional field-test name. |
| `components/mess_mesh` | Keeps the newest reading per original MAC and sensor, rejects duplicates and older readings, and limits forwarding. |
| `components/mess_broadcast` | Starts the Wi-Fi mesh and forwards node readings to the collector, which prints them over serial. |
| `build_and_flash.ps1` | Builds, flashes, and monitors firmware using `firmware/builds`. |

Paths for components and main are inside `firmware/`.

Each node polls its own I2C sensors once per second and sends one record per
sensor to the collector. The collector also polls its local sensors and prints
local and remote records over serial, identified by their source MAC and sensor.


## Field testing and remaining work

The BME280, SGP30, and BH1750 drivers now publish real sensor values; the
SGP30's first 15 successful samples are reported as `status=warming_up value=null`
while it warms up.

| Sensor | Status and remaining work |
|---|---|
| BME280 | Reads temperature (C), pressure (Pa), and humidity (%RH) over I2C0 on SDA GPIO 21 / SCL GPIO 22. Supports addresses `0x76` and `0x77`; verify wiring on the target hardware. |
| SGP30 | Initializes the I2C bus, confirms the sensor's serial and feature set, starts the air-quality algorithm, and reads CO2-equivalent (ppm) and TVOC (ppb) once per second. Baseline and humidity compensation handling remain to be tuned on the target platform. |
| BH1750 | Reads single-shot high-resolution illuminance in lux using the shared I2C0 bus. The sensor supports addresses `0x23` and `0x5C` and uses a 180 ms conversion delay before reading its 16-bit result. |

The sensors share I2C port 0 by default, with SDI (SDA) on GPIO 21 and SCK (SCL)
on GPIO 22. To change the bus settings, open **MESS I2C** in `idf.py menuconfig`.
The BME280 publishes its readings as `C`, `Pa`, and `%RH`.

For a standalone scan, enable **Scan I2C once at boot and stop** under **MESS I2C**,
then build and flash. The serial monitor at 115200 baud prints the configured
GPIOs and responding addresses. Reset the board to repeat the scan. Disable the
setting and rebuild to resume measurements.

If a sensor gives no I2C response at startup, its record contains
`status=sensor_not_installed value=null`. A wrong chip ID or another I2C
failure carries the ESP-IDF error name. Other installed sensors continue to
produce their own records.

## Separate hotspot collection on the firmware branch

The [sensor hotspot project](https://github.com/AavanVadiwala/MESS-RCOS/tree/firmware/bme280_hotspot)
uses two ESP32 builds. Flash `bme280_hotspot/sensor` on the board wired to the
sensors and `bme280_hotspot/brain` on the gateway. The sensor probes and reads
each installed device separately, then sends the available values together in
an HTTP POST to the gateway. The gateway serves live graphs to devices on its
Wi-Fi hotspot. That project also uses BME280 humidity to compensate SGP30 gas
readings when both sensors are present.


## Build, flash and monitor

Flash the same build onto every board. Replace COM10 with your board's port.

```powershell
# Build only
./build_and_flash.ps1 -Build

# Build, flash and monitor
./build_and_flash.ps1 -Build -Port COM10 -Monitor

# Flash the existing build and monitor
./build_and_flash.ps1 -Port COM10 -Monitor

# Monitor without flashing
./build_and_flash.ps1 -Port COM10 -MonitorOnly
```

The firmware image is `firmware/builds/MESS.bin`.
