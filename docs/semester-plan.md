# Technical Specification & Semester Plan

*Converted from the "MESS Technical Specification & Semester Plan" Google Doc (Sept 18, 2026). This supersedes the earlier "Mess Plan" and "MESS Dev" drafts.*

## 1. Overview & vision

- **Project:** MESS (Mesh Environment Sensor System), RCOS sponsored project.
- **Vision:** an interconnected mesh of ESP32 microcontrollers with gas, light, temperature, and humidity sensors. Nodes talk to each other and stream data to a central server for monitoring and visualization.
- **Core objective:** compare environmental data between older and newer campus buildings (e.g. West Hall vs. JEC, or Quad/Nugent vs. Blitman/RAHPS B) and detect condition anomalies such as open windows or HVAC shifts.
- **Duration:** the project spans two semesters.

## 2. Design decisions

- **One ESP32 per multi-sensor cluster**, rather than one per sensor, to save power and footprint and keep the mesh node count manageable.
- **Dual-mode web UI:**
  - *Live Stream mode* — real-time telemetry from active nodes.
  - *Analytics & Findings mode* — historical graphs, anomaly alerts, and building comparisons.
- **Analysis in R and Python** for statistics, time-series modeling, and old-vs-new building comparison.

## 3. Sub-teams

| Sub-team | Responsibilities |
|---|---|
| Firmware | ESP32 configuration, C driver development, mesh protocol and routing |
| Hardware | Sensor calibration, wiring, power/battery optimization, stress-test setup |
| Software & Infrastructure | Central server, database, payload ingestion, API design |
| Data Science & Interactive UI | Web dashboard, live visualization, R/Python building-comparison models |

In practice, Software and Data Science currently operate as one group — see [team.md](team.md).

## 4. Semester schedule

### September — configuration & sensor prototyping
- **Weeks 1–2:** set up ESP32 toolchains; form sub-teams and groups of 3; wire and test each sensor individually.
- **Weeks 3–4:** baseline data reading and serial/Wi-Fi transmission; write the controlled-room stress-test protocol.

### October — mesh networking & server/UI prototype
- **Weeks 5–6:** node-to-node mesh communication; central ingestion server.
- **Weeks 7–8:** first UI prototype (live stream + findings overview); stress test in a single room.

### November — reliability & limits testing
- **Weeks 9–10:** range, node-to-node transmission rate, and battery runtime tests; refine pipeline and persistent storage for high-frequency data.
- **Weeks 11–12:** finalize R/Python comparison models; field tests that simulate changes (e.g. opening a window).

### December — building comparison & presentation
- **Weeks 13–14:** deploy nodes in the target buildings; capture and model the comparison data.
- **Week 15:** stabilize, finish documentation, present at the RCOS demo/Expo.
