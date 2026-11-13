import data from "../../public/dummy_data.json";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';


// apparently vite parses json files by default sooooooo
function TableSGP() {
  // TODO: make it automatically display the header of what is being read, actually,
  // just request that the data from each sensor is read seperately
  // table currently hardcoded, make it dynamic later

  // time is used as the key due to it being unique, also a bit more reliable
  const listitem = data.sgp30.readings.map((r) => (
    <tr key={r.time}>
      <th>{r.time}</th>
      <th>{r.eco2}</th>
      <th>{r.tvoc}</th>
    </tr>
  ));
  return (
    <table>
      <tr>
        <th>Time</th>
        <th>ECO_2 ({data.sgp30.unit.eco2})</th>
        <th>TVOC ({data.sgp30.unit.tvoc})</th>
      </tr>
      {listitem}
    </table>
  );
}

//display the graphs of the read in data
function GraphSGP(){
  return (
  <div>
  <div style = {{width:'100%', height:300}}>
    <ResponsiveContainer width = "100%" height ="100%">
      <LineChart data = {data.sgp30.readings} margin ={{top:5, right: 30, left: 20, bottom:5}}>
        <CartesianGrid strokeDasharray="3 3"/>
          <XAxis dataKey="time"/>
          <YAxis/>
          <Tooltip/>
          <Legend/>
          <Line type = "monotone" dataKey="eco2" stroke="#8884d8" activeDot={{r:8}}/>
      </LineChart>
    </ResponsiveContainer>
  </div>
  <div style = {{width:'100%', height:300}}>
    <ResponsiveContainer width = "100%" height ="100%">
      <LineChart data = {data.sgp30.readings} margin ={{top:5, right: 30, left: 20, bottom:5}}>
        <CartesianGrid strokeDasharray="3 3"/>
          <XAxis dataKey="time"/>
          <YAxis/>
          <Tooltip/>
          <Legend/>
          <Line type = "monotone" dataKey="tvoc" stroke="#84add8" activeDot={{r:8}}/>
      </LineChart>
    </ResponsiveContainer>
  </div>
  </div>);
}

export default function SGP(){
  return (
    <div>
      <GraphSGP />
      <TableSGP />
    </div>
  );
}