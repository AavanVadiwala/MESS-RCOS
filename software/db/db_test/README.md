# Database Input Scripts

These scripts insert test readings into the PostgreSQL database and print the
latest rows back, so you can check that the schema and connection work without
real hardware.

## Requirements

- Python 3.10+ (the scripts use `match` statements)
- Python packages: `psycopg` and `python-dotenv`

  ```powershell
  pip install "psycopg[binary]" python-dotenv
  ```

- PostgreSQL running on `localhost:5432`
- Database settings in `software/web/.env` (copy `software/web/.env.example`
  if you don't have one). The scripts read `POSTGRES_DB`, `POSTGRES_USER`,
  `POSTGRES_PASSWORD` and, optionally, `DB_HOST`.

Start the database from `software` if it is not already running:

```powershell
docker compose up -d db
```

> `db/schema.sql` only runs when the database volume is first created. If you
> still have the old tables from an earlier schema, reset the volume with
> `docker compose down -v` and start the database again. This deletes all
> stored data.

## Tables

Each physical sensor has its own table (see [`../schema.sql`](../schema.sql)).
Every row also stores `recorded_at`, `sensor_id` and `loc`.

| Sensor | Table | Value columns |
| --- | --- | --- |
| `bme280` | `bme280_data` | `temperature` (°C), `humidity` (%RH), `pressure` (hPa) |
| `sgp30` | `sgp30_data` | `eco2` (ppm), `tvoc` (ppb), both integers |
| `bh1750` | `bh1750_data` | `lux` (lx) |

Both scripts use sensor ID `9` and location `Room 101` for every row. To change
them, edit `temp_sensor_id` and `temp_loc` at the top of each script.

## `manual_input.py`

Use this script to enter one reading at a time.

```powershell
python manual_input.py
```

From the main menu:

- **`live`**: enter a sensor type (`bme280`, `sgp30` or `bh1750`), then its
  values separated by spaces, in this order:

  | Sensor | Example input |
  | --- | --- |
  | `bme280` | `22.5 45 1013.2` (temperature, humidity, pressure) |
  | `sgp30` | `450 20` (eco2, tvoc) |
  | `bh1750` | `320` (lux) |

  After each reading, press Enter to add another, or type `back` to return to
  the main menu.
- **`output`**: enter a sensor type to show its 10 most recent readings.
- **`exit`**: quit. Ctrl+C also stops the script.

## `random_input.py`

Use this script to fill the database with test data automatically.

```powershell
python random_input.py
```

For each sensor it inserts 10 rows (`NUM_READINGS`) of random values within
realistic ranges. The rows are timestamped 5 seconds apart (`INTERVAL_S`),
with the newest row at the current time. It then prints the 10 most recent
readings for each sensor.

| Sensor | Value ranges |
| --- | --- |
| `bme280` | temperature 20–25, humidity 40–50, pressure 1010–1015 |
| `sgp30` | eco2 400–600, tvoc 0–60 |
| `bh1750` | lux 300–350 |

To change the ranges, edit the `SENSORS` dictionary. Its column names must
match `db/schema.sql`.
