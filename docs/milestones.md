# Milestones

*Converted from the mentor-provided `MESS-milestones.pdf`.*

## Milestone 1 — Data collection & transmission (~end of September)

1. Read and confirm data from each sensor individually on the ESP32.
   - I2C for the SGP30 and BH1750.
   - I2C or SPI for the BME280.
2. Send sensor data wirelessly from an ESP32 to a server (e.g. a Node.js server on a laptop). Printing received values to a terminal is enough.

> Work in pairs so everyone learns the hardware and toolchain. Learn all the sensors now, but for the mesh work it's easiest to start with one sensor and add the rest once meshing works.

## Milestone 2 — Meshing & data application (~end of October)

1. Build the mesh backbone.
   - Multiple ESP32s communicate directly with each other.
   - Data can be passed along through the mesh.
   - One ESP32 can read its own sensors, receive data from another node, **and** forward its data at the same time (consider interrupts vs. polling).
2. Grow the server into something useful.
   - A user interface beyond printing values.
   - Log data as it arrives and record which node sent it.
   - Show meaning: graphs over time, min/max/average per node, etc.

> Work as one large group using all 6 nodes. Aim for a single "omnibus" firmware that every node runs.

## Milestone 3 — Testing gauntlet & stretch goals (~end of November, before Thanksgiving)

1. Push the system until it breaks, then make it more resilient. For example:
   - Can it run for long periods without problems?
   - How far apart can nodes be?
   - Can one node relay traffic from many others at once?
   - If a node drops out, does the rest of the network adapt?
2. Attempt stretch goals (below) or your own ideas.

## Stretch goals

- **Reliability:** could it run unattended in a real building for a day? A week? What fails first, and how do you harden it?
- **Security:** keep nodes hidden on the network and data visible only to authorized users.
- **Cross-network:** two separate meshes feeding one server that aggregates and analyzes both.
- **All sensors per node, with the BME280 on SPI**, collected without race conditions.
- **RTOS:** replace polling/interrupts with RTOS tasks for sensing, receiving, and sending simultaneously.
- **Routing optimization:** balance speed against reliability in how data moves through the mesh.
- **Acting on the data:** alerts for poor air quality, comparisons against safe/efficient temperature ranges, lights as an occupancy proxy, and other real-world uses.
- **Integration:** if the team splits across stretch goals, merge everything back into one network.

Original ideas are encouraged — bonus points for a cool idea that works.

## Expo — Project presentations (~December)
