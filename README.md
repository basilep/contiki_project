# Mobile and Embedded Computing 2022-2023

Contiki project simulated in Cooja

## Run the project
To run the project correctly, you need to launch cooja

Create 3 types of motes (Using **Z1** mote):
- A border router (as the root) with the *border_router.c* code
- Some coordinators (from 1 to 4) with the *coordinator.c* code (in the range of the border router)
- Some sensors (from 1 to 4 per coordinator) with the *sensor.c* code, sensors can have other sensors as children

Once this is done, you can start the serial socket (SERVER) of the border router

You can execute the python server with this command:
***python3 ./server.py --ip ip_addr --port port_nb***
Where *ip_addr* is the ip_addr of the container, to find it:
    Use ***docker ps*** to retrieve the name of the container where the simulation is executed
    Then ***docker container inspect <container-name>*** to retrieve the ip of the container
And *port_nb* is the port of the serial socket of the border router 

In my case, the command was ***python3 ./server.py --ip 172.17.0.2 --port 60001***

Then you can launch the simulation!