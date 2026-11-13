# how to use

docker compose up --build will work but for deployment
Ive wrapped it in start.js the only difference is it prints a qr 
code before running docker compose up that can be scanned

thats pretty much it, if you want to seed db with fake data
start it and then run any of the python files in db/db_test/







# How to send data

`POST http://<server>:3000/data` with header `Content-Type: application/json`


Multiple readings can arrive in one request but data from different sensors must be made in seperate requests.

You can NOT have any blank fields, all fields for a sensor must be populated,

## bme280

```json
{
  "sensor": "bme280",
  "sensor_id": 1,
  "loc": "Room 101",
  "readings": [
    { "time": "2026-11-13T12:00:00Z", "temperature": 22.6, "humidity": 44.9, "pressure": 1012.9 }
  ]
}
```

## sgp30

```json
{
  "sensor": "sgp30",
  "sensor_id": 1,
  "loc": "Room 101",
  "readings": [
    { "time": "2026-11-13T12:00:00Z", "eco2": 412, "tvoc": 15 }
  ]
}
```

## bh1750

```json
{
  "sensor": "bh1750",
  "sensor_id": 1,
  "loc": "Room 101",
  "readings": [
    { "time": "2026-11-13T12:00:00Z", "lux": 320 }
  ]
}
```
