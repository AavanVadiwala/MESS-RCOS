import SensorTable from "./SensorTable";

const COLUMNS = [
  { key: "temperature", label: "Temperature (C)" },
  { key: "humidity", label: "Humidity (%RH)" },
  { key: "pressure", label: "Pressure (hPa)" },
];

function TableBME() {
  return <SensorTable type="bme280" columns={COLUMNS} />;
}

export default TableBME;
