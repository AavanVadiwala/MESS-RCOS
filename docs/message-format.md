# Message Format (DRAFT — not yet agreed)

This is the contract between firmware and server. Agree on it **before** writing much code so both sides can build against fake data in parallel. Changes need a review from both Firmware and Software.

## Proposed reading message

```json
{
  "v": 1,
  "node": "a4:cf:12:34:56:78",
  "seq": 1042,
  "uptime_ms": 360512,
  "hops": 2,
  "readings": {
    "temp_c": 22.41,
    "humidity_pct": 38.2,
    "pressure_hpa": 1012.6,
    "eco2_ppm": 412,
    "tvoc_ppb": 15,
    "lux": 310.0
  }
}
```

| Field | Meaning |
|---|---|
| `v` | Format version; bump on breaking changes |
| `node` | Sender's MAC address (stable node ID) |
| `seq` | Per-node counter; lets the server spot lost or duplicate packets |
| `uptime_ms` | Time since node boot. Nodes have no real clock — the **server** stamps wall-clock time on arrival |
| `hops` | Incremented by each forwarding node; useful for routing analysis |
| `readings` | Only include sensors that are present and working |

## Open points

- JSON (easy to debug) vs. a packed binary struct (smaller, matters if packets get large).
- Reading interval (e.g. every 10 s?).
- Separate status/heartbeat message (battery, parent node, RSSI)?
