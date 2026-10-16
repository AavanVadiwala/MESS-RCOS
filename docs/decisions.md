# Decisions & Open Questions

Record significant choices here with a date and a one-line reason, so future members (including the spring team) know why things are the way they are.

## Decided

| Date | Decision | Why |
|---|---|---|
| 2026-09 | Use ESP-IDF (VS Code extension + CLI) as the toolchain | Official, best-supported; per quickstart guide |
| 2026-09 | One ESP32 per multi-sensor cluster | Saves power and footprint; fewer mesh nodes to manage |
| 2026-09 | Target comparison: an old vs. a new campus building | Core project objective (see semester plan) |
| 2026-09 | Dual-mode web UI (Live Stream + Analytics & Findings) | From tech spec |
| 2026-09 | R and Python for analysis | From tech spec |

## Hardware constraints (from mentor Q&A)

- 6 sets of hardware total (one per two members); starting with 4.
- Kits include power and enclosure parts.
- The allocation is fixed; replacements are possible but a hassle — be careful with boards.

## Open questions

- [ ] **Mesh protocol:** ESP-WIFI-MESH vs. ESP-NOW (vs. something else). Blocks Milestone 2 firmware.
- [ ] **Board model / Wi-Fi band:** campus Wi-Fi is 5 GHz; the classic ESP32 is 2.4 GHz only (the ESP32-C5 supports 5 GHz). How will the root node reach the server?
- [ ] **Message format:** finalize [message-format.md](message-format.md).
- [ ] **Server stack and database** (Node.js + SQLite? Postgres? MQTT?).
- [ ] **Target buildings:** West Hall vs. JEC, or Quad/Nugent vs. Blitman/RAHPS B? Who grants permission for deployment?
