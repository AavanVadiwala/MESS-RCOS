import data from "../../public/dummy_data.json";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';
import SensorTable from "./SensorTable";

const COLUMNS = [
  { key: "temperature", label: "Temperature (C)" },
  { key: "humidity", label: "Humidity (%RH)" },
  { key: "pressure", label: "Pressure (hPa)" },
];

function TableBME() {
  return <SensorTable type="bme280" columns={COLUMNS} />;
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
