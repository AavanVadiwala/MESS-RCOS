# MESS MCU scanner and one-shot I2C polling

ESP-IDF v6.1 firmware for the original ESP32. Nearby boards announce themselves
using ESP-NOW Long Range at a configured 250 Kbps on channel 1. The monitor lists
their MAC addresses and latest RSSI every three seconds. Boards expire after six
seconds without an observation; up to 32 remote boards are tracked.

## One-shot poll

At the USB/UART `mess>` prompt:

```text
poll i2cdetect
poll i2cget -c 0x76 -r 0xD0 -l 1
poll i2cconfig --sda 21 --scl 22 --freq 100000
```

Each invocation executes the I2C command once locally, broadcasts one request,
and waits 12 seconds for remote results. It prints one grouped report:

```text
MCU 1:
MCU MAC: aa:bb:cc:dd:ee:01
RSSI: N/A (local MCU)
TX rate: 250 Kbps (configured)
Announcement PDR: N/A (local MCU)
I2C output:
...address table...

MCU 2:
MCU MAC: aa:bb:cc:dd:ee:02
RSSI: -55 dBm
TX rate: 250 Kbps (configured)
Announcement PDR: 94% (30/32 announcements)
I2C output:
...address table...
```

Remote RSSI comes from the received reply; timed-out boards show their latest
discovery RSSI. Numbering is report order, not a persistent device identity: use
the MAC address. Background list printing pauses on the requesting MCU during
the poll, then resumes. Polling never repeats automatically. No requests or
result fragments are automatically retried. A timeout or incomplete reply does
not establish that the command failed to execute. Duplicate requests are filtered
by sender and request ID using an eight-entry, 60-second RAM cache; reboot clears it.

## I2C component

`firmware/components/i2ctools/cmd_i2ctools.c` is adapted from Espressif's local
`C:/test_mc/esp-idf/examples/peripherals/i2c/i2c_tools/main/cmd_i2ctools.c`
(Unlicense OR CC0-1.0). It preserves the example command names and arguments:
`i2cconfig`, `i2cdetect`, `i2cget`, `i2cset`, and `i2cdump`. Type these directly
to run locally; prefix them with `poll` to run across matching boards. `help`
lists usage. Default bus configuration on every board is SDA21/SCL22, 100 kHz.
Adaptations add an explicit output stream and mutex, argument bounds, proper
transfer-error propagation for get/set, buffer/handle cleanup, and raw reads
when `i2cget` has no register argument. `i2cdetect` retains the example's 0..127
scan table and `UU` marker for timeouts; an ACK does not identify a sensor.
`i2cdump` retains per-register `XX` failure markers.

Poll command text is limited to 159 bytes; each MCU's result is capped at 2048
bytes, with a truncation marker. Results are split into numbered frames of at
most 250 bytes and assembled by request ID and sender; incomplete results are
reported rather than printed as a complete table. Only matching exact ELF hashes
are accepted. Discovery and polling are direct one-hop communication, not mesh.
There is no hotspot, browser server, router connection, or internet upload.
Remote operations are unencrypted and unauthenticated; firmware hashes are a
compatibility filter, not authentication. `poll i2cset` writes to every responding
board, and `poll i2cconfig` changes their bus configuration until reboot.

## Build, flash, and verify

```powershell
.\build_and_flash.ps1 -Build
.\build_and_flash.ps1 -Port COM10 -Monitor
```

Build once and flash the same image onto every board (replace COM10). Exit the
monitor with Ctrl+]. To monitor an already flashed board without flashing again:

```powershell
.\build_and_flash.ps1 -Port COM10 -MonitorOnly
```

The script invokes ESP-IDF through its selected Python environment explicitly,
avoiding Windows `.py` file associations and Conda's Python selection. Output
remains `hello_world.bin`.

With two boards, run `poll i2cdetect` and verify one local section and one remote
section against direct `i2cdetect` results. Attach different known I2C devices
to verify outputs stay associated with the correct MAC. Run a known register
read with `poll i2cget`. Check an uninitialized/busy bus error, powered-off board
timeout, malformed arguments, and incomplete replies under packet loss. Different
firmware images must not respond. Confirm no I2C scan repeats after poll completes.
Physical I2C and radio reception require hardware validation.

## Packet delivery rate

Both the automatic list and poll report show announcement PDR alongside RSSI.
Each sender numbers announcements accepted by the radio driver. Receivers count
unique arrivals over the sequence span from their first received announcement,
capped at the latest 32 sequence numbers. Gaps count as missing; duplicates do
not inflate the count, and late packets can fill gaps still in the window. A
random per-boot sender session resets the sample after reboot. Each receiver
measures its own incoming link: this is not round-trip delivery, an I2C result
delivery rate, or a throughput measurement. Loss before the first arrival or
after the last received sequence cannot be inferred until another packet arrives.
Counts are shown so a one-packet 100% sample is not mistaken for a long test.
At 1-2 seconds per announcement, a full window spans roughly 32-64 seconds.
The local board has no received self-link and displays N/A. Powered-off boards
expire from the list rather than reporting an invented 0% delivery rate.

## Coordinated transmission rate command

Run `rate 500` or `rate 250` at `mess>` to set the local broadcast peer's physical
TX rate and request that all reachable matching MCUs do the same. Units are Kbps,
not kHz. Receivers retain support for both LR rates, so mixed TX rates remain
receivable. Rate requests and acknowledgements use the same queue/worker as polls;
the sender waits 12 seconds and shows confirmed, failed, or unconfirmed boards.
There is no atomic network-wide change: an unreachable MCU keeps its old setting,
and a lost acknowledgement leaves its state uncertain. No automatic retries are
made. Future `rate` commands are safe to repeat intentionally to converge settings.
Each announcement advertises the sender's actual configured TX rate; automatic
lists and poll reports show it beside RSSI and announcement PDR. After a switch,
the rolling PDR window initially includes packets sent at the previous rate until
those leave the window. Rate settings reset to 250 Kbps on reboot. Flash the same
new image onto all MCUs before using these commands.

Hardware validation: use `rate 500`, verify confirmations and subsequent 500 Kbps
announcements/poll sections on both boards, then switch back with `rate 250`.
Check a powered-off or busy MCU produces an unconfirmed result rather than a
false success, and verify a reboot returns the board to 250 Kbps.
