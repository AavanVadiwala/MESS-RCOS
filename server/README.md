# MESS server setup

The server connects to the collector ESP32 over USB, archives its readings locally,
and sends measurements to the API in [software](../software/). The Windows setup
is available now. Ubuntu setup is pending.

## Windows

1. Flash the same normal MESS firmware on each board. Connect the boards you
   want to configure over USB and close any serial monitors. Discovery briefly
   resets the boards through their USB serial adapters and checks for the MESS
   boot prompt before offering them for selection.
2. From the repository root in PowerShell, install the serial dependency and run
   the setup.

   ```powershell
   python -m pip install -r .\server\windows\requirements.txt
   python .\server\windows\collector.py
   ```

3. Choose the collector from the numbered list. The script saves the `node`
   role on every other detected ESP32, confirms each assignment, then sets the
   chosen board as collector. Afterward it reads only the collector's COM port.
   The other boards can run on separate USB power once their roles are saved.

Use `--manual-reset` if discovery misses a board whose adapter cannot reset it.
For subsequent runs, use `python .\server\windows\collector.py --collector COM10`
with the saved collector's port. This skips discovery and leaves saved roles alone.

## Start the visualization

With Docker Desktop running, open a second PowerShell terminal at the repository
root and run:

```powershell
.\server\windows\start-visualization.ps1
```

The script uses the existing Compose setup under `software/` and copies its
example environment file if needed. It opens the React tables at
http://localhost:3000. Shiny graphs are available at http://localhost:3838.
Use `docker compose down` from `software/` to stop the services.

## Saved data

`server/data/readings.sqlite3` stores the raw serial records, UTC receive times,
formatted API payloads, and delivery state. Each board MAC gets a stable integer
sensor ID in that database and serves as the API location label. BME280 pressure
is converted from Pa to hPa. Missing-sensor, warm-up, and error records stay in
the local archive. The software API accepts complete numeric measurements.

Collection can start before the visualization. The uploader retries pending
measurements when the API becomes available, including after restarting the
collector script. If an upload succeeds but its response is lost, a retry can
create a duplicate API row. Use `--endpoint http://HOST:3000/data` for an API
running on another computer, or `--database PATH` to choose an archive location.
