import { useState } from "react";
import "./App.css";
import SensorTable from "./components/SensorTable";

const SENSORS = {
  bme: {
    title: "BME280",
    type: "bme280",
    columns: [
      { key: "temperature", label: "Temperature (°C)" },
      { key: "humidity", label: "Humidity (%)" },
      { key: "pressure", label: "Pressure (hPa)" },
    ],
  },
  sgp: {
    title: "SGP30",
    type: "sgp30",
    columns: [
      { key: "eco2", label: "eCO2 (ppm)" },
      { key: "tvoc", label: "TVOC (ppb)" },
    ],
  },
  bh: {
    title: "BH1750",
    type: "bh1750",
    columns: [{ key: "lux", label: "Light (lux)" }],
  },
};

// Shiny container is published on 3838 (see software/docker-compose.yml)
const SHINY_URL = `${window.location.protocol}//${window.location.hostname}:3838/`;

// today's local date as YYYY-MM-DD, for the datetime-local defaults
const today = () => {
  const d = new Date();
  const pad = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
};

function App() {
  const [web_state, setState] = useState("info");
  const [render_limit, setlimit] = useState(10);
  const [locEnabled, setLocEnabled] = useState(false);
  const [locValue, setLocValue] = useState("");
  const [timeEnabled, setTimeEnabled] = useState(false);
  const [timeFrom, setTimeFrom] = useState(`${today()}T00:00`);
  const [timeTo, setTimeTo] = useState(`${today()}T23:59`);
  const sensor = SENSORS[web_state];

  const handleLimit = (e) => {
    e.preventDefault();
    const form = e.target;
    const formData = new FormData(form);
    setlimit(formData.get("query"));
  };

  return (
    <>
      <h1>MESS</h1>
      {/*button interface to select what to display*/ }
      <div style={{ marginBottom: "20px" }}>
        <button onClick={() => setState("bme")}>BME280</button>
        <button onClick={() => setState("sgp")}>SGP30</button>
        <button onClick={() => setState("bh")}>BH1750</button>
        <button onClick={() => setState("info")}>Info</button>
        <button onClick={() => setState("analysis")}>Analysis</button>
      </div>

    {/*display basic info on what MESS is and what the sensors do*/}
      {web_state === "info" && (
        <>
          <h2>What is MESS</h2>
          <p>
            Mesh Environment Sensor System, essentially a network of
            microcontrollers with a variety of sensors attached
          </p>
          <h2>The Sensors</h2>
          <h3>BME280</h3>
          <p>Temperature, humidity and pressure sensor</p>
          <h3>SGP30</h3>
          <p>Multi-pixel gas sensor</p>
          <h3>BH1750</h3>
          <p>Ambient light sensor</p>
        </>
      )}

      {web_state === "analysis" && (
        <>
          <iframe
            title="Analysis"
            src={SHINY_URL}
            style={{ width: "100%", height: "85vh", border: "none" }}
          />
        </>
      )}

      {/*display the sensor clicked*/}
      {/*that being the corresponding data and a graph showing the data*/}
      {sensor && (
        <>
          <div className="controls">
            <form onSubmit={handleLimit}>
              <label>
                Load latest:{" "}
                <input name="query" defaultValue={render_limit}></input>
              </label>
              <button type="submit">Go</button>
            </form>

            <div>
              <label>
                <input type="checkbox" checked={timeEnabled} onChange={(e) => setTimeEnabled(e.target.checked)} />
                {" "}Time filter{" "}
              </label>
              <label>
                From <input type="datetime-local" value={timeFrom} disabled={!timeEnabled} onChange={(e) => setTimeFrom(e.target.value)} />
              </label>{" "}
              <label>
                to <input type="datetime-local" value={timeTo} disabled={!timeEnabled} onChange={(e) => setTimeTo(e.target.value)} />
              </label>
            </div>

            <div>
              <label>
                <input type="checkbox" checked={locEnabled} onChange={(e) => setLocEnabled(e.target.checked)} />
                {" "}Location filter{" "}
              </label>
              <input value={locValue} disabled={!locEnabled} onChange={(e) => setLocValue(e.target.value)} />
            </div>
          </div>

          <h2>{sensor.title}</h2>
          <SensorTable
            key={sensor.type}
            type={sensor.type}
            columns={sensor.columns}
            limit={render_limit}
            timeFrom={timeEnabled ? timeFrom : ""}
            timeTo={timeEnabled ? timeTo : ""}
            loc={locEnabled ? locValue.trim() : ""}
          />
        </>
      )}
    </>
  );
}

export default App;
