import data from "../../public/dummy_data.json";
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer } from 'recharts';

import SensorTable from "./SensorTable";

const COLUMNS = [{ key: "lux", label: "Lux (lx)" }];

function TableBH() {
  return <SensorTable type="bh1750" columns={COLUMNS} />;
}

//display the graphs of the read in data
function GraphBH(){
  return (
  <div>
  <div style = {{width:'100%', height:300}}>
    <ResponsiveContainer width = "100%" height ="100%">
      <LineChart data = {data.bh1750.readings} margin ={{top:5, right: 30, left: 20, bottom:5}}>
        <CartesianGrid strokeDasharray="3 3"/>
          <XAxis dataKey="time"/>
          <YAxis/>
          <Tooltip/>
          <Legend/>
          <Line type = "monotone" dataKey="lux" stroke="#8884d8" activeDot={{r:8}}/>
      </LineChart>
    </ResponsiveContainer>
  </div>
  </div>);
}

export default function BH(){
  return (
    <div>
      <GraphBH />
      <TableBH />
    </div>
  );
}