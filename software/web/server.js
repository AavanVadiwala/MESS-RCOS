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

const SENSOR_COLUMNS = {
  bme280: ['temperature', 'humidity', 'pressure'],
  sgp30: ['eco2', 'tvoc'],
  bh1750: ['lux'],
};
const SENSOR_TYPE = Object.keys(SENSOR_COLUMNS);
const MAX_LIMIT = 1000;

// data validation to make sure its good to ship to whoevers asking
function isValidRow(r, type) {
  return r && typeof r === 'object'
    && Number.isInteger(r.id)
    && r.recorded_at instanceof Date && !Number.isNaN(r.recorded_at.getTime())
    && Number.isInteger(r.sensor_id)
    && typeof r.loc === 'string'
    && SENSOR_COLUMNS[type].every((c) => typeof r[c] === 'number' && Number.isFinite(r[c]));
}

// Usage: /api/bme280?limit=50  -> newest N rows (default 10, max 1000)
app.get('/api/:type', async (req, res) => {
  const type = req.params.type;
  if (!SENSOR_TYPE.includes(type)) {
    return res.status(400).json({ error: `Invalid sensor type, expected one of: ${SENSOR_TYPE.join(', ')}` });
  }
  // limit validation
  let limit = 10;
  if (req.query.limit !== undefined) {
    limit = Number(req.query.limit);
    if (typeof req.query.limit !== 'string' || !Number.isInteger(limit) || limit < 1 || limit > MAX_LIMIT) {
      return res.status(400).json({ error: `Expected limit to be an integer from 1 to ${MAX_LIMIT}` });
    }
  }
  try {
    const result = await pool.query(`SELECT * FROM ${type}_data ORDER BY id DESC LIMIT $1`, [limit]);
    const bad = result.rows.filter((r) => !isValidRow(r, type));
    if (bad.length) {
      console.error(`Malformed ${type}_data rows:`, bad);
      return res.status(500).json({
        error: `Expected {id, recorded_at, ${SENSOR_COLUMNS[type].join(', ')}, sensor_id, loc}`,
      });
    }
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