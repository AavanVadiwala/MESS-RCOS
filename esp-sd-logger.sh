"""Display live MESS readings from a local serial port. For testing and referece to the Software/Visulization Team"""
import argparse
from datetime import datetime
import json
import sys
import time

PREFIX = b"MESS_READING "


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Serial device, e.g. COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    role = parser.add_mutually_exclusive_group()
    role.add_argument("--collector", dest="role", action="store_const", const="collector",
                      help="Set the ESP as collector at boot, then stream readings")
    role.add_argument("--node", dest="role", action="store_const", const="node",
                      help="Set the ESP as a mesh node at boot and exit after confirmation")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--raw", action="store_true", help="Show all serial output, including ESP logs")
    output.add_argument("--json", action="store_true", help="Print newline-delimited JSON")
    args = parser.parse_args()
    try:
        import serial
    except ImportError:
        parser.exit(1, "Install pyserial with: python -m pip install pyserial\n")

    try:
        with serial.Serial(args.port, args.baud) as connection:
            if args.role:
                print("Press EN/RESET on the ESP now. Waiting up to 30 seconds for boot.",
                      file=sys.stderr)
                connection.timeout = 0.2
                deadline = time.monotonic() + 30
                command_sent = False
                command = f"{args.role}\n".encode("ascii")
                confirmation = f"MESS mesh role: {args.role}".encode("ascii")
                while time.monotonic() < deadline:
                    line = connection.readline()
                    if b"Send collector or node followed by Enter" in line:
                        connection.write(command)
                        command_sent = True
                    if command_sent and confirmation in line:
                        print(f"{args.role.capitalize()} role confirmed.", file=sys.stderr)
                        break
                else:
                    print(f"No {args.role} confirmation. Check the port and firmware, then retry.",
                          file=sys.stderr)
                    return 1
                connection.timeout = None
                if args.role == "node" and not args.raw:
                    return 0
            if not args.json:
                print(f"Listening on {args.port} at {args.baud} baud. Press Ctrl+C to stop.",
                      file=sys.stderr)
            for line in connection:
                if args.raw:
                    print(line.decode(errors="replace").rstrip(), flush=True)
                    continue
                if not line.startswith(PREFIX):
                    continue
                try:
                    reading = json.loads(line[len(PREFIX):])
                except (ValueError, UnicodeError):
                    continue
                if not isinstance(reading, dict):
                    continue
                if args.json:
                    print(json.dumps(reading), flush=True)
                    continue
                received = datetime.now().strftime("%H:%M:%S")
                print(f"{received}  {reading.get('mac', '?')}  "
                      f"sensor={reading.get('sensor', '?')}  "
                      f"{reading.get('reading', '')}", flush=True)
    except KeyboardInterrupt:
        return 0
    except (serial.SerialException, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
