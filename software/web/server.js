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

const SENSOR_TYPE = ['bme280', 'sgp30', 'bh1750'];

// Usage: /api/bme280?limit=50  -> newest N rows (default 10, max 1000)
app.get('/api/:type', async (req, res) => {
  const type = req.params.type;
  if (!SENSOR_TYPE.includes(type)) {
    return res.status(400).json({ error: 'Invalid sensor type' });
  }
  const limit = Math.min(Math.max(parseInt(req.query.limit, 10) || 10, 1), 1000);
  try {
    const result = await pool.query(`SELECT * FROM ${type}_data ORDER BY id DESC LIMIT $1`, [limit]);
    res.json(result.rows);
  } catch (err) {
    console.error('Error executing query', err.stack);
    res.status(500).json({ error: 'Error retrieving data' });
  }
});



app.post('/data', (req, res) => {
  const data = req.body;
  // data format TBD
  console.log('Received data:', data);
})



app.listen(port, () => {
  console.log(`running on port ${port}`);
})