# Note of some contiki functionnality


## Nullnet
We use nullnet and get inspired by the unicast and broadcast nullnet example, here are some information about how it is used.

### Sending data
We created a *nullnet_buf* and *nullnet_len* where we can store data
We then can send data using:

#### Unicast
***NETSTACK_NETWORK.output(&dest_addr)*** where *dest_addr* is the IP destination address of the mote we want to contact

#### Broadcast
Simply put NULL as parameter to broadcast
***NETSTACK_NETWORK.output(NULL)***

### Receive data
We can set an *inpute callback* function using
***nullnet_set_input_callback(input_callback);***
Which will be called everytime the node receive a data


## LOG
We can use *LOG_INFO()* like a printf to log things inside cooja
We can use *LOG_INFO_LLADDR()* to log a *linkaddr_t*


## Other
Do not forget to update the makefile if needed


