# MESS

ESP32 firmware with serial and browser command terminals for I2C devices, ESP-NOW board-to-board messages, and Wi-Fi broadcasts to laptops. Uses ESP-IDF v6.1.

## Install and open the terminal

From PowerShell in `C:/MESS`, replacing COM4 with your board port:

```powershell
.\build_and_flash.ps1 -Port COM4 -Monitor
```

No ESP-IDF terminal is required. The script reuses an active ESP-IDF environment or automatically loads the installed environment from `C:/Espressif/tools/eim_idf.json`. It uses the local VS Code ESP-IDF selection when available. ESP-IDF itself must already be installed.

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

## Laptop Wi-Fi quick start

1. Enter `wifi` at `mess>`.
2. Join the laptop to the printed `MESS-xxxxxx` hotspot using password `mess-demo-123`.
3. Open `http://192.168.4.1` in your laptop browser.
4. Type any ordinary line at `wifi>` and press Enter. The browser updates automatically.

The webpage also has a command terminal. Enter `i2ctools`, then `scan` or `write-read 0x76 1 0xD0`, and see the output in the browser. `exit` returns the browser to `mess>`. You can also send full commands such as `comm status` or `i2ctools scan`. Browser and USB terminal modes are independent; their hardware operations are serialized.

Terminal output is returned directly to the requesting browser and discarded on the MCU. The display exists only in the current page, clears on reload, and can be cleared with the button. System/driver logs remain on the USB terminal. The separate broadcasts panel still uses the existing 16-message history.

Each line also goes out as a UDP broadcast on port 3333 for external receivers. The included browser terminal needs no receiver script or inbound firewall rule.

This direct hotspot connection needs no router and provides no Internet access. The password is a demo default in `mess_wifi.c`; change it and rebuild for your own use. The browser serves all connected clients; the MCU retains only 16 messages in RAM. UDP has no delivery guarantee.

## Components

- `mess_i2c`: reusable I2C bus operations.
- `mess_i2ctools`: interactive I2C commands.
- `mess_wifi`: shared AP+station radio initialization, hotspot, status and channel control.
- `mess_comm`: existing ESP-NOW communication using the shared station interface.
- `mess_laptop`: raw-line UDP broadcasts and a browser message viewer.
- `mess_commands`: serializes hardware commands from the browser and USB terminals.

The radio starts when you first enter `comm` or `wifi` and stays active when you exit a mode. Both wireless modes share the same channel. This is one-hop ESP-NOW communication, not mesh networking.

Wi-Fi and HTTP setup use patterns from Espressif's Public Domain/CC0 examples in `C:/test_mc/esp-idf`: `wifi/getting_started/softAP`, `protocols/http_server/simple`, and `protocols/sockets/udp_client`. MESS adds its own shared lifecycle, terminal commands, history, and viewer.
