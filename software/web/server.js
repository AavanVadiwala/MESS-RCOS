const express = require('express');
<<<<<<< Updated upstream
=======
const path = require('path');
const http = require('http');
const { Server } = require('socket.io');
>>>>>>> Stashed changes
const pg = require('pg');

try {
  process.loadEnvFile();
} catch {
  console.warn('setup an .env file');
}

const pool = new pg.Pool({
  user: process.env.POSTGRES_USER,
  password: process.env.POSTGRES_PASSWORD,
  database: process.env.POSTGRES_DB,
  host: process.env.DB_HOST,
  port: Number(process.env.DB_PORT),
});

const app = express();
const server = http.createServer(app);
const io = new Server(server);
const port = 3000;
app.use(express.json());


app.get('/', (req, res) => {
  res.send('placeholder')
});

app.post('/data', (req, res) => {
  const data = req.body;
  console.log('Received data:', data);
  const done = (err) => {
    if (err) {
      console.error('Error inserting data into database:', err);
      return res.status(500).send('Error inserting data into database');
    }
    io.emit('sensor-update', data);
    res.status(200).send('Data inserted successfully');
  };

  if (data.sensor === "bme280") {
    pool.query(
      "INSERT INTO bme280_data (time, temperature, humidity, pressure) VALUES ($1, $2, $3, $4)",
      [data.time, data.temperature, data.humidity, data.pressure],
      done
    );
  } else if (data.sensor === "sgp30") {
    pool.query(
      "INSERT INTO sgp30_data (time, eco2, tvoc) VALUES ($1, $2, $3)",
      [data.time, data.eco2, data.tvoc],
      done
    );
  } else if (data.sensor === "bh1750") {
    pool.query(
      "INSERT INTO bh1750_data (time, lux) VALUES ($1, $2)",
      [data.time, data.lux],
      done
    );
  } else {
    res.status(400).send('Unknown sensor');
  }
});

server.listen(port, () => {
  console.log(`running on port ${port}`);
})