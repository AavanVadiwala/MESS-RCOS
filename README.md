# MESS

This branch makes building and flashing an ESP32 easier and brings tools adapted from several ESP-IDF examples into a single firmware image. The goal is one flash containing I2C tools, networking, and mesh tools, accessible through a common MCU terminal.

This branch includes `i2ctools`, long-range ESP-NOW board-to-board communication (250 Kbps), and a Wi-Fi hotspot with a browser terminal. All boards listen at boot and print incoming ESP-NOW messages automatically on USB/UART. Mesh relaying is not implemented. Uses ESP-IDF v6.1.

## Install and open the terminal

Run the script from normal PowerShell in `C:/MESS`, passing your board's serial port. You do not need to open or manually activate an ESP-IDF terminal:

```powershell
.\build_and_flash.ps1 -Port COM10
```

This builds and flashes the combined firmware. To also open the MCU terminal afterward:

```powershell
.\build_and_flash.ps1 -Port COM10 -Monitor
```

Replace COM10 with your board's port. The script reuses an active ESP-IDF environment or automatically loads the installed environment from `C:/Espressif/tools/eim_idf.json`. It uses the local VS Code ESP-IDF selection when available. ESP-IDF itself must already be installed.

Check setup without building or flashing:

```powershell
.\build_and_flash.ps1 -CheckEnvironment
```

`-Monitor` opens the USB/UART MCU terminal at 115200 baud after flashing. Exit with Ctrl+]. Close an existing monitor before flashing so the serial port is free. Omit `-Monitor` to build and flash only.

For a different installation, pass `-ActivationScript 'path/to/activation.ps1'` or set `MESS_IDF_ACTIVATE`. If PowerShell blocks script execution, run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build_and_flash.ps1 -Port COM4 -Monitor
```

To monitor an already flashed board without rebuilding, use `idf.py -p COM4 monitor` from `firmware` in an activated ESP-IDF terminal. The repository has one standalone script: `build_and_flash.ps1`.

At `mess>` choose `i2ctools`, `comm`, or `wifi`. Each accepts `help` and `exit`. See [firmware/CONSOLES.md](firmware/CONSOLES.md) for commands and testing.

## Components

- `mess_i2c`: reusable I2C bus operations.
- `mess_i2ctools`: interactive I2C commands.
- `mess_wifi`: shared AP+station radio initialization, hotspot, status and channel control.
- `mess_comm`: existing ESP-NOW communication using the shared station interface.
- `mess_laptop`: raw-line UDP broadcasts and a browser message viewer.
- `mess_commands`: serializes hardware commands from the browser and USB terminals.

The Wi-Fi hotspot, browser terminal, and ESP-NOW listener start automatically at boot. Both wireless modes share channel 1 by default. The station interface supports Espressif LR and each ESP-NOW destination, including broadcast, is explicitly configured for 250 Kbps LR transmission. The hotspot keeps ordinary Wi-Fi compatibility. This is one-hop communication, not mesh networking; range must be verified at your site.

Flash this same firmware onto each ESP32. In one board's USB terminal, enter `comm`, then `broadcast "hello from this board"`. All other listening boards in radio range on the same channel print `[ESP-NOW RX]` with the sender MAC, RSSI, and message, even while sitting at `mess>`. `receive` optionally replays the last eight messages, including from the browser terminal. Incoming messages can interrupt the visual USB prompt; the line being typed is retained. Broadcast delivery is best effort, without acknowledgements or automatic retries.

Wi-Fi and HTTP setup use patterns from Espressif's Public Domain/CC0 examples in `C:/test_mc/esp-idf`: `wifi/getting_started/softAP`, `protocols/http_server/simple`, and `protocols/sockets/udp_client`. MESS adds its own shared lifecycle, terminal commands, history, and viewer.
