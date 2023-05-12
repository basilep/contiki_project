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
#define SEND_INTERVAL (2 * CLOCK_SECOND)


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
    uint16_t rssi;
    node_type_t node_type;
  } payload;
}data_structure_t;

static node_t my_node = { 
  .type = SENSOR, 
  .parent = {{0}}, // initialize all 8 bytes to 0
  .children = NULL, 
  .nb_children = 0 
};

static data_structure_t data_to_send;

static int has_parent = 0; //TO DELETE OR CHANGE
static int best_rssi = -100;

void add_child(node_t *n, linkaddr_t child) {
  if(n->nb_children == 0){
    n->children = (linkaddr_t *) malloc(sizeof(linkaddr_t)); 
  }
  else{
    // Allocate memory for one additional child
    n->children = realloc(n->children, (n->nb_children + 1) * sizeof(linkaddr_t));   //-> is used to access the element of a struct through a pointer
  }
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

/* Check if nodes from the routing table are still reachable*/
static void check_network(){
  if (NETSTACK_ROUTING.node_is_reachable(&dest_addr)) {
    
  }
  else {
    for(int i=0; i < my_node.nb_children; i++){
            LOG_INFO_LLADDR(&(my_node.children[i]));
            LOG_INFO_(" ; ");
            NETSTACK_NETWORK.output(&(my_node.children[i]));  // Use to sent data to the destination
          }
    // node is not reachable, do something else
  }
}

/* Function to check the better rssi
   between the current better  rssi and the last packet receive 
   (using packetbuf_attr(PACKETBUF_ATTR_RSSI))
   return 1 if the new rssi is better
*/
int is_better_rssi(){
  LOG_INFO_(" RSSI : best = %d, current = %d ; ", best_rssi, packetbuf_attr(PACKETBUF_ATTR_RSSI));
  if(best_rssi < packetbuf_attr(PACKETBUF_ATTR_RSSI)){
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
  linkaddr_t src_copy;  // Need to do a copy, to prevent problems if src is changing during the execution
  linkaddr_copy(&src_copy, src);
  data_structure_t *data_receive = (data_structure_t *) data; // Cast the data to data_structure_t
  if(linkaddr_cmp(src, &linkaddr_node_addr)){  //Check that the node doesn't get message from itself
    LOG_INFO("Same address\n");
  }
  else
  {
    if(data_receive->step_signal == 1 && !in_network){ // CONNECTION RESPONSE
      has_parent = 1;
      in_network = 1;
      LOG_INFO("SGN 1 (ACCEPTED) with rssi %d from ",packetbuf_attr(PACKETBUF_ATTR_RSSI));
      LOG_INFO_LLADDR(&src_copy);
      best_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
      linkaddr_copy(&(my_node.parent), &src_copy);  //Save the parent address
      LOG_INFO_(" ; SGN 2 (ack) sent to ");
      LOG_INFO_LLADDR(&(my_node.parent));
      LOG_INFO_("\n");
      data_to_send.step_signal = 2; // Send an ACK to the connection
      NETSTACK_NETWORK.output(&(my_node.parent));
    }
    else if(in_network){
      if(data_receive->step_signal == 0){ // CONNECTION REQUEST
        LOG_INFO("SGN 0 (connexion request) received from ");
        LOG_INFO_LLADDR(&src_copy);
        data_to_send.step_signal = 1; // Send a connection response
        LOG_INFO_(" ; SGN 1 (connexion response) send to ");
        LOG_INFO_LLADDR(&src_copy);
        LOG_INFO_("\n");
        NETSTACK_NETWORK.output(&src_copy);
      }
      else if(data_receive->step_signal == 1 && has_parent){
        LOG_INFO("SGN 1 received from ");      
        LOG_INFO_LLADDR(&src_copy);
        LOG_INFO_(" ; let's check rssi ;");
        if(is_better_rssi()){
          data_to_send.step_signal = 3; // aware the parent the he found a new better node, to delete it from its list
          LOG_INFO_(" SEND SGN 3 to ");
          LOG_INFO_LLADDR(&(my_node.parent));
          LOG_INFO_("\n");
          NETSTACK_NETWORK.output(&(my_node.parent));
          LOG_INFO_LLADDR(&src_copy);
          LOG_INFO_(" has a better rssi, he will be now my parent ; ");
          best_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
          linkaddr_copy(&(my_node.parent), &src_copy);
          data_to_send.step_signal = 2; // Send an ACK to the connection
          LOG_INFO_("SGN 2 (ack) sent to ");
          LOG_INFO_LLADDR(&(my_node.parent));
          LOG_INFO_("\n");
          NETSTACK_NETWORK.output(&(my_node.parent));
        }
        else if(linkaddr_cmp(&(my_node.parent), &src_copy)){
          LOG_INFO_LLADDR(&src_copy);
          LOG_INFO_(" is already my parent\n");
        }
        else{
          LOG_INFO_LLADDR(&src_copy);
          LOG_INFO_(" has not a best rssi\n");
        }
      }
      else if(data_receive->step_signal == 2){  // ACKNOWLEDGE CONNECTION
        LOG_INFO("SGN 2 (ACK) received from ");
        LOG_INFO_LLADDR(&src_copy);
        LOG_INFO_(" which is now my child\n");
        add_child(&my_node, src_copy);  //add the child to the list of children
      }
      else if(data_receive->step_signal == 3){  // REMOVE CHILDREN
        LOG_INFO("RECEIVED CHILD TO REMOVE from ");
        LOG_INFO_LLADDR(src);
        LOG_INFO_("\n");
        remove_child(&my_node, src_copy);
      }
      else if(data_receive->step_signal> 50){
        LOG_INFO(" ");
        LOG_INFO_LLADDR(&src_copy);
        LOG_INFO_(" sent me the signal %u and ", data_receive->step_signal);
        LOG_INFO_("its rssi is %d : \n",packetbuf_attr(PACKETBUF_ATTR_RSSI));
      }
    }
  }
} 


/* MAIN PART PROCESS CODE */
PROCESS_THREAD(node_example, ev, data)
{
  // declaration
  static struct etimer timer;
  data_to_send.payload.node_type = SENSOR;
  
  if(node_id == 1 && !in_network){
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
      LOG_INFO("Node %u broadcasts SGN 0\n", node_id); //node_id return the ID of the current node
      //LOG_INFO_("\t\tI'm the type %u of mote\n", my_node.type);
      data_to_send.step_signal = 0;
      NETSTACK_NETWORK.output(NULL);
    }
    else{
      if(has_parent){
        data_to_send.step_signal = 100;
        LOG_INFO("I'm sending %u to my parent ", data_to_send.step_signal);
        LOG_INFO_LLADDR(&(my_node.parent));
        LOG_INFO_("\n");
        NETSTACK_NETWORK.output(&(my_node.parent));  // Use to sent data to the destination
      }
      if(my_node.nb_children > 0){
        data_to_send.step_signal = 150;
        LOG_INFO("I have %u children\n", my_node.nb_children);
        LOG_INFO("I'm sending %u to my children ", data_to_send.step_signal);
        for(int i=0; i < my_node.nb_children; i++){
          LOG_INFO_LLADDR(&(my_node.children[i]));
          LOG_INFO_(" ; ");
          NETSTACK_NETWORK.output(&(my_node.children[i]));  // Use to sent data to the destination
        }
        LOG_INFO_("\n");
      }
    }
    etimer_reset(&timer);
  }
  PROCESS_END();
}

// FULL LOG
/*
LOG_INFO("Parent: ");
LOG_INFO_LLADDR(&(my_node.parent));
LOG_INFO("\n\t\tChildren: ");
for(int i=0; i < my_node.nb_children; i++){
  LOG_INFO_LLADDR(&(my_node.children[i]));
  LOG_INFO_(" ; ");
  }
LOG_INFO_("\n")
*/