"""Display live MESS readings from a local serial port and append them to a CSV."""
import argparse
import csv
from datetime import datetime
import json
import sys
import time

PREFIX = b"MESS_READING "


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Serial device, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--csv", default="esp-readings.csv", metavar="PATH",
                        help="Append readings to PATH (default: esp-readings.csv)")
    parser.add_argument("--collector", action="store_true",
                        help="Set the ESP as collector at boot, then stream readings")
    parser.add_argument("--reset-esp", action="store_true",
                        help="Reset the ESP through the USB serial adapter's RTS line")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--raw", action="store_true", help="Show all serial output, including ESP logs")
    output.add_argument("--json", action="store_true", help="Print newline-delimited JSON")
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        parser.exit(1, "Install pyserial with: python -m pip install pyserial\n")

    try:
        connection = serial.Serial(baudrate=args.baud)
        connection.port = args.port
        if args.reset_esp:
            connection.dtr = False
            connection.rts = False
        with connection:
            if args.reset_esp:
                connection.rts = True
                time.sleep(0.1)
                connection.reset_input_buffer()
                connection.rts = False
            if args.collector:
                if not args.reset_esp:
                    print("Press EN/RESET on the ESP now. Waiting up to 30 seconds for boot.",
                          file=sys.stderr)
                connection.timeout = 0.2
                deadline = time.monotonic() + 30
                command_sent = False
                while time.monotonic() < deadline:
                    line = connection.readline()
                    if b"Send collector or node followed by Enter" in line:
                        connection.write(b"collector\n")
                        command_sent = True
                    if command_sent and b"MESS mesh role: collector" in line:
                        print("Collector role confirmed.", file=sys.stderr)
                        break
                else:
                    print("No collector confirmation. Check the port and firmware, then retry.",
                          file=sys.stderr)
                    return 1
                connection.timeout = None
            if not args.json:
                print(f"Listening on {args.port} at {args.baud} baud. Press Ctrl+C to stop.",
                      file=sys.stderr)
            if args.raw:
                for line in connection:
                    print(line.decode(errors="replace").rstrip(), flush=True)
                return 0
            with open(args.csv, "a", encoding="utf-8", newline="") as csv_file:
                writer = csv.writer(csv_file)
                if csv_file.tell() == 0:
                    writer.writerow(("received", "mac", "sensor", "reading"))
                    csv_file.flush()
                for line in connection:
                    if not line.startswith(PREFIX):
                        continue
                    try:
                        reading = json.loads(line[len(PREFIX):])
                    except ValueError:
                        continue
                    if not isinstance(reading, dict):
                        continue
                    received = datetime.now().strftime("%H:%M:%S")
                    mac = reading.get("mac", "?")
                    sensor = reading.get("sensor", "?")
                    value = reading.get("reading", "")
                    writer.writerow((received, str(mac), str(sensor), str(value)))
                    csv_file.flush()
                    if args.json:
                        print(json.dumps(reading), flush=True)
                    else:
                        print(f"{received}  {mac}  sensor={sensor}  {value}", flush=True)
    except KeyboardInterrupt:
        return 0
    except (serial.SerialException, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
