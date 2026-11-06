import { useState } from "react";
import "./App.css";
import data from "../../../public/dummy_data.json";

function SensorTable({ sensorName, sensor }) {
  const columns = Object.keys(sensor.unit); 
  const rows = Object.entries(sensor.readings); // [[time, {temperature, ...}], ...]

  return (
    <div>
      <h2>{sensorName}</h2>
      <table>
        <thead>
          <tr>
            <th>Time</th>
            {columns.map((col) => (
              <th key={col}>
                {col} ({sensor.unit[col]})
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map(([time, reading]) => (
            <Row key={reading.time} reading={reading} columns={columns} />
          ))}
        </tbody>
      </table>
    </div>
  );
}

function Row({reading, columns }) {
  return (
    <tr>
      <td>{reading.time}</td>
      {columns.map((col) => (
        <td key={col}>{reading[col]}</td>
      ))}
    </tr>
  );
}

function App() {
  // MAKE IT MORE DYNAMIC, MAP THE WHOLE JSON FILE, FOR EVERY SENSOR, RENDER A TABLE FOR IT.
  // But then again, since the number of sensors is static, I can just hard code the rendering for it
  // the json is an object of objects

  return Object.entries(data).map(([sensorName, sensor]) => (
    <SensorTable key={sensorName} sensorName={sensorName} sensor={sensor} />
  ));
}

export default App;
