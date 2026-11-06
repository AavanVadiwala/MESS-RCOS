import data from "../../public/dummy_data.json";

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

export default TableSGP;
