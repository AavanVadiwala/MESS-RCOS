# BME280 hotspot: sensor → brain → phone → live site

A self-contained setup, separate from the mesh firmware in `firmware/`. Two classic ESP32 boards run from any USB power (charger or power bank), no computer needed:

```text
sensor ESP32 + BME280  --Wi-Fi-->  brain ESP32 hotspot "MESS-BME280"  --Wi-Fi-->  phone at http://192.168.4.1
                                                                                      |
                                                                          mobile data (iPhone)
                                                                                      v
                                                                       Supabase  -->  live site
```

- **Sensor** (`sensor/`): reads temperature, humidity and pressure once a second and POSTs each reading to the brain.
- **Brain** (`brain/`): runs a WPA2 hotspot, keeps the last 1,200 readings, and serves a live graph page at http://192.168.4.1/. While an iPhone has that page open, the page also uploads readings to Supabase over the phone's mobile data.
- **Live site** (`site/index.html`): a static page that graphs the Supabase readings from anywhere. It is currently hosted at https://orangetangy.github.io/mess-gas-sensor/; any static host works.
- **Database** (`supabase/schema.sql`): tables and the only functions the public key may call.

BME280 does not measure gas.

## Demo

1. Power both boards.
2. On an iPhone, join the Wi-Fi network `MESS-BME280` with the hotspot password. Keep mobile data on.
3. In Safari, open **http://192.168.4.1** (type the `http://`). Ignore "no internet connection"; that is expected.
4. The page shows live graphs. A "Live site" line under the banner counts readings uploaded; they appear on the live site within about 10–20 seconds.

Android phones load the page only with mobile data off, so they show graphs but cannot upload. Uploads happen only while a page is open; the brain's backlog (about 20 minutes for one sensor) is uploaded when someone opens it, and older readings show as gaps.

## Setup

Needs ESP-IDF 6.1 and a Supabase project. All `sdkconfig.local` and `*.local.sql` files are git-ignored: never commit them, and never share built `.bin` files, because they contain the passwords.

**1. Supabase.** In the SQL Editor run `supabase/schema.sql`. Choose a long random visitor key and store only its hash by running (keep this in a git-ignored `supabase/visitor_key.local.sql`):

```sql
insert into private.upload_keys (key_hash, label)
values (encode(extensions.digest('YOUR-VISITOR-KEY', 'sha256'), 'hex'), 'visitor');
```

See [brain/README.md](brain/README.md) for the brain's own optional internet uplink, which uses a separate upload key.

**2. Sensor** (`sensor/sdkconfig.local`):

```
CONFIG_MESS_BME_WIFI_SSID="MESS-BME280"
CONFIG_MESS_BME_WIFI_PASSWORD="same hotspot password, 8-63 characters"
CONFIG_MESS_BME_SERVER_URL="http://192.168.4.1/readings"
CONFIG_MESS_BME_SDA=21
CONFIG_MESS_BME_SCL=22
```

Set SDA and SCL to the GPIOs your BME280 is actually wired to (our test board uses 25 and 26). Wire 3V3, GND, SDA and SCL with the power off; the firmware probes I2C addresses 0x76 and 0x77.

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

`SDKCONFIG_DEFAULTS` applies only when `sdkconfig` is first created, so delete `sdkconfig` after changing `sdkconfig.local`. The brain's monitor should show `Hotspot "MESS-BME280" on channel 6` and then `Node ... joined`; the sensor's should show `BME280 found` and `Connected, IP 192.168.4.2`. Exit the monitor with **Ctrl+]**.

**5. Live site.** Put the same Supabase URL and publishable key in `SUPABASE` near the top of the script in `site/index.html`, then host that file. Both values are public by design: the database only lets the public key call the read and upload functions, and uploads also need the visitor or brain key.

## Status

Tested on 2026-11-06 with two classic ESP32 boards (ESP-IDF 6.1): the sensor joined the hotspot within 2 seconds and the brain received about one reading a second at −42 dBm with the boards close together. The iPhone relay was tested by the team; range, battery life and Android uploads are untested.
