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

function App() {
  const [web_state, setState] = useState("info");
  const [render_limit, setlimit] = useState(10);
  const sensor = SENSORS[web_state];

  const handleSumbit = (e) => {
    e.preventDefault();
    const form = e.target;
    const formData = new FormData(form);
    setlimit(formData.get("query"));
  };

  return (
    <>
      <h1>MESS</h1>
      <div style={{ marginBottom: "20px" }}>
        <button onClick={() => setState("bme")}>BME280</button>
        <button onClick={() => setState("sgp")}>SGP30</button>
        <button onClick={() => setState("bh")}>BH1750</button>
        <button onClick={() => setState("info")}>Info</button>
      </div>

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

      {sensor && (
        <>
          <form onSubmit={handleSumbit}>
            <input name="query"></input>
            <button type="submit">Set Limit</button>
          </form>
          <iframe
            src = "http://127.0.0.1:3589"
            title="Live Data"
            width="100%"
            lengh = "200%"
          
          />
          <h2>{sensor.title}</h2>
          <SensorTable
            key={sensor.type}
            type={sensor.type}
            columns={sensor.columns}
            limit={render_limit}
          />
        </>
      )}
    </>
  );
}

export default App;
