import os
import psycopg
import random
from datetime import datetime, timedelta, timezone
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), "../../web/.env"))

temp_sensor_id = 9
temp_loc = "Room 101"
NUM_READINGS = 10   # rows per sensor
INTERVAL_S = 5      # seconds between fake readings

# sensor -> {column: (min, max)} realistic ranges; columns must match db/schema.sql
SENSORS = {
    "bme280": {"temperature": (20, 25), "humidity": (40, 50), "pressure": (1010, 1015)},
    "sgp30": {"eco2": (400, 600), "tvoc": (0, 60)},
    "bh1750": {"lux": (300, 350)},
}
INT_COLUMNS = {"eco2", "tvoc"}   # INTEGER in the schema

def random_row(sensor):
    return [random.randint(lo, hi) if col in INT_COLUMNS else round(random.uniform(lo, hi), 2)
            for col, (lo, hi) in SENSORS[sensor].items()]

def add_list_readings_to_db(cur, sensor, count, sensor_id, loc):
    # every row in one transaction would get the same CURRENT_TIMESTAMP,
    # so set recorded_at explicitly: oldest first, INTERVAL_S apart, ending now
    cols = list(SENSORS[sensor])
    placeholders = ", ".join(["%s"] * (len(cols) + 3))
    now = datetime.now(timezone.utc)
    for i in range(count):
        recorded_at = now - timedelta(seconds=INTERVAL_S * (count - 1 - i))
        cur.execute(
            f"INSERT INTO {sensor}_data (recorded_at, {', '.join(cols)}, sensor_id, loc) VALUES ({placeholders})",
            (recorded_at, *random_row(sensor), sensor_id, loc),)

def output_readings(cur, sensor):
    cols = list(SENSORS[sensor])
    cur.execute(f"SELECT recorded_at, sensor_id, loc, {', '.join(cols)} FROM {sensor}_data ORDER BY recorded_at DESC LIMIT 10")
    rows = cur.fetchall()
    print(f"\nLast 10 readings for {sensor}:")
    print(f"{'Recorded at':19} " + " ".join(f"{c:>12}" for c in cols))
    print("-" * (20 + 13 * len(cols)))
    for row in rows:
        timestamp = row[0].strftime("%Y-%m-%d %H:%M:%S")
        values = " ".join(f"{v:12.2f}" for v in row[3:])
        print(f"{timestamp:19} {values}   (Sensor {row[1]}, Location: {row[2]})")

def main():
    with psycopg.connect(
        host="127.0.0.1",
        port=5432,
        dbname=os.environ["POSTGRES_DB"],
        user=os.environ["POSTGRES_USER"],
        password=os.environ["POSTGRES_PASSWORD"],
        connect_timeout=5,
    ) as conn:
        with conn.cursor() as cur:
            for sensor in SENSORS:
                add_list_readings_to_db(cur, sensor, NUM_READINGS, temp_sensor_id, temp_loc)
            conn.commit()
            for sensor in SENSORS:
                output_readings(cur, sensor)


if __name__ == "__main__":
    main()