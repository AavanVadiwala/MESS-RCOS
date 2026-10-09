# MESS MCU scanner

ESP-IDF v6.1 firmware for the original ESP32. Every board automatically broadcasts
an ESP-NOW announcement at 250 Kbps Espressif Long Range on channel 1, approximately
every 1-2 seconds with random jitter. It listens for announcements matching the
exact firmware ELF hash and refreshes its UART monitor list every three seconds.
No commands are required.

Example monitor output:

```text
Nearby MCUs (same firmware; channel 1):
MCU aa:bb:cc:dd:ee:01 : TX rate 250 Kbps (configured) | RSSI -55 dBm
MCU aa:bb:cc:dd:ee:02 : TX rate 250 Kbps (configured) | RSSI -72 dBm
```

The MAC address identifies each MCU. RSSI is the latest received announcement's
signal strength. The displayed rate is the configured physical transmission rate,
not measured throughput. Distance cannot be measured by this firmware. Boards
expire after six seconds without a received announcement; up to 32 are listed.
Discovery is one-hop, best effort, unencrypted, and not identity authentication.
Matching firmware hashes do not authenticate a sender. There is no hotspot,
browser server, I2C interface, chat console, mesh forwarding, or router connection.

Build once, then flash that same image onto every board:

```powershell
.\build_and_flash.ps1 -Build
.\build_and_flash.ps1 -Port COM10 -Monitor
```

Replace COM10 with each board's port. Exit monitor with Ctrl+]. To monitor without
flashing, use `idf.py -p COM10 monitor` from `firmware` in an activated ESP-IDF
terminal. Separately rebuilt images may have different hashes and will not appear.
The project retains the output name `hello_world.bin`.

Validation with two boards: flash the same image to both, confirm each monitor
lists the other MAC and RSSI, power one off and confirm it disappears after about
6-9 seconds, then power it on and confirm it reappears. A board running a different
image must not appear. Hardware range and reception require physical verification.
