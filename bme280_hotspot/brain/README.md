# MESS BME280 gateway

Runs on a second ESP32 (the "brain") with no sensor wired to it. It creates a WPA2 Wi-Fi hotspot (default name `MESS-BME280`, address `192.168.4.1`) for battery-powered sensor nodes (BME280, BH1750 and SGP30 on one I2C bus). Each reading a node POSTs to `http://192.168.4.1/readings` is printed as one `MESS_BME_JSON {...}` line on the gateway's USB serial. The live site's **Connect brain (USB)** button reads those lines in Chrome or Edge on the computer the brain is plugged into.

The laptop does not join this hotspot, so it can stay on its usual network. No sensor is wired to the gateway.

## Graphs with no computer

The brain also serves its own graph page, so both boards can run from any USB power source (phone charger, power bank) with no computer attached:

1. Power the brain and the sensor board.
2. On a phone or laptop, join the Wi-Fi network `MESS-BME280` with the hotspot password.
3. Open **http://192.168.4.1/** (type the `http://`).

The page shows live temperature, humidity, pressure, light, TVOC and eCO₂ graphs (1, 5 or 10 minutes) with one coloured line per sensor, plus a table of each sensor's latest values, signal strength, and readings received and missed. The brain keeps the last 1,200 readings in memory (10 minutes for two sensors), and a restart clears them. While joined, that device has no internet: the hotspot is not connected to anything. Phones may warn about this or switch back to another network; choose to stay connected. The hotspot takes eight devices in total, sensors and viewers together.

## Live website through a visitor's phone (no internet on the brain)

The brain has no internet of its own, but the page at http://192.168.4.1/ can relay readings to the [live website](https://orangetangy.github.io/mess-gas-sensor/) through the internet of whoever is viewing it.

- **iPhone (tested):** join `MESS-BME280`, keep mobile data on, and open `http://192.168.4.1` in Safari. iOS keeps local traffic on the hotspot and sends the upload over mobile data. A "Live site" line under the banner counts readings sent every 10 seconds.
- **Android:** the page loads only with mobile data off, so it cannot upload. Graphs still work.
- **Only while the page is open:** each visitor also uploads the brain's backlog (about 20 minutes for one sensor), and Supabase ignores duplicates. Readings older than that are lost and show as gaps on the site.

Setup: in Supabase, run `supabase/schema.sql`, then a git-ignored `supabase/visitor_key.local.sql` that stores the SHA-256 hash of `CONFIG_MESS_GW_VISITOR_KEY` with label `visitor`. Put the key itself in the brain's `sdkconfig.local` next to `CONFIG_MESS_GW_SUPABASE_URL` and `CONFIG_MESS_GW_SUPABASE_KEY`. The brain hands all three to the page from `/relay`, so anyone on the hotspot can read the visitor key. It is separate from the brain's own upload key; revoke it with `delete from private.upload_keys where label = 'visitor';`.

## Fully wireless: upload to the website over the internet

With an uplink set, the brain also joins an internet Wi-Fi (eduroam, or any 2.4 GHz WPA2 network) while keeping its hotspot, and uploads readings to Supabase every 10 seconds. The [website](https://orangetangy.github.io/mess-gas-sensor/) then shows them from anywhere, about 10–20 seconds behind, and both boards can run on power banks. While the internet is down, the brain keeps the last 15 minutes and uploads them once it is back. Older readings are dropped. The hotspot moves to the internet network's channel; sensors find it again on their own.

One-time setup:

1. **Supabase:** in your project's **SQL Editor**, run [`supabase/schema.sql`](../supabase/schema.sql), then the git-ignored `supabase/upload_key.local.sql`. That file stores only a hash of the brain's upload key. The public (publishable/anon) key can then only call the upload and read functions; it cannot read or change tables directly, and uploads also need the brain's key.
2. **Brain settings:** in `brain/sdkconfig.local` (git-ignored), set:
   - `CONFIG_MESS_GW_SUPABASE_URL`: the Project URL, e.g. `https://abcd.supabase.co`.
   - `CONFIG_MESS_GW_SUPABASE_KEY`: the publishable (anon) key.
   - `CONFIG_MESS_GW_UPLOAD_KEY`: the key whose hash you stored in step 1.
   - For eduroam: `CONFIG_MESS_GW_UPLINK_SSID="eduroam"`, `CONFIG_MESS_GW_EAP_USERNAME` (e.g. `rcsid@rpi.edu`) and `CONFIG_MESS_GW_EAP_PASSWORD`.
   - For a normal network: set the SSID and `CONFIG_MESS_GW_UPLINK_PASSWORD`, and leave the eduroam username empty.
3. **Website:** put the same Project URL and publishable key in `SUPABASE` near the top of the script in `site/index.html`. Both values are public by design.
4. Build and flash the brain. Delete an existing `sdkconfig` first, so the larger app partition and the new settings take effect.

**eduroam: not working yet.** The brain uses PEAP/MSCHAPv2 and sends the login only to a server that proves it is `radiusauth.net.rpi.edu` with a certificate from a trusted public root. Tested on 2026-11-03, RPI's eduroam server presented a certificate that matches neither the roots in RPI's profile on cat.eduroam.org (outdated) nor any public root, so the brain stops before sending the password. It needs RPI's own eduroam CA certificate (ask DotCIO) before eduroam can work safely; do not disable the check. Until then, use a normal WPA2 network (e.g. a 2.4 GHz phone hotspot) or a Raspberry Pi with wired Ethernet. Any password stored on the board can be read out of its flash by whoever holds it, so use a device or project account, never a personal one where you can avoid it.

Set the hotspot name and a password of 8–63 characters in a git-ignored `sdkconfig.local` (or under **MESS BME280 gateway** in menuconfig). The gateway refuses to start an open network. Use the same values in the sensor node's `sensor/sdkconfig.local` (see [the overview](../README.md)):

```
CONFIG_MESS_GW_SSID="MESS-BME280"
CONFIG_MESS_GW_PASSWORD="choose-a-password"
```

Build and flash from this folder in an ESP-IDF terminal:

```sh
idf.py set-target esp32
idf.py -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.local" build
idf.py -p PORT flash monitor
```

The monitor shows `Hotspot "MESS-BME280" on channel 6`, then `Node ... joined` and one `MESS_BME_JSON` line per second for each node. Close the monitor before pressing **Connect brain (USB)**; only one program can read the port. Up to eight devices (sensors and viewers together) can join. The gateway forwards printable ASCII bodies up to 512 bytes and leaves checking to whatever reads them.

## Flash a computer that has no ESP-IDF

Build on a computer that has ESP-IDF, then make a single image:

```sh
idf.py merge-bin -o gateway-merged.bin
```

Copy `build/gateway-merged.bin` to the other computer by a private route. It contains the hotspot password, so do not post or commit it. There, in Chrome or Edge, open [Espressif's web flasher](https://espressif.github.io/esptool-js/), press **Connect** and pick the ESP32's port, set **Flash Address** to `0x0`, choose the file and press **Program**. Press the board's EN/RESET button when it finishes. This erases whatever the board was running.

Then open the [live site](https://orangetangy.github.io/mess-gas-sensor/) in Chrome or Edge, press **Connect brain (USB)** and pick the port. The web flasher must be closed or disconnected first; only one program can read the port.

Validation: on 2026-10-20 a classic ESP32 (ESP-IDF 6.1, macOS) running this gateway received one reading per second from a BME280 node about 30 cm away (RSSI about −25 dBm), and the dashboard plotted them. Range, battery life and multi-node operation are untested.
