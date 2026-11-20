import { useEffect, useMemo, useState } from "react";

// Polls GET /api/:type (see software/web/server.js) and renders the rows.
// columns: [{ key: "temperature", label: "Temperature (C)" }, ...]
// Click a column header to sort by it; click again to reverse.
// timeFrom/timeTo are datetime-local values ("YYYY-MM-DDTHH:MM", local time,
// either may be empty) sent to the API to bound recorded_at.
function SensorTable({ type, columns, limit = 10, intervalMs = 2000, timeFrom = "", timeTo = "", loc = "" }) {
  const [rows, setRows] = useState([]);
  const [error, setError] = useState(null);
  const [sort, setSort] = useState({ key: "recorded_at", dir: "desc" });

  useEffect(() => {
    let cancelled = false;

    async function load() {
      try {
        const params = new URLSearchParams({ limit });
        for (const [k, v] of [["from", timeFrom], ["to", timeTo]]) {
          const d = new Date(v);
          if (v && !Number.isNaN(d.getTime())) params.set(k, d.toISOString());
        }
        if (loc) params.set("loc", loc);
        const res = await fetch(`/api/${type}?${params}`);
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
  }, [type, limit, intervalMs, timeFrom, timeTo, loc]);

  const sortedRows = useMemo(() => {
    const value = (r) =>
      sort.key === "recorded_at" ? new Date(r.recorded_at).getTime()
      : sort.key === "loc" ? r.loc
      : Number(r[sort.key]);

    return [...rows].sort((a, b) => {
      const va = value(a);
      const vb = value(b);
      const cmp = typeof va === "string" ? va.localeCompare(vb) : va - vb;
      return sort.dir === "asc" ? cmp : -cmp;
    });
  }, [rows, sort]);

  function toggleSort(key) {
    setSort((s) =>
      s.key === key
        ? { key, dir: s.dir === "asc" ? "desc" : "asc" }
        : { key, dir: "desc" } // first click on a new column: highest first
    );
  }

  function Header({ sortKey, children }) {
    const active = sort.key === sortKey;
    return (
      <th
        className={`sortable ${active ? "sorted" : ""}`}
        onClick={() => toggleSort(sortKey)}
      >
        {children}
        <span className="arrow">{active ? (sort.dir === "asc" ? "▲" : "▼") : "↕"}</span>
      </th>
    );
  }

  return (
    <>
      {error && <p className="error">Error: {error}</p>}
      <div className="table-wrap">
        <table>
          <thead>
            <tr>
              <Header sortKey="recorded_at">Time</Header>
              {columns.map((c) => (
                <Header key={c.key} sortKey={c.key}>{c.label}</Header>
              ))}
              <Header sortKey="loc">Location</Header>
            </tr>
          </thead>
          <tbody>
            {sortedRows.map((r) => (
              <tr key={r.id}>
                <td>{new Date(r.recorded_at).toLocaleString()}</td>
                {columns.map((c) => (
                  <td key={c.key}>{Number(r[c.key]).toFixed(2)}</td>
                ))}
                <td>{r.loc}</td>
              </tr>
            ))}
          </tbody>
        </table>
        {rows.length === 0 && !error && <p className="empty">No readings yet</p>}
      </div>
    </>
  );
}

export default SensorTable;