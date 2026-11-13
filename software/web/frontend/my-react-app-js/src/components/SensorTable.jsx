import { useEffect, useState } from "react";

// Polls GET /api/:type (see software/web/server.js) and renders the rows.
// columns: [{ key: "temperature", label: "Temperature (C)" }, ...]
function SensorTable({ type, columns, limit = 10, intervalMs = 2000 }) {
  const [rows, setRows] = useState([]);
  const [error, setError] = useState(null);

  useEffect(() => {
    let cancelled = false;

    async function load() {
      try {
        const res = await fetch(`/api/${type}?limit=${limit}`);
        const body = await res.json();
        if (!res.ok) throw new Error(body.error || `HTTP ${res.status}`);
        if (!cancelled) {
          setRows(body);
          setError(null);
        }
      } catch (err) {
        if (!cancelled) setError(err.message);
      }
    }

    load();
    const id = setInterval(load, intervalMs);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [type, limit, intervalMs]);

  return (
    <>
      {error && <p style={{ color: "red" }}>Error: {error}</p>}
      <table>
        <thead>
          <tr>
            <th>Time</th>
            {columns.map((c) => (
              <th key={c.key}>{c.label}</th>
            ))}
            <th>Location</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((r) => (
            <tr key={r.id}>
              <td>{new Date(r.recorded_at).toLocaleString()}</td>
              {columns.map((c) => (
                <td key={c.key}>{r[c.key]}</td>
              ))}
              <td>{r.loc}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </>
  );
}

export default SensorTable;
