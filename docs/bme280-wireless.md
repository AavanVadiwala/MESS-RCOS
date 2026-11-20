# BME280 wireless test setup and live page

How Tanay's group gets BME280 readings from a battery-powered ESP32 onto a web page. Full firmware, bridge and history: [OrangeTangy/mess-gas-sensor](https://github.com/OrangeTangy/mess-gas-sensor). The live page lives here in [`software/web/bme280-live/index.html`](../software/web/bme280-live/index.html), and is published at <https://orangetangy.github.io/mess-gas-sensor/>.

## How it works

```text
sensor ESP32 + BME280 --Wi-Fi--> brain ESP32 --USB--> any laptop: live page (Chrome/Edge)
 (power bank)                    (hotspot)   \--internet (optional)--> Supabase --> live page from anywhere
```

- **Sensor ESP32:** reads the BME280 once a second over I2C (we use SDA 25, SCL 26) and sends each reading over Wi-Fi to the brain. It runs from any USB power and reconnects on its own.
- **Brain ESP32:** no sensor wired to it. It hosts a WPA2 hotspot (`MESS-BME280`, 192.168.4.1) and passes each reading on in three ways:
  - one line per reading on its USB serial (`MESS_BME_JSON {...}`),
  - its own graph page at <http://192.168.4.1/> for anyone who joins the hotspot,
  - optionally, an upload to Supabase if it can reach the internet.
- **Several sensors:** each board is told apart by its board ID (MAC). The brain takes up to 8 devices (sensors and viewers together).
- **Live page:** a single static HTML file with no build step and no server. It shows a table with one row per sensor (temperature, humidity, pressure, signal to the brain, readings missed, last seen) and one coloured line per sensor on the temperature, humidity and pressure graphs (5 min to 24 h), plus a CSV download.

## Using the live page

1. Plug the brain into any laptop and open the page in **Chrome or Edge**. Web Serial is required; Safari and Firefox are not supported.
2. Press **Connect brain (USB)** and pick the board's port, usually "CP2102N USB to UART Bridge". Readings appear once a second. Nothing is installed or uploaded.

It also works with **this repo's mesh firmware** (`firmware/`): plug in any MESS board and Connect shows every BME280 on the mesh. It reads the `origin=… sensor=bme280 status=ESP_OK C=… Pa=… %RH=…` lines, converts Pa to hPa, and takes signal strength from relayed lines. This needs the BME280 driver from PR #9; placeholder and error lines are ignored.

When nothing is plugged in, the page shows readings from Supabase (refreshing every 10 s) if any have been uploaded, otherwise the last published copy. Supabase access is read-only through one database function; the key in the page is the public one.

## Status (Nov 2026)

- Works: sensor to brain over Wi-Fi, brain to laptop over USB, the brain's hotspot page, and multiple sensors (tested with simulated sensors; one real BME280 so far).
- Internet from the brain: **eduroam does not work.** RPI's eduroam server uses a certificate the ESP32 cannot verify, so the brain refuses to send a password, which is correct. Options: a 2.4 GHz WPA2 network such as a phone hotspot (supported now), or a Raspberry Pi on wired Ethernet as the uploader. This connects to the open question in [decisions.md](decisions.md) on how the root node reaches the server.
- Not measured yet: range between boards, battery life.
