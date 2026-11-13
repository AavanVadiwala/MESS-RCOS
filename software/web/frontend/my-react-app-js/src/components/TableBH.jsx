import SensorTable from "./SensorTable";

const COLUMNS = [{ key: "lux", label: "Lux (lx)" }];

function TableBH() {
  return <SensorTable type="bh1750" columns={COLUMNS} />;
}

export default TableBH;
