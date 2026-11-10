## MESS-RCOS DATA RECIEVER


This is a web server to receive data from the ESP 32 Mesh network over HTTP.
This assumes at least one node is connected to the same network as the device running this and that the esp32 is sending data in json form to this server (port 3000)

to start: 

``docker compose up --build`` 

Services:

port 5432 PG serer
port 3000 web frontend
port 3838 shiny server


To seed the DB run random_input,py in db/db_test/ it will add 10 fake records per sensor per run for debug.
