import { useEffect, useMemo, useState } from "react";
import RefreshRing from "./RefreshRing";

// readings older than this get a "(stale)" marker
const STALE_MS = 10 * 60 * 1000;

// One box per location, each showing that location's newest reading of every
// sensor. Polls GET /api/latest (see software/web/server.js).
// sensors: the SENSORS map from App.jsx ({ key: { title, type, columns } })
function LiveView({ sensors, intervalMs = 2000 }) {
  const [latest, setLatest] = useState({});
  const [error, setError] = useState(null);

  useEffect(() => {
    let cancelled = false;

    async function load() {
      try {
        const res = await fetch("/api/latest");
        const body = await res.json();
        if (!res.ok) throw new Error(body.error || `HTTP ${res.status}`);
        if (!cancelled) {
          setLatest(body);
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
  }, [intervalMs]);

  // { loc: { bme280: row, sgp30: row, ... } }
  const byLoc = useMemo(() => {
    const out = {};
    for (const [type, rows] of Object.entries(latest)) {
      for (const r of rows) (out[r.loc] ??= {})[type] = r;
    }
    return out;
  }, [latest]);

  const locations = Object.keys(byLoc).sort();

  return (
    <>
      <p>
        <RefreshRing intervalMs={intervalMs} /> Next update
      </p>
      {error && <p className="error">Error: {error}</p>}
      {locations.length === 0 && !error && <p className="empty">No readings yet</p>}
      <div className="live-grid">
        {locations.map((loc) => (
          // <details> keeps its open/closed state across polls because the keys are stable
          <details className="live-card" key={loc} open>
            <summary><h3>{loc}</h3></summary>
            {Object.values(sensors)
              .filter((s) => byLoc[loc][s.type])
              .map((s) => {
                const row = byLoc[loc][s.type];
                const stale = Date.now() - new Date(row.recorded_at).getTime() > STALE_MS;
                const badge = stale && (
                  <span className="stale">
                    <span className="stale-square" /> (stale)
                  </span>
                );
                return (
                  <details className="live-sensor" key={s.type} open>
                    <summary><h4>{s.title}</h4></summary>
                    {s.columns.map((c) => (
                      <div className="live-value" key={c.key}>
                        <span className="live-number">{Number(row[c.key]).toFixed(2)}</span>
                        <span className="live-label">{c.label}</span>
                      </div>
                    ))}
                    <p className="live-meta">{new Date(row.recorded_at).toLocaleString()} {badge}</p>
                  </details>
                );
              })}
          </details>
        ))}
      </div>
    </>
  );
}

export default LiveView;
