## MESS-RCOS DATA RECIEVER


This is a web server to receive data from the ESP 32 Mesh network over HTTP.
This assumes at least one node is connected to the same network as the device running this and that the esp32 is sending data in json form to this server (port 3000)

to start: 

``docker compose up --build`` 
