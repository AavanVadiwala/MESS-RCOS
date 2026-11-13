import { useState } from 'react'
import './App.css'
import TableBME from './components/TableBME';
import TableSGP from './components/TableSGP';
import TableBH from './components/TableBH';
import GraphBME from './components/TableBME';
import GraphBH from './components/TableBH';
import GraphSGP from './components/TableSGP';


//diplay basic info on MESS, what it is and what the sensors do
//diplay the table of data on the different sensors
//later on display a graphs for the sensors
function App() {

  const [web_state, setState] = useState('info')

  return (
    <>
    <h1>MESS</h1>
    <div style = {{marginBottom: '20px'}}>
      <button onClick={() => setState('bme')}>BME280</button>
      <button onClick={() => setState('sgp')}>SGP30</button>
      <button onClick={() => setState('bh')}>BH1750</button>
      <button onClick={()=> setState('info')}>Info</button>


    </div>
    {web_state === 'info' && (
      <>
      <h2>What is MESS</h2>
      <p>Mesh Environment Sensor System, essentially a network of 
        microcontrollers with a variety of sensors attached</p>
        <br></br>
        <br></br>
      <h2>The Sensors </h2>
      <h3>BME280</h3>
      <p>Humidity and pressure sensor</p>
      <h3>SGP30</h3>
      <p>Multi-pixel gas sensor</p>
      <h3>BH1750</h3>
      <p>Ambient light sensor</p>
      <br></br>
      <br></br>
      <br></br>
      <br></br>
      </>
      
    )}
    {web_state === 'bme'&& (
      <>
      <h2>BME280</h2>
      <TableBME/>
      
      </>
    )}
    {web_state === 'sgp'&&(
      <>
      <h2>SGP30</h2>
      <TableSGP></TableSGP>
      </>
    )}
    {web_state === 'bh'&&(
      <>
      <h2>BH1750</h2>
      <TableBH></TableBH>
      </>
    )}
      
    </>
  )
  const [web_state, setState] = useState("info");
  const [rows, setRows] = useState([]);
  const [error, setError] = useState(null);

  useEffect(() => {
    fetch("/api/")
      .then((res) => {
        if (!res.ok) throw new Error(`Request failed: ${res.status}`);
        return res.json();
      })
      .then(setRows)
      .catch((err) => setError(err.message));
  }, [web_state]);

  if (error) return <p>Error: {error}</p>;
  // return (
  //   <ul>
  //     {rows.map((row) => (
  //       <li key={row.id}>{JSON.stringify(row)}</li>
  //     ))}
  //   </ul>
  // );

  return (
    <>
      <h1>MESS</h1>
      <div style={{ marginBottom: "20px" }}>
        <button onClick={() => setState("bme")}>BME280</button>
        <button onClick={() => setState("sgp")}>SGP30</button>
        <button onClick={() => setState("bh")}>BH1750</button>
        <button onClick={() => setState("info")}>Info</button>
      </div>
      {web_state === "info" && (
        <>
          <h2>What is MESS</h2>
          <p>
            Mesh Environment Sensor System, essentially a network of
            microcontrollers with a variety of sensors attached
          </p>
          <br></br>
          <br></br>
          <h2>The Sensors </h2>
          <h3>BME280</h3>
          <p>Humidity and pressure sensor</p>
          <h3>SGP30</h3>
          <p>Multi-pixel gas sensor</p>
          <h3>BH1750</h3>
          <p>Ambient light sensor</p>
          <br></br>
          <br></br>
          <br></br>
          <br></br>
        </>
      )}
      {web_state === "bme" && (
        <>
          <h2>BME280</h2>
          <TableBME></TableBME>
        </>
      )}
      {web_state === "sgp" && (
        <>
          <h2>SGP30</h2>
          <TableSGP></TableSGP>
        </>
      )}
      {web_state === "bh" && (
        <>
          <h2>BH1750</h2>
          <TableBH></TableBH>
        </>
      )}
    </>
  );
}

export default App
