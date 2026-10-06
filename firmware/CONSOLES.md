# MESS interactive consoles

From normal PowerShell in the repository root, `./build_and_flash.ps1 -Port COM4 -Monitor` automatically activates ESP-IDF, builds, flashes and opens the MCU terminal. Replace COM4 with your board port. For manual build or monitor only, use `idf.py build` or `idf.py -p COM4 monitor` from `firmware` in an activated ESP-IDF terminal. The UART serial monitor shows `mess>`. Enter `i2ctools`, `comm`, or `wifi` to open a mode in the same terminal, and `exit` to return. `help` inside each mode prints usage.

## I2C

At `mess>` type `i2ctools`, then:

```text
scan
write-read 0x76 1 0xD0
read 0x23 2
write 0x23 0x10
exit
```

These are MESS's command names, not the standalone Espressif example's i2cget/i2cset flags. `write-read ADDRESS READ_LENGTH BYTE...` sends the bytes then performs a repeated-START read. `read` sends no command bytes. `write` inserts no register byte. Observe sensor-specific conversion delays between separate commands. The examples are independent operations, not a universal sensor initialization sequence.

Default pins remain SDA21/SCL22 at 100 kHz; change them with menuconfig's MESS I2C settings and rebuild. Transfers allow 64 bytes in each direction. The console component `mess_i2ctools` uses the reusable `mess_i2c` bus component. No automatic polling runs concurrently.

## Wireless communication between two boards

### Find nearby boards and sample link quality

From the USB `mess>` prompt enter `comm`, then `discover`. From the browser command terminal run `comm discover`.

For continuously refreshed USB output, enter `discover live` in `comm`. Run `discover stop` to stop (or `comm discover stop` in the browser). Each scan takes about five seconds, followed by a one-second pause. Stopping or running another command can wait for the current scan to finish. Updates continue after leaving `comm` until explicitly stopped or rebooted. Browser commands can start the live worker, but its repeated output goes to USB/UART; the browser uses one-shot `comm discover` for results.

Each board row labels distance as unavailable, since ESP-NOW RSSI is not a distance sensor. The displayed 250 Kbps is the configured raw transmit rate, not measured useful throughput. Signal strength is the last reply's RSSI; replies out of ten indicate the short sample's round-trip reliability. No fabricated distance or throughput estimate is produced.

Discovery broadcasts ten versioned probes on the current channel over about five seconds. Other boards automatically answer with random reply delays to reduce collisions. Only replies matching this exact firmware image's ELF SHA256 and the current probe's random identifier are counted; duplicate replies count once per board per probe. The output lists each board's MAC, last received reply RSSI (dBm), and replies out of ten (percentage). The local board is excluded. The receive callback queues discovery separately from text traffic so commands waiting for discovery cannot block replies. A separate transmit mutex serializes console sends and automatic replies.

This is a sample of round-trip success, not a measurement of one-way packet loss, cryptographic identity, or guaranteed future reliability. Results include at most 32 responding boards in direct range; this does not discover remote mesh nodes, scan Wi-Fi routers, or switch channels. Boards with older firmware cannot answer; all boards must be flashed with the same new image. Discovery control frames are not printed as chat messages or added to message history. A zero-result scan completes normally with troubleshooting guidance.

The `mess_comm` component uses ESP-NOW on the shared `mess_wifi` station interface. It needs no router, SSID, or password. The shared radio also creates the laptop hotspot. This is direct one-hop communication, not ESP-WIFI-MESH. The STA interface enables Espressif LR; all outgoing ESP-NOW peers use 250 Kbps LR. The laptop AP keeps normal Wi-Fi compatibility. Both wireless components can remain active; a separate Wi-Fi owner requires integration work.

Flash the same firmware to both boards. On both:

```text
comm
status
channel 1
```

On board A, send to the MAC printed by board B's `status`:

```text
send aa:bb:cc:dd:ee:02 "hello from A"
```

On board B:

```text
receive
```

For a broadcast from either board:

```text
broadcast "hello nearby MESS boards"
```

Every board starts listening at boot on channel 1; only the sender needs to enter `comm`. Incoming messages print automatically on USB/UART as `[ESP-NOW RX] sender (bytes, RSSI): message`, even at `mess>` or in another console. All boards must use the same channel (console supports 1..11). `receive` optionally replays the last eight messages and works in the browser command terminal too. The USB prompt may be visually interrupted by a received message; input is retained. Press Enter to obtain a fresh prompt. Browser terminals do not automatically display incoming ESP-NOW messages; use `comm receive` there.

The hotspot uses the same channel as ESP-NOW. Selecting the channel already in use succeeds without changing the radio, including while laptops are connected. Disconnect laptops before selecting a different channel; actual changes are rejected while a laptop is connected. Set matching channels on both boards before joining a laptop to the hotspot. A second board is needed to verify ESP-NOW delivery.

