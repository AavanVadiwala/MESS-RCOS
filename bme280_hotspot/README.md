# Sensor hotspot: BME280 + BH1750 + SGP30 → brain → phone → live site

A self-contained setup, separate from the mesh firmware in `firmware/`. Two classic ESP32 boards run from any USB power (charger or power bank), no computer needed:

```text
sensor ESP32 + I2C   --Wi-Fi-->  brain ESP32 hotspot "MESS-BME280"  --Wi-Fi-->  phone at http://192.168.4.1
                                                                                      |
                                                                          mobile data (iPhone)
                                                                                      v
                                                                       Supabase  -->  live site
```

- **Sensor** (`sensor/`): once a second reads whichever of a **BME280** (temperature, humidity, pressure), **BH1750** (light) and **SGP30** (TVOC, eCO₂) are on its I2C bus, and POSTs the values to the brain. Any of the three may be missing; the others keep working.
- **Brain** (`brain/`): runs a WPA2 hotspot, keeps the last 1,200 readings, and serves a live graph page at http://192.168.4.1/. While an iPhone has that page open, the page also uploads readings to Supabase over the phone's mobile data.
- **Live site** (`site/index.html`): a static page that graphs the Supabase readings from anywhere. It is currently hosted at https://orangetangy.github.io/mess-gas-sensor/; any static host works.
- **Database** (`supabase/schema.sql`): tables and the only functions the public key may call.

eCO₂ is the SGP30's estimate from VOCs, not a direct CO₂ measurement; gas values start after the SGP30's 15-second warm-up.

## Quick start

You need two ESP32 boards, USB data cables, and [ESP-IDF 6.1](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/). Wire the sensors to one board: 3V3, GND, SDA and SCL shared by all of them, on GPIO21/22 (or 25/26; the firmware finds them). Then, in an ESP-IDF terminal in this folder, plug in one board at a time:

```sh
./setup.sh brain     # Mac/Linux: the board with no sensors
./setup.sh sensor    # the board with the sensors
```

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1 brain    # Windows
powershell -ExecutionPolicy Bypass -File .\setup.ps1 sensor
```

The first run asks for a hotspot name and password (and, optionally, the visitor key for uploading to the live site) and saves them in the git-ignored `hotspot.local`, so every board you flash afterwards matches. The script finds the board's USB port (or asks which one when several are plugged in, or take `PORT` / `-Port COM5`), builds, flashes and shows the board starting; press **Ctrl+]** to stop watching. Then power both boards from anything USB and follow **Demo** below. The rest of this page is the same setup done by hand.

## Demo

1. Power both boards.
2. On an iPhone, join the Wi-Fi network `MESS-BME280` with the hotspot password. Keep mobile data on.
3. In Safari, open **http://192.168.4.1** (type the `http://`). Ignore "no internet connection"; that is expected.
4. The page shows live graphs. A "Live site" line under the banner counts readings uploaded; they appear on the live site within about 10–20 seconds.

Android phones load the page only with mobile data off, so they show graphs but cannot upload. Uploads happen only while a page is open; the brain's backlog (about 20 minutes for one sensor) is uploaded when someone opens it, and older readings show as gaps.

## Setup

Needs ESP-IDF 6.1 and a Supabase project. All `sdkconfig.local` and `*.local.sql` files are git-ignored: never commit them, and never share built `.bin` files, because they contain the passwords.

**1. Supabase.** In the SQL Editor run `supabase/schema.sql`. Re-running it is safe and upgrades an older BME280-only database in place, keeping its readings; without the upgrade, light and gas values are dropped. Choose a long random visitor key and store only its hash by running (keep this in a git-ignored `supabase/visitor_key.local.sql`):

```sql
insert into private.upload_keys (key_hash, label)
values (encode(extensions.digest('YOUR-VISITOR-KEY', 'sha256'), 'hex'), 'visitor');
```

See [brain/README.md](brain/README.md) for the brain's own optional internet uplink, which uses a separate upload key.

Steps 2–4 are what `setup.sh` / `setup.ps1` do for you.

**2. Sensor** (`sensor/sdkconfig.local`):

```
CONFIG_MESS_BME_WIFI_SSID="MESS-BME280"
CONFIG_MESS_BME_WIFI_PASSWORD="same hotspot password, 8-63 characters"
CONFIG_MESS_BME_SERVER_URL="http://192.168.4.1/readings"
CONFIG_MESS_BME_SDA=21
CONFIG_MESS_BME_SCL=22
```

Wire all sensors to the same 3V3, GND, SDA and SCL with the power off. SDA and SCL are optional: the sensor tries them first, then GPIO21/22 and 25/26, and uses the first pair where a sensor answers. At start-up the sensor logs every I2C address it finds: BME280 0x76 or 0x77, BH1750 0x23 or 0x5C, SGP30 0x58. When both are fitted, the BME280's humidity is fed to the SGP30 for compensation.

**3. Brain** (`brain/sdkconfig.local`):

```
CONFIG_MESS_GW_SSID="MESS-BME280"
CONFIG_MESS_GW_PASSWORD="same hotspot password"
CONFIG_MESS_GW_SUPABASE_URL="https://YOUR-PROJECT.supabase.co"
CONFIG_MESS_GW_SUPABASE_KEY="your publishable (anon) key"
CONFIG_MESS_GW_VISITOR_KEY="the visitor key from step 1"
```

Anyone on the hotspot can read the visitor key from the brain, so keep it separate from other keys. Revoke it with `delete from private.upload_keys where label = 'visitor';`.

**4. Build and flash each board.** From `sensor/`, then `brain/`, in an ESP-IDF terminal:

```sh
idf.py set-target esp32
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local" build
idf.py -p PORT flash monitor
```

`SDKCONFIG_DEFAULTS` applies only when `sdkconfig` is first created, so delete `sdkconfig` after changing `sdkconfig.local`. The brain's monitor should show `Hotspot "MESS-BME280" on channel 6` and then `Node ... joined`; the sensor's should list its I2C devices, then `BME280 found`, `BH1750 found`, `SGP30 found` and `Connected, IP 192.168.4.2`. Exit the monitor with **Ctrl+]**.

**5. Live site.** Put the same Supabase URL and publishable key in `SUPABASE` near the top of the script in `site/index.html`, then host that file. Both values are public by design: the database only lets the public key call the read and upload functions, and uploads also need the visitor or brain key.

## Status

Tested on 2026-11-13 with two classic ESP32 boards (ESP-IDF 6.1) and a BME280 (0x77), BH1750 (0x23) and SGP30 (0x58) on GPIO21/22: the sensor joined the hotspot within 3 seconds, the brain received all six values about once a second at −36 dBm, and an iPhone on the hotspot relayed them to the live site, which graphed light, TVOC and eCO₂. The iPhone relay was tested by the team; range, battery life and Android uploads are untested.
