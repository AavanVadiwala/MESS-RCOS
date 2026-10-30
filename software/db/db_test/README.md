# Database Input Scripts

These scripts test inserting data values into the PostgreSQL database. Run them from this directory after starting the database.

## Requirements

- Python 3
- PostgreSQL running on port `5432`
- Python packages: `psycopg` and `python-dotenv`
- Database settings in `software/web/.env`

The scripts use sensor ID `1` and location `Room 101` as default. They expect the
following data types and corresponding database tables:

| Data type | Table | Value column |
| --- | --- | --- |
| `light` | `light_data` | `light_value` |
| `gas` | `gas_data` | `gas_value` |
| `temp` | `temp_data` | `temp_value` |
| `humid` | `humid_data` | `humid_value` |

## `manual_input.py`

Use this script to enter one reading at a time.

1. Select `live` from the main menu.
2. Enter a data type: `light`, `gas`, `temp`, or `humid`.
3. Enter a numeric value. Decimal values are accepted.
4. The reading is inserted into the matching table.

The `output` menu option displays the 10 most recent readings for a selected
data type. Choose `exit` to stop the script.

```powershell
python manual_input.py
```

## `random_input.py`

Use this script to generate test data automatically. It creates a list of 10
random floating-point values between `10` and `100` for each of the four data
types:

- Light
- Gas
- Temperature
- Humidity

All 40 generated readings are inserted into the database. After insertion,
the script prints the 10 most recent readings for each data type in the
terminal.

```powershell
python random_input.py
```

Start the database from `software` if it is not already running:

```powershell
docker compose up -d db
```