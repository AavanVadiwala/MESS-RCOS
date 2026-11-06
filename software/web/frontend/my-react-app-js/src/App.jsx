import { useState } from 'react'
import './App.css'
import TableBME from './components/TableBME';
import TableSGP from './components/TableSGP';
import TableBH from './components/TableBH';

function App() {

  return (
    <>
      <h2>BME280</h2>
      <TableBME></TableBME>
      <h2>SGP30</h2>
      <TableSGP></TableSGP>
      <h2>BH1750</h2>
      <TableBH></TableBH>
    </>
  )
}

export default App
