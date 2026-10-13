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
| `components/bme280` | Placeholder for temperature, pressure, and humidity readings. Returns `BME280` for field testing. |
| `components/sgp30` | Placeholder for CO2-equivalent and TVOC readings. Returns `SGP30` for field testing. |
| `components/bh1750` | Placeholder for light readings. Returns `BH1750` for field testing. |
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

Sensors do not need to be connected or initialized for field testing. Their
hardware drivers remain unimplemented. Every second the placeholders broadcast:

```text
sensor=bme280 status=placeholder value=BME280
sensor=sgp30 status=placeholder value=SGP30
sensor=bh1750 status=placeholder value=BH1750
```

These names test transmission and relaying; they are not physical measurements.

| Sensor | What needs to be implemented |
|---|---|
| BME280 | I2C setup, chip identification, calibration, conversion timing, and temperature (C), pressure (Pa), humidity (%RH). |
| SGP30 | I2C setup, air-quality initialization and warm-up, response checks, baseline/humidity handling, CO2-equivalent (ppm) and TVOC (ppb). Read once per second. |
| BH1750 | I2C setup, measurement mode, conversion timing, and conversion to lux. |

Implement a shared synchronized I2C bus, initialization retries and recovery.
When implementing each driver, clear `field_test_name` and return real readings
with their status and value count.

The mesh is implemented with best-effort delivery; acknowledgements and retries
are not implemented. Physical radio operation still needs testing. To check
relaying, arrange A–B–C so A and C cannot communicate directly, then confirm C
receives A's placeholder readings through B.

## Open the readings table

Connect your laptop to the board's `MESS-<MAC>` Wi-Fi network.
Every board uses the password **MESS12345**. Open **http://192.168.4.1/**.
The website runs on the ESP32; `localhost` on your laptop would address the laptop.
No internet or router is required.

The table refreshes every second and shows the newest 256 accepted readings,
including sensor placeholders. Each UUID is the original MAC address combined
with the original measurement time in microseconds. Sensor and boot session are
shown separately because identical timestamps or restarts can repeat that UUID.
Duplicate mesh copies are not added again. History is kept in RAM, older rows
are replaced when full, and a restart clears the table.

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
