const express = require('express');
const path = require('path');
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
const frontendDir = process.env.FRONTEND_DIR
  || path.join(__dirname, 'frontend', 'my-react-app-js', 'dist');
app.use(express.static(frontendDir));

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