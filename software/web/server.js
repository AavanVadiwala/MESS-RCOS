const express = require('express');
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
const port = 3000;
app.use(express.json());


app.get('/', (req, res) => {
  res.send('placeholder')
});

app.post('/data', (req, res) => {
  const data = req.body;
  console.log('Received data:', data);
})

app.listen(port, () => {
  console.log(`running on port ${port}`);
})