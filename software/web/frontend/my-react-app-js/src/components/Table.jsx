import data from "../../../../public/dummy_data.json";

// apparently vite parses json files by default sooooooo
function Table() {
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

export default Table;
