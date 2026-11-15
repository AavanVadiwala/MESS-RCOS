"""Configure MESS boards over USB and forward collector readings to the web API."""

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import sqlite3
import sys
from threading import Event, Thread
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from serial import Serial, SerialException
from serial.tools import list_ports


PROMPT = b"Send collector or node followed by Enter"
PREFIX = b"MESS_READING "
SENSORS = {
    0: ("bme280", {"C": "temperature", "%RH": "humidity", "Pa": "pressure"}),
    1: ("sgp30", {"ppm_CO2eq": "eco2", "ppb_TVOC": "tvoc"}),
    2: ("bh1750", {"lux": "lux"}),
}


def wait_for(connection, matches, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if matches(connection.readline()):
            return True
    return False


def reset(connection):
    connection.reset_input_buffer()
    connection.rts = True
    time.sleep(0.12)
    connection.rts = False


def open_port(port):
    connection = Serial(baudrate=115200, timeout=0.2, write_timeout=2)
    connection.port = port
    connection.dtr = False
    connection.rts = False
    connection.open()
    return connection


def detect_boards(manual_reset):
    boards = []
    for port in sorted(list_ports.comports()):
        print(f"Checking {port.device} ({port.description})", flush=True)
        try:
            with open_port(port.device) as connection:
                reset(connection)
                detected = wait_for(connection, lambda line: PROMPT in line, 2)
                if not detected and manual_reset:
                    print(f"Press EN/RESET on {port.device} within 10 seconds, if it is an ESP32.")
                    detected = wait_for(connection, lambda line: PROMPT in line, 10)
                if detected:
                    boards.append(port.device)
                else:
                    print(f"  No MESS role prompt on {port.device}.")
        except (SerialException, OSError) as error:
            print(f"  Skipping {port.device}: {error}", file=sys.stderr)
    return boards


def choose_collector(boards):
    for index, port in enumerate(boards, start=1):
        print(f"  {index}. {port}")
    while True:
        choice = input("Select collector number: ").strip()
        if choice.isdecimal() and 1 <= int(choice) <= len(boards):
            return boards[int(choice) - 1]
        print("Enter a number from the list.")


def configure_role(connection, role):
    reset(connection)
    if not wait_for(connection, lambda line: PROMPT in line, 2):
        print(f"Press EN/RESET on {connection.port} within 15 seconds.")
        if not wait_for(connection, lambda line: PROMPT in line, 15):
            raise RuntimeError(f"{connection.port}: no MESS role prompt")
    connection.write(f"{role}\n".encode("ascii"))
    expected = f"MESS mesh role: {role}".encode("ascii")
    if not wait_for(connection, lambda line: line.strip() == expected, 6):
        raise RuntimeError(f"{connection.port}: {role} role was not confirmed")
    print(f"{connection.port}: {role} role confirmed.")


def api_payload(record, received, sensor_id):
    """Convert a successful firmware record into one POST /data payload."""
    sensor = record.get("sensor")
    if type(sensor) is not int or sensor not in SENSORS:
        return None
    name, fields = SENSORS[sensor]
    values = dict(part.split("=", 1) for part in record.get("reading", "").split() if "=" in part)
    if values.get("sensor") != name or values.get("status") != "ESP_OK":
        return None
    try:
        reading = {column: float(values[key]) for key, column in fields.items()}
        if not all(math.isfinite(value) for value in reading.values()):
            return None
        if name == "bme280":
            reading["pressure"] /= 100
        elif name == "sgp30":
            for key in ("eco2", "tvoc"):
                if not reading[key].is_integer():
                    return None
                reading[key] = int(reading[key])
    except (KeyError, ValueError):
        return None
    reading["time"] = received
    return {
        "sensor": name, "sensor_id": sensor_id, "loc": record["mac"],
        "readings": [reading],
    }


def save_record(database, record):
    received = datetime.now(timezone.utc).isoformat()
    mac = record["mac"]
    with database:
        database.execute("INSERT OR IGNORE INTO boards(mac) VALUES (?)", (mac,))
        sensor_id = database.execute("SELECT id FROM boards WHERE mac = ?", (mac,)).fetchone()[0]
        payload = api_payload(record, received, sensor_id)
        database.execute(
            "INSERT INTO records(received, raw, payload) VALUES (?, ?, ?)",
            (received, json.dumps(record), json.dumps(payload) if payload else None),
        )


def upload_pending(path, endpoint, stop):
    database = sqlite3.connect(path)
    try:
        while not stop.is_set():
            rows = database.execute(
                "SELECT id, payload FROM records WHERE payload IS NOT NULL AND delivery IS NULL "
                "ORDER BY id LIMIT 100"
            ).fetchall()
            if not rows:
                stop.wait(0.5)
                continue
            for row_id, payload in rows:
                if stop.is_set():
                    return
                request = Request(endpoint, data=payload.encode(), headers={"Content-Type": "application/json"})
                try:
                    with urlopen(request, timeout=3) as response:
                        response.read()
                except HTTPError as error:
                    if error.code in (400, 422):
                        with database:
                            database.execute("UPDATE records SET delivery = ? WHERE id = ?",
                                             (f"rejected: HTTP {error.code}", row_id))
                        print(f"API rejected record {row_id}: HTTP {error.code}", file=sys.stderr)
                        continue
                    print(f"API upload paused: {error}. Readings are saved locally.", file=sys.stderr)
                    stop.wait(5)
                    break
                except (URLError, OSError) as error:
                    print(f"API upload paused: {error}. Readings are saved locally.", file=sys.stderr)
                    stop.wait(5)
                    break
                with database:
                    database.execute("UPDATE records SET delivery = 'sent' WHERE id = ?", (row_id,))
    finally:
        database.close()


def collect(connection, path, endpoint):
    path.parent.mkdir(parents=True, exist_ok=True)
    database = sqlite3.connect(path)
    database.executescript("""
        PRAGMA journal_mode = WAL;
        CREATE TABLE IF NOT EXISTS boards (id INTEGER PRIMARY KEY, mac TEXT NOT NULL UNIQUE);
        CREATE TABLE IF NOT EXISTS records (
            id INTEGER PRIMARY KEY, received TEXT NOT NULL, raw TEXT NOT NULL,
            payload TEXT, delivery TEXT
        );
        CREATE INDEX IF NOT EXISTS pending ON records(id)
            WHERE payload IS NOT NULL AND delivery IS NULL;
    """)
    stop = Event()
    uploader = Thread(target=upload_pending, args=(path, endpoint, stop))
    uploader.start()
    print(f"Reading only {connection.port}. Saving to {path}. Press Ctrl+C to stop.")
    try:
        while True:
            line = connection.readline()
            if not line.startswith(PREFIX):
                continue
            try:
                record = json.loads(line[len(PREFIX):])
            except (ValueError, UnicodeError):
                continue
            if not isinstance(record, dict):
                continue
            mac = record.get("mac")
            if not isinstance(mac, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", mac):
                continue
            if not isinstance(record.get("reading"), str):
                continue
            record["mac"] = mac.lower()
            save_record(database, record)
            print(f"{record['mac']}  {record['reading']}", flush=True)
    finally:
        stop.set()
        uploader.join()
        database.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--collector", metavar="COM_PORT",
                        help="Resume collection from a previously configured collector")
    parser.add_argument("--manual-reset", action="store_true",
                        help="Allow a manual reset during discovery if automatic reset fails")
    parser.add_argument("--endpoint", default="http://127.0.0.1:3000/data")
    parser.add_argument("--database", type=Path,
                        default=Path(__file__).resolve().parents[1] / "data" / "readings.sqlite3")
    args = parser.parse_args()
    try:
        if args.collector:
            with open_port(args.collector) as connection:
                collect(connection, args.database, args.endpoint)
            return 0

        boards = detect_boards(args.manual_reset)
        if not boards:
            raise RuntimeError("No MESS ESP32s detected. Close serial monitors and try --manual-reset.")
        collector = choose_collector(boards)
        for port in boards:
            if port != collector:
                with open_port(port) as connection:
                    configure_role(connection, "node")
        with open_port(collector) as connection:
            configure_role(connection, "collector")
            collect(connection, args.database, args.endpoint)
    except KeyboardInterrupt:
        return 0
    except (OSError, RuntimeError, SerialException, sqlite3.Error) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
