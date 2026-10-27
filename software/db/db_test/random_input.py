import os
import psycopg
import random
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), "../../web/.env"))

temp_sensor_id = 1
temp_loc = "Room 101"

L_reading = [random.uniform(10, 100) for _ in range(10)]  # Generate 10 random light readings
G_reading = [random.uniform(10, 100) for _ in range(10)]  # Generate 10 random gas readings
T_reading = [random.uniform(10, 100) for _ in range(10)]  # Generate 10 random temperature readings
H_reading = [random.uniform(10, 100) for _ in range(10)]  # Generate 10 random humidity readings

def add_list_readings_to_db(cur, data_type, data_list, sensor_id, loc):
    for x in data_list:
        cur.execute(
            f"INSERT INTO {data_type}_data ({data_type}_value, sensor_id, loc) VALUES (%s, %s, %s)",
            (x, sensor_id, loc),)

def output_readings(cur, data_type):
    cur.execute(f"SELECT recorded_at, {data_type}_value, sensor_id, loc FROM {data_type}_data ORDER BY recorded_at DESC LIMIT 10")
    rows = cur.fetchall()
    print(f"\nLast 10 readings for {data_type}:")
    print(f"{'Recorded at':19} {'Value':>10}")
    print("-" * 31)
    for row in rows:
        timestamp = row[0].strftime("%Y-%m-%d %H:%M:%S")
        print(f"Senor {row[2]}, Location: {row[3]}: {timestamp:19} {row[1]:10.2f}")

def main():
    with psycopg.connect(
        host=os.getenv("DB_HOST", "localhost"),
        port=5432,
        dbname=os.environ["POSTGRES_DB"],
        user=os.environ["POSTGRES_USER"],
        password=os.environ["POSTGRES_PASSWORD"],
    ) as conn:
        with conn.cursor() as cur:
            add_list_readings_to_db(cur, "light", L_reading, temp_sensor_id, temp_loc)
            add_list_readings_to_db(cur, "gas", G_reading, temp_sensor_id, temp_loc)
            add_list_readings_to_db(cur, "temp", T_reading, temp_sensor_id, temp_loc)
            add_list_readings_to_db(cur, "humid", H_reading, temp_sensor_id, temp_loc)
            conn.commit()
            output_readings(cur, "light")
            output_readings(cur, "gas")
            output_readings(cur, "temp")
            output_readings(cur, "humid")


if __name__ == "__main__":
    main()