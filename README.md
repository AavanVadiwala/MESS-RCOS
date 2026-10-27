# MESS

ESP32 firmware that discovers nearby compatible boards and shares the latest
sensor readings through a mesh using ESP-NOW Long Range at 500 Kbps.

## Folder structure

```text
MESS/
  firmware/
    builds/           Generated firmware and build files
    components/       Sensor stubs, shared types, radio and mesh logic
    main/             Application startup and one-second sensor loop
    CMakeLists.txt    Project configuration
    sdkconfig         ESP-IDF settings
    sdkconfig.defaults
  LICENSE
  README.md
  build_and_flash.ps1
```

Hidden Git files remain for version control. There is no records or tests folder.

## What each part does

| Part | What it does |
|---|---|
| `main/app_main.c` | Starts the mesh, calls sensor initialization stubs, reads each sensor once per second, and queues its result for broadcasting. |
| `components/bme280` | Reads temperature, pressure, and humidity over I2C0 on GPIO 21/22 and publishes the live values once per second. |
| `components/sgp30` | Reads CO2-equivalent and TVOC over I2C0 on GPIO 21/22, initializes the air-quality algorithm, and publishes the live values once per second. |
| `components/bh1750` | Reads illuminance in lux over I2C0 on GPIO 21/22 and publishes the live value once per second. |
| `components/sensor_types` | Common result containing status, numeric values, and an optional field-test name. |
| `components/mess_mesh` | Keeps the newest reading per original MAC and sensor, rejects duplicates and older readings, and limits forwarding. |
| `components/mess_broadcast` | Sets up ESP-NOW, discovers neighbors, receives and prints readings, and broadcasts eligible readings. |
| `components/mess_hotspot` | Creates each board's laptop hotspot and hosts a live table of its accepted local and relayed readings. |
| `build_and_flash.ps1` | Builds, flashes, and monitors firmware using `firmware/builds`. |

Paths for components and main are inside `firmware/`.

Each board forwards a new reading once, preserving its original MAC and
measurement time. Random delays, an eight-relay-hop limit and an accumulated-age
limit prevent endless forwarding. Measurement times are the original board's
uptime. Remote output distinguishes the original MCU from the immediate relay.

## Field testing and remaining work

The BME280, SGP30, and BH1750 drivers now publish real sensor values; the
SGP30's first ~15 readings are fixed at 400 ppm CO2-equivalent / 0 ppb TVOC
while it warms up.

| Sensor | Status and remaining work |
|---|---|
| BME280 | Reads temperature (C), pressure (Pa), and humidity (%RH) over I2C0 on SDA GPIO 21 / SCL GPIO 22. Supports addresses `0x76` and `0x77`; verify wiring on the target hardware. |
| SGP30 | Initializes the I2C bus, confirms the sensor's serial and feature set, starts the air-quality algorithm, and reads CO2-equivalent (ppm) and TVOC (ppb) once per second. Baseline and humidity compensation handling remain to be tuned on the target platform. |
| BH1750 | Reads single-shot high-resolution illuminance in lux using the shared I2C0 bus. The sensor is addressed at `0x23` and uses a 180 ms conversion delay before reading its 16-bit result. |

The BME280 initializes I2C0 when needed; the BH1750 reuses that bus. I2C
transactions are serialized by ESP-IDF. Initialization retries and bus recovery
remain outstanding.

The mesh is implemented with best-effort delivery; acknowledgements and retries
are not implemented. Physical radio operation still needs testing. To check
relaying, arrange A–B–C so A and C cannot communicate directly, then confirm C
receives A's readings through B.

## Open the readings table

Connect your laptop to the board's `MESS-<MAC>` Wi-Fi network.
Every board uses the password **MESS12345**. Open **http://192.168.4.1/**.
The website runs on the ESP32; `localhost` on your laptop would address the laptop.
No internet or router is required.

The table refreshes every second and shows the newest 256 accepted readings.
Each UUID is the original MAC address combined with the original measurement
time in microseconds. Sensor and boot session are shown separately because
identical timestamps or restarts can repeat that UUID. Duplicate mesh copies are
not added again. History is kept in RAM, older rows are replaced when full, and
a restart clears the table.

The hotspot uses normal Wi-Fi while the station interface continues ESP-NOW
Long Range at 500 Kbps. Both use channel 1. Check simultaneous laptop access
and mesh reception on real boards after flashing.

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