Limits: 200-byte text payloads; 8 packets awaiting the UART worker; new arrivals drop if that queue fills. The worker separately retains the latest 8 displayed messages for receive; older history is discarded. Unicast peers are added as used; ESP-NOW's finite peer table is not evicted automatically. Reboot clears it. Send waits up to two seconds for radio completion. Radio completion, especially broadcast, is not proof of application receipt; check the remote inbox. No application acknowledgements, automatic discovery, retries, encryption, persistence, or multihop forwarding are implemented. Do not send credentials through this console.

Commands accept decimal values and quoted messages; MAC addresses use six colon-separated hexadecimal octets. Incoming nonprintable bytes are escaped for display.

The startup path does not erase NVS on errors. If startup reports an NVS problem, resolve it deliberately rather than automatically wiping saved settings.

## Broadcast typed lines to laptops

At `mess>` enter `wifi`. The `wifi>` console sends each ordinary line as an unquoted message; spaces and quotes are preserved. Press Enter to send:

```text
wifi
hello from the ESP32
this entire line goes to my laptop
status
broadcast exit
exit
```

`help`, `status`, and `exit` are commands. Use `broadcast TEXT` to send reserved words literally. Empty messages are ignored, and messages longer than 480 bytes are rejected. No other terminal modes automatically forward their output.

The hotspot and browser terminal start automatically at boot; entering `wifi` is only needed to send typed lines from USB. On your laptop, join the hotspot printed by `status` (`MESS-xxxxxx`, demo password `mess-demo-123`) and open `http://192.168.4.1`. The page polls every half-second and independently displays new messages for each connected browser. It displays message content as text, including markup and quotes. Up to four devices can join. This hotspot provides no Internet access.

Actual UDP broadcasts go to `192.168.4.255:3333` and can be consumed by an external UDP receiver. The browser terminal is included and needs no receiver script. UDP broadcasts are not acknowledged or retried. A successful send is not evidence of laptop receipt.

The browser history holds 16 messages in MCU RAM and survives mode exit, but resets on reboot. A disconnected/slow browser can miss messages beyond this history. The viewer remains available after `exit`; re-enter `wifi` to type more messages. Laptop Wi-Fi and ESP-NOW remain active together on the shared channel.

## Hardware verification

1. Flash the firmware and open monitor. Confirm `mess>` lists all three modes.
2. Run `i2ctools`, `scan`, and a known sensor register read; then `exit`.
3. With one board, enter `wifi`, join the hotspot, and open the browser. Send lines containing spaces, quotes, `<tags>`, and `broadcast exit`; verify their exact text. Optionally run the UDP receiver and confirm the same messages arrive.
4. Exit and re-enter `wifi`; confirm the history and hotspot stay available. Reboot and confirm the history clears.
5. With two boards, enter `comm` on both, set the same channel with laptops disconnected, and send unicast and broadcast messages. Verify them with `receive` on the other board.
6. Connect a laptop, switch between `wifi`, `comm`, and `i2ctools`, and confirm both wireless receivers still work. Confirm changing channels while connected is rejected.

## Browser terminal

After opening the hotspot webpage, the terminal accepts the same operations as the USB console. It has an independent mode and prompt for each open page:

```text
mess> i2ctools
i2ctools> scan
i2ctools> write-read 0x76 1 0xD0
i2ctools> exit
mess> comm
comm> status
comm> send aa:bb:cc:dd:ee:02 "hello from browser"
comm> receive
comm> exit
mess> wifi
wifi> status
wifi> hello from the browser
wifi> exit
```

Full commands such as `i2ctools scan` and `comm status` also work without entering a mode. Within browser `wifi` mode, ordinary lines become broadcasts, just like the USB console. `help`, `status`, and `exit` retain their command meanings.

Press Enter or Send to submit. The input stays disabled until the command returns. A scan may take several seconds. Returned text includes command errors and result bytes. Browser commands do not read from the UART or change its prompt. Concurrent browser/USB I2C and ESP-NOW commands run one at a time. ESP-NOW `receive` consumes shared message history; another terminal cannot replay the same history again, but automatic USB printing is independent of this history.

No terminal session history is saved on the MCU. Output is briefly captured for the response and then released; each browser keeps only its visible display (up to about 64 KB). Reload or Clear display removes that display. System/Wi-Fi driver logs remain on UART. The separate MCU broadcasts panel retains its existing 16-message RAM history.

Commands use `POST /command` with a plain-text body of 1..511 bytes including the mode prefix, and return plain-text output with `X-MESS-Result` (0 for success). One request executes one command. Requests containing embedded NUL/newline characters or a different browser Origin are rejected. Failed network requests are never automatically retried because the command may already have executed.

Hardware check: enter `i2ctools` in the browser, scan and read a known sensor, and compare with the USB terminal. Open a second browser page and confirm each page keeps its own mode. Confirm `comm status` works, and `receive` returns queued messages. Check quotes and spaces in a browser broadcast, Clear display, and reload. The favicon request now receives an empty success response.
