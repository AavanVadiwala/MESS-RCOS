import SensorTable from "./SensorTable";

const COLUMNS = [
  { key: "eco2", label: "eCO2 (ppm)" },
  { key: "tvoc", label: "TVOC (ppb)" },
];

function TableSGP() {
  return <SensorTable type="sgp30" columns={COLUMNS} />;
}

export default TableSGP;
