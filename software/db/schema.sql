-- One table per physical sensor; columns match the keys in dummy_data.json
-- Units: temperature C, humidity %RH, pressure hPa, eco2 ppm, tvoc ppb, lux lx

CREATE TABLE IF NOT EXISTS bme280_data (
    id INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    recorded_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    temperature REAL NOT NULL,
    humidity REAL NOT NULL,
    pressure REAL NOT NULL,
    sensor_id INTEGER NOT NULL,
    loc TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS sgp30_data (
    id INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    recorded_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    eco2 INTEGER NOT NULL,
    tvoc INTEGER NOT NULL,
    sensor_id INTEGER NOT NULL,
    loc TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS bh1750_data (
    id INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    recorded_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    lux REAL NOT NULL,
    sensor_id INTEGER NOT NULL,
    loc TEXT NOT NULL
);