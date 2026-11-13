import os
from unittest import case
import psycopg
from dotenv import load_dotenv

load_dotenv(os.path.join(os.path.dirname(__file__), "../../web/.env"))

temp_sensor_id = 9
temp_loc = "Room 101"

def add_readings_to_db(cur, sensor_type, value, id, loc):
        match sensor_type:
            case "bme280":
                cur.execute(
                    "INSERT INTO bme280_data (temperature, humidity, pressure, sensor_id, loc) "
                    "VALUES (%s, %s, %s, %s, %s)",
                    (value[0], value[1], value[2], id, loc),
                )
            case "sgp30":
                cur.execute(
                    f"INSERT INTO sgp30_data (eco2, tvoc, sensor_id, loc) VALUES (%s, %s, %s, %s)",
                    (value[0], value[1], id, loc),
                )
            case "bh1750":
                cur.execute(
                    f"INSERT INTO bh1750_data (lux, sensor_id, loc) VALUES (%s, %s, %s)",
                    (value[0], id, loc),)
            case _:
                print("Invalid sensor type.")
                return

def live_readings(cur):
    sensor_type = input("Enter the sensor type (bme280, sgp30, bh1750): ").strip().lower()
    if sensor_type not in ["bme280", "sgp30", "bh1750"]:
        print("Invalid sensor type. Please enter 'bme280', 'sgp30', or 'bh1750'.")
        return
    try:
        match sensor_type:
                case "bme280":
                    data_type = (input("Enter the data value (temperature, humidity, pressure): "))
                    data_value = [float(x) for x in data_type.strip().split()]
                case "sgp30":
                    data_type = (input("Enter the data value (eco2, tvoc): "))
                    data_value = [float(x) for x in data_type.strip().split()]
                case "bh1750":
                    data_type = (input("Enter the data value (lux): ")) 
                    data_value = [float(x) for x in data_type.strip().split()]
                case _:
                    print("Invalid sensor type.")
                    return
    except ValueError:
        print("Invalid data value. Please enter a valid number.")
        return

    add_readings_to_db(cur, sensor_type, data_value, temp_sensor_id, temp_loc)

def output_readings(cur, sensor_type):
    match sensor_type:
        case "bme280":
            cur.execute(f"SELECT recorded_at, temperature, humidity, pressure, sensor_id, loc FROM bme280_data ORDER BY recorded_at DESC LIMIT 10")
        case "sgp30":
            cur.execute(f"SELECT recorded_at, eco2, tvoc, sensor_id, loc FROM sgp30_data ORDER BY recorded_at DESC LIMIT 10")
        case "bh1750":
            cur.execute(f"SELECT recorded_at, lux, sensor_id, loc FROM bh1750_data ORDER BY recorded_at DESC LIMIT 10")
        case _:
            print("Invalid sensor type.")
            return

    rows = cur.fetchall()
    print(f"\nLast 10 readings for {sensor_type}:")
    print(f"{'Recorded at':19} {'Value':>10}")
    print("-" * 31)
    for row in rows:
        timestamp = row[0].strftime("%Y-%m-%d %H:%M:%S")
        print(f"Sensor {row[2]}, Location: {row[3]}: {timestamp:19} {row[1]:10.2f}")


def main():
    with psycopg.connect(
        host=os.getenv("DB_HOST", "localhost"),
        port=5432,
        dbname=os.environ["POSTGRES_DB"],
        user=os.environ["POSTGRES_USER"],
        password=os.environ["POSTGRES_PASSWORD"],
    ) as conn:
        with conn.cursor() as cur:
            try:
                while True:
                    choice = input("Enter 'live' to input live readings, 'output' to view last 10 readings, or 'exit' to quit: ").strip().lower()
                    if choice == 'live':
                        while True:
                            live_readings(cur)
                            if input("Enter 'back' to return to the main menu: ").strip().lower() == 'back':
                                break
                            conn.commit()
                    elif choice == 'output':
                        sensor_type = input("Enter the sensor type (bme280, sgp30, bh1750): ").strip().lower()
                        if sensor_type not in ["bme280", "sgp30", "bh1750"]:
                            print("Invalid sensor type. Please enter 'bme280', 'sgp30', 'bh1750'.")
                            continue
                        output_readings(cur, sensor_type)
                    elif choice == 'exit':
                        break
            except (KeyboardInterrupt, EOFError): # Handle Ctrl+C or Ctrl+D to stop the loop
                print("\nStopping the reading process.")

if __name__ == "__main__":
    main()