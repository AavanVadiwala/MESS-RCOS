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

// Usage: /api/latest  -> { bme280: [...], sgp30: [...], bh1750: [...] } with the newest row
//        for every loc of each sensor. Declared before /api/:type so it isn't read as a type.
app.get('/api/latest', async (_req, res) => {
  try {
    const out = {};
    for (const type of SENSOR_TYPE) {
      const result = await pool.query(
        `SELECT DISTINCT ON (loc) * FROM ${type}_data ORDER BY loc, id DESC`);
      const bad = result.rows.filter((r) => !isValidRow(r, type));
      if (bad.length) {
        console.error(`Malformed ${type}_data rows:`, bad);
        return res.status(500).json({
          error: `Expected {id, recorded_at, ${SENSOR_COLUMNS[type].join(', ')}, sensor_id, loc}`,
        });
      }
      out[type] = result.rows;
    }
    res.json(out);
  } catch (err) {
    console.error('Error executing query', err.stack);
    res.status(500).json({ error: 'Error retrieving data' });
  }
});

// Usage: /api/bme280?limit=50  -> newest N rows (default 10, max 1000)
//        /api/bme280?limit=-1  -> every row in bme280_data
//        /api/bme280?from=2026-11-20T00:00:00Z&to=...  -> only rows recorded in that range
//        /api/bme280?loc=lab  -> only rows whose loc is exactly "lab"
app.get('/api/:type', async (req, res) => {
  const type = req.params.type;
  if (!SENSOR_TYPE.includes(type)) {
    return res.status(400).json({ error: `Invalid sensor type, expected one of: ${SENSOR_TYPE.join(', ')}` });
  }
  // limit validation
  let limit = 10;
  if (req.query.limit !== undefined) {
    limit = Number(req.query.limit);
    if (typeof req.query.limit !== 'string' || !Number.isInteger(limit)
        || (limit !== -1 && (limit < 1 || limit > MAX_LIMIT))) {
      return res.status(400).json({ error: `Expected limit to be -1 (all rows) or an integer from 1 to ${MAX_LIMIT}` });
    }
  }
  // optional from/to (ISO date-times) bound recorded_at
  const range = {};
  for (const k of ['from', 'to']) {
    range[k] = null;
    if (req.query[k]) {
      const d = new Date(req.query[k]);
      if (typeof req.query[k] !== 'string' || Number.isNaN(d.getTime())) {
        return res.status(400).json({ error: `Expected ${k} to be an ISO date-time` });
      }
      range[k] = d;
    }
  }
  // optional loc keeps only rows from that exact location
  if (req.query.loc !== undefined && typeof req.query.loc !== 'string') {
    return res.status(400).json({ error: 'Expected loc to be a string' });
  }
  const loc = req.query.loc || null;
  try {
    // LIMIT NULL in Postgres = no limit, so -1 returns the whole table
    const result = await pool.query(
      `SELECT * FROM ${type}_data
       WHERE ($2::timestamptz IS NULL OR recorded_at >= $2)
         AND ($3::timestamptz IS NULL OR recorded_at <= $3)
         AND ($4::text IS NULL OR loc = $4)
       ORDER BY id DESC LIMIT $1`,
      [limit === -1 ? null : limit, range.from, range.to, loc]);
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



// "readings" may be omitted to send a single reading with its fields at the top level.
// 
app.post('/data', async (req, res) => {
  const { sensor, sensor_id, loc } = req.body ?? {};
  if (!SENSOR_TYPE.includes(sensor)) {
    return res.status(400).json({ error: `Invalid sensor, expected one of: ${SENSOR_TYPE.join(', ')}` });
  }
  if (!Number.isInteger(sensor_id) || typeof loc !== 'string' || !loc) {
    return res.status(400).json({ error: 'Expected integer sensor_id and non-empty string loc' });
  }
  const readings = Array.isArray(req.body.readings) ? req.body.readings : [req.body];
  if (!readings.length || readings.length > MAX_LIMIT) {
    return res.status(400).json({ error: `Expected 1 to ${MAX_LIMIT} readings` });
  }

  const cols = SENSOR_COLUMNS[sensor];
  const rows = [];
  for (const r of readings) {
    const time = r?.time === undefined ? new Date() : new Date(r.time);
    if (Number.isNaN(time.getTime())
        || !cols.every((c) => typeof r[c] === 'number' && Number.isFinite(r[c]))) {
      return res.status(400).json({ error: `Each reading needs ${cols.join(', ')} as numbers and an optional ISO time` });
    }
    rows.push([time, ...cols.map((c) => r[c]), sensor_id, loc]);
  }

  const placeholders = [...cols, 'recorded_at', 'sensor_id', 'loc'].map((_, i) => `$${i + 1}`).join(', ');
  const sql = `INSERT INTO ${sensor}_data (recorded_at, ${cols.join(', ')}, sensor_id, loc) VALUES (${placeholders})`;
  const client = await pool.connect();
  try {
    await client.query('BEGIN');
    for (const row of rows) await client.query(sql, row);
    await client.query('COMMIT');
    res.send('data recieved :)');
  } catch (err) {
    await client.query('ROLLBACK').catch(() => {});
    console.error('Error inserting data', err.stack);
    res.status(500).json({ error: 'Error saving data' });
  } finally {
    client.release();
  }
})



app.listen(port, () => {
  console.log(`running on port ${port}`);
})