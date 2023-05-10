#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#include "sys/node-id.h"

/* LOG CONFIGURATION */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

/* OTHER CONFIGURATION */
#define SEND_INTERVAL (1 * CLOCK_SECOND)


// REALOC FROM https://github.com/kYc0o/kevoree-contiki/blob/master/realloc.c (Cooja didn't recognise the realloc)
#include "realloc.h"
size_t getsize(void *p)
{
	size_t *in = p;

	if(in)
	{
		--in; 
		return *in;
	}

	return -1;
}

void *realloc(void *ptr, size_t size)
{
	void *newptr;
	int msize;
	msize = getsize(ptr);

	if (size <= msize)
		return ptr;

	newptr = malloc(size);
	memcpy(newptr, ptr, msize);
	free(ptr);

	return newptr;
}
//-------------------------------------
//static linkaddr_t null_addr = {{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};//TOCHANGE

//static unsigned data_to_send = 10;  //TOCHANGE

static int in_network = 0; // Says if the node is already connected to the network ()

typedef enum {
  BORDER_ROUTER,
  COORDINATOR,
  SENSOR
} node_type_t;

typedef struct node {
  node_type_t type;
  linkaddr_t parent;
  linkaddr_t *children; // pointer to an array of linkaddr_t
  uint16_t nb_children;
} node_t;

typedef struct data_structure{
  uint8_t step_signal;
  union {
    uint8_t seconde_step;
    node_type_t node_type;
  } payload;
}data_structure_t;

static node_t my_node;
static data_structure_t data_to_send;
static int best_rssi = -100;
static int has_parent = 0; //TO DELETE OR CHANGE

void add_child(node_t *n, linkaddr_t child) {
  // Allocate memory for one additional child
  n->children = realloc(n->children, (n->nb_children + 1) * sizeof(linkaddr_t));   //-> is used to access the element of a struct through a pointer

  // Copy the new child's address into the new memory location
  linkaddr_copy(&n->children[n->nb_children], &child);

  // Increment the number of children
  n->nb_children++;
}

void remove_child(node_t *n, linkaddr_t child) {
  // Search for the index of the child in the array
  int i;
  for (i = 0; i < n->nb_children; i++) {
    if (linkaddr_cmp(&n->children[i], &child)) {
      break;
    }
  }

  // If the child was found in the array, remove it
  if (i < n->nb_children) {
    // Shift all elements after the child's index down by one
    memmove(&n->children[i], &n->children[i + 1], (n->nb_children - i - 1) * sizeof(linkaddr_t));

    // Deallocate the memory for the last element in the array
    n->children = realloc(n->children, (n->nb_children - 1) * sizeof(linkaddr_t));

    // Decrement the number of children
    n->nb_children--;
  }
}

int is_better_rssi(int rssi){ //check signal strenght
  // rssi need to be the actual best rssi
  if(rssi > packetbuf_attr(PACKETBUF_ATTR_RSSI)){
    return 1;
  }
  else{
    return 0;
  }
}

/* PROCESS CREATION */
PROCESS(node_example, "Node Example");
AUTOSTART_PROCESSES(&node_example);

/* CALL BACK FUNCTION WHEN MESSAGE IS RECEIVED*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
  data_structure_t *data_receive = (data_structure_t *) data; // Cast the data to data_structure_t

  if(data_receive->step_signal == 0){ // CONNECTION REQUEST
    LOG_INFO("Received a connection request from ");
    LOG_INFO_LLADDR(src);
    LOG_INFO_(" which is a %u node type\n", data_receive->payload.node_type);
    LOG_INFO_("Its rssi is %d : \n",packetbuf_attr(PACKETBUF_ATTR_RSSI));
    data_to_send.step_signal = 1; // CONNECTION RESPONSE
    add_child(&my_node, *src);
    NETSTACK_NETWORK.output(src);
  }
  else if(data_receive->step_signal == 1){ // CONNECTION RESPONSE
    if(best_rssi == -100){
      LOG_INFO_LLADDR(src);
      LOG_INFO_(" has accepted my connection request (rssi = %d)\n",packetbuf_attr(PACKETBUF_ATTR_RSSI));
      linkaddr_copy(&(my_node.parent), src);
      has_parent = 1;
      in_network = 1;
    }
    else if(is_better_rssi(best_rssi)){
        data_to_send.step_signal = 2; //Aware the current parent to change children
        NETSTACK_NETWORK.output(&(my_node.parent));
        LOG_INFO_LLADDR(src);
        LOG_INFO_(" has a better rssi so I change my parent (rssi = %d)\n",packetbuf_attr(PACKETBUF_ATTR_RSSI));
        linkaddr_copy(&(my_node.parent), src);  // change the parent
        has_parent = 1;
        in_network = 1;
    }
  }
  else if(data_receive->step_signal == 2){  // Changing children
    remove_child(&my_node, *src);
  }
  else if(my_node.nb_children>0){
    LOG_INFO_LLADDR(src);
    LOG_INFO_(" sent me the signal %u\n", data_receive->step_signal);
    LOG_INFO_("Its rssi is %d : \n",packetbuf_attr(PACKETBUF_ATTR_RSSI));
  }
} 


/* MAIN PART PROCESS CODE */
PROCESS_THREAD(node_example, ev, data)
{
  // declaration
  static struct etimer timer;
  my_node.type = SENSOR;
  data_to_send.payload.node_type = SENSOR;

  if(node_id == 1){
    NETSTACK_NETWORK.output(NULL);  // Needed to activate the antenna has he must do a broadcast first
    in_network = 1;
  }

  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&data_to_send;
  nullnet_len = sizeof(data_to_send); //PUT IT EVERYTIME IT CHANGES ??
  nullnet_set_input_callback(input_callback);




  etimer_set(&timer, SEND_INTERVAL);
  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
    if(!in_network){
      // Not in the network at the moment -> broadcast a packet to know the neighboors
      LOG_INFO("Hi, I\'m the node %u would like to join the network, let's broadcast the signal 0\n", node_id); //node_id return the ID of the current node
      LOG_INFO_("\t\tI'm the type %u of mote\n", my_node.type);
      data_to_send.step_signal = 0;
      data_to_send.payload.node_type = SENSOR;
      NETSTACK_NETWORK.output(NULL);
    }
    else{
      if(has_parent){
        data_to_send.step_signal = 150;
        LOG_INFO("I'm sending %u to my parent\n", data_to_send.step_signal);
        NETSTACK_NETWORK.output(&(my_node.parent));  // Use to sent data to the destination
      }
      if(my_node.nb_children > 0){
        data_to_send.step_signal = 100;
        LOG_INFO("I'm sending %u to my children\n", data_to_send.step_signal);
        for(int i=0; i<my_node.nb_children;i++){
          NETSTACK_NETWORK.output(&(my_node.children[i]));  // Use to sent data to the destination
        }
      }
      else{
        LOG_INFO("I'm alone on the network :(\n");
      }
    } 
    etimer_reset(&timer);
  }
  PROCESS_END();
}

