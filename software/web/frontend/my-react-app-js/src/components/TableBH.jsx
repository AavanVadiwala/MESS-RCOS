import data from "../../../../public/dummy_data.json";

// apparently vite parses json files by default sooooooo
function TableBH() {
  // TODO: make it automatically display the header of what is being read, actually,
  // just request that the data from each sensor is read seperately
  // table currently hardcoded, make it dynamic later

  // time is used as the key due to it being unique, also a bit more reliable
  const listitem = data.bh1750.readings.map((r) => (
    <tr key={r.time}>
      <th>{r.time}</th>
      <th>{r.lux}</th>
    </tr>
  ));
  return (
    <table>
      <tr>
        <th>Time</th>
        <th>Lux* ({data.bh1750.unit.lux})</th>
      </tr>
      {listitem}
    </table>
  );
}

export default TableBH;
