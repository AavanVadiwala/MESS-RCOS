const express = require('express');
const pg = require('pg');
const path = require('path');

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

// Confirm the database connection
pool.query('SELECT 1')
  .then(() => console.log('Database connection successful'))
  .catch((err) => console.error('Database connection error', err.stack));

// Serve the table page
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'index.html'));
});

// Serve the table page script
app.get('/table.js', (req, res) => {
  res.sendFile(path.join(__dirname, 'table.js'));
});

const SENSOR_TYPE = ['light', 'gas', 'humid', 'temp'];

// Latest rows as JSON, fetched by table.js
app.get('/api/:type', async (req, res) => {
  const type = req.params.type;
  if (!SENSOR_TYPE.includes(type)) {
    return res.status(400).json({ error: 'Invalid sensor type' });
  }
  try {
    const result = await pool.query(`SELECT * FROM ${type}_data ORDER BY ID DESC LIMIT 10`);
    res.json(result.rows);
  } catch (err) {
    console.error('Error executing query', err.stack);
    res.status(500).json({ error: 'Error retrieving data' });
  }
});

app.post('/data', (req, res) => {
  const data = req.body;
  console.log('Received data:', data);
})

app.listen(port, () => {
  console.log(`running on port ${port}`);
})