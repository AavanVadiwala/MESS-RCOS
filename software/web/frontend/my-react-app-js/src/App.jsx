import { useState } from 'react'
import './App.css'
import Table from "./components/Table";
import TableSGP from './components/TableSGP';
import TableBH from './components/TableBH';

function App() {

  return (
    <>
      <h2>BME280</h2>
      <Table></Table>
      <h2>SGP30</h2>
      <TableSGP></TableSGP>
      <h2>BH1750</h2>
      <TableBH></TableBH>
      

      
    </>
  )
}

export default App
