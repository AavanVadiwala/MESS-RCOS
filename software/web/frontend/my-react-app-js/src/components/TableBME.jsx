import data from "../../public/dummy_data.json";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';

// apparently vite parses json files by default sooooooo
function TableBME() {
  // TODO: make it automatically display the header of what is being read, actually,
  // just request that the data from each sensor is read seperately
  // table currently hardcoded, make it dynamic later

  // time is used as the key due to it being unique, also a bit more reliable
  const listitem = data.bme280.readings.map((r) => (
    <tr key={r.time}>
      <th>{r.time}</th>
      <th>{r.temperature}</th>
      <th>{r.humidity}</th>
      <th>{r.pressure}</th>
    </tr>
  ));
  return (
    <table>
      <tr>
        <th>Time</th>
        <th>Temperature* ({data.bme280.unit.temperature})</th>
        <th>Humidity ({data.bme280.unit.humidity})</th>
        <th>Pressure ({data.bme280.unit.pressure})</th>
      </tr>
      {listitem}
    </table>
  );
}

//display the graphs of the read in data
function GraphBME(){
  return (
  <div>
  <div style = {{width:'100%', height:300}}>
    <ResponsiveContainer width = "100%" height ="100%">
      <LineChart data = {data.bme280.readings} margin ={{top:5, right: 30, left: 20, bottom:5}}>
        <CartesianGrid strokeDasharray="3 3"/>
          <XAxis dataKey="time"/>
          <YAxis/>
          <Tooltip/>
          <Legend/>
          <Line type = "monotone" dataKey="temperature" stroke="#8884d8" activeDot={{r:8}}/>
      </LineChart>
    </ResponsiveContainer>
  </div>
  <div style = {{width:'100%', height:300}}>
    <ResponsiveContainer width = "100%" height ="100%">
      <LineChart data = {data.bme280.readings} margin ={{top:5, right: 30, left: 20, bottom:5}}>
        <CartesianGrid strokeDasharray="3 3"/>
          <XAxis dataKey="time"/>
          <YAxis/>
          <Tooltip/>
          <Legend/>
          <Line type = "monotone" dataKey="humidity" stroke="#84add8" activeDot={{r:8}}/>
      </LineChart>
    </ResponsiveContainer>
  </div>
  <div style = {{width:'100%', height:300}}>
    <ResponsiveContainer width = "100%" height ="100%">
      <LineChart data = {data.bme280.readings} margin ={{top:5, right: 30, left: 20, bottom:5}}>
        <CartesianGrid strokeDasharray="3 3"/>
          <XAxis dataKey="time"/>
          <YAxis/>
          <Tooltip/>
          <Legend/>
          <Line type = "monotone" dataKey="pressure" stroke="#c345aa" activeDot={{r:8}}/>
      </LineChart>
    </ResponsiveContainer>
  </div>
  </div>);
}

export default function BME(){
  return (
    <div>
      <GraphBME />
      <TableBME />
    </div>
  );
}
