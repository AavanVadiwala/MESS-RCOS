import { useState } from 'react'
import './App.css'
import TableBME from './components/TableBME';
import TableSGP from './components/TableSGP';
import TableBH from './components/TableBH';

function App() {

  const [web_state, setState] = useState('bme')

  return (
    <>
    <h1>MESS</h1>
    <div style = {{marginBottom: '20px'}}>
      <button onClick={() => setState('bme')}>BME280</button>
      <button onClick={() => setState('sgp')}>SGP30</button>
      <button onClick={() => setState('bh')}>BH1750</button>


    </div>
    {web_state === 'bme'&& (
      <>
      <h2>BME280</h2>
      <TableBME></TableBME>
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
}

export default App
