#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"
#include "net/packetbuf.h"
#include "realloc.h"

#include "sys/clock.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#include "sys/node-id.h"

/* LOG CONFIGURATION */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO
// You can change the level of log to LOG_LEVEL_DBG to see everything

/* OTHER CONFIGURATION */
#define SEND_INTERVAL (2 * CLOCK_SECOND)
#define CHECK_NETWORK (5 * CLOCK_SECOND)
#define TIME_WINDOW (2 * CLOCK_SECOND)

//-------------------------------------

static int in_network = 0; // Says if the node is already connected to the network ()

// ROUTING TABLE
typedef struct node {
  linkaddr_t parent;  
  linkaddr_t *children; 
  int *child_reach_count; // number or round, the children didn't anwser (to know if they are still available)
  uint16_t nb_children;
} node_t;

typedef struct data_structure{
  uint8_t step_signal;
  int node_rank;
  uint8_t data[2];
  clock_time_t clock;
  clock_time_t timeslot_array [2];
}data_structure_t;

static node_t my_node = { 
  .parent = {{0}}, // initialize all 8 bytes to 0
  .children = NULL,
  .child_reach_count = NULL,
  .nb_children = 0 
};

static data_structure_t data_to_send ={
  .node_rank = 1,
  .clock = 0,
  .timeslot_array = {0, 0}
};

static struct ctimer timer;
static struct ctimer check_network_timer;
static struct ctimer get_sensor_data_timer;
static int clock_compensation = 0;

void add_child(node_t *n, linkaddr_t child) {
  if(n->nb_children == 0){
    n->children = (linkaddr_t *) malloc(sizeof(linkaddr_t));
    n->child_reach_count = (int*) malloc(sizeof(int));
  }
  else{
    // Allocate memory for one additional child
    n->children = realloc(n->children, (n->nb_children + 1) * sizeof(linkaddr_t));   //-> is used to access the element of a struct through a pointer
    n->child_reach_count = realloc(n->child_reach_count, (n->nb_children+1)*sizeof(int));
  }
  // Copy the new child's address into the new memory location
  linkaddr_copy(&n->children[n->nb_children], &child);
  n->child_reach_count[n->nb_children] = -1;

  // Increment the number of children
  n->nb_children++;
}

void remove_child(node_t *n, linkaddr_t child) {  //Maybe free it if no children anymore?
  // Search for the index of the child in the array
  int i;
  for (i = 0; i < n->nb_children; i++) {
    if (linkaddr_cmp(&n->children[i], &child)) {
      break;
    }
  }

  // If the child was found in the array, remove it
  if (i < n->nb_children) {
    if (i < n->nb_children -1) { //If it's not the last of the array
      // Shift all elements after the child's index down by one
      memmove(&n->children[i], &n->children[i + 1], (n->nb_children - i - 1) * sizeof(linkaddr_t));
      memmove(&n->child_reach_count[i], &n->child_reach_count[i + 1], (n->nb_children - i - 1) * sizeof(uint8_t));
    }
    // Deallocate the memory for the last element in the array
    n->children = realloc(n->children, (n->nb_children - 1) * sizeof(linkaddr_t));
    n->child_reach_count = realloc(n->child_reach_count, (n->nb_children - 1) * sizeof(uint8_t));
    // Decrement the number of children
    n->nb_children--;
  }
}

/* PROCESS CREATION */
PROCESS(coordinator_process, "Coordinator node");
AUTOSTART_PROCESSES(&coordinator_process);

/* CALL BACK FUNCTION WHEN MESSAGE IS RECEIVED*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
  linkaddr_t src_copy;  // Need to do a copy, to prevent problems if src is changing during the execution
  linkaddr_copy(&src_copy, src);
  data_structure_t *data_receive = (data_structure_t *) data; // Cast the data to data_structure_t
  if(data_receive->step_signal == 1 && !in_network && data_receive->node_rank == 0){ // CONNECTION RESPONSE
      in_network = 1;
      LOG_DBG("SGN 1 (ACCEPTED) with rssi %d from ",packetbuf_attr(PACKETBUF_ATTR_RSSI));
      LOG_DBG_LLADDR(&src_copy);
      linkaddr_copy(&(my_node.parent), &src_copy);  //Save the parent address
      LOG_DBG_(" rank: %d ; SGN 2 (ack) sent to ", data_to_send.node_rank);
      LOG_DBG_LLADDR(&(my_node.parent));
      LOG_DBG_("\n");
      data_to_send.step_signal = 2; // Send an ACK to the connection
      NETSTACK_NETWORK.output(&(my_node.parent));
  }
  else if(in_network){
    if(data_receive->step_signal == 0 && data_receive->node_rank != 1){ // CONNECTION REQUEST from sensors
      LOG_DBG("SGN 0 (connexion request) received from ");
      LOG_DBG_LLADDR(&src_copy);
      data_to_send.step_signal = 1; // Send a connection response
      LOG_DBG_(" ; SGN 1 (connexion response) send to ");
      LOG_DBG_LLADDR(&src_copy);
      LOG_DBG_("\n");
      NETSTACK_NETWORK.output(&src_copy);
    }
    else if(data_receive->step_signal == 2){  // ACKNOWLEDGE CONNECTION
      LOG_DBG("SGN 2 (ACK) received from ");
      LOG_DBG_LLADDR(&src_copy);
      LOG_DBG_(" which is now my child\n");
      add_child(&my_node, src_copy);  //add the child to the list of children
    }
    else if(data_receive->step_signal == 3){  // REMOVE CHILDREN
      LOG_DBG("RECEIVED CHILD TO REMOVE from ");
      LOG_DBG_LLADDR(src);
      LOG_DBG_("\n");
      remove_child(&my_node, src_copy);
    }
    else if(data_receive->step_signal == 4){
      data_to_send.step_signal = 5;
      NETSTACK_NETWORK.output(&src_copy);
    }
    else if(data_receive->step_signal == 5){
      for (int i = 0; i < my_node.nb_children; i++) {
        if (linkaddr_cmp(&my_node.children[i], &src_copy)) { //Get the children
          my_node.child_reach_count[i] = 0;  //reset its count
          break;
        }
      }
    }
    else if(data_receive->step_signal == 6){
      LOG_DBG("RECEIVED CLOCK REQUEST FROM ");
      LOG_DBG_LLADDR(&src_copy);
      
      data_to_send.clock = (clock_time_t)((long int) clock_time() + clock_compensation); // get its own clock
      data_to_send.step_signal = 7;

      LOG_DBG_(" ; My clock is %lu", data_to_send.clock);
      LOG_DBG_("\n");
      NETSTACK_NETWORK.output(&src_copy);

    }
    else if(data_receive->step_signal == 8){
      LOG_DBG("RECEIVED NEW SYNCHRONIZED CLOCK");
      clock_compensation = data_receive->clock - clock_time();
      LOG_DBG_(" : %d", clock_compensation);
      LOG_DBG_(" ; New clock: %lu\n", data_receive->clock);
    }
    else if(data_receive->step_signal == 9){
      LOG_DBG("RECEIVED TIMESLOT");
      data_to_send.timeslot_array[0] = data_receive->timeslot_array[0];
      data_to_send.timeslot_array[1] = data_receive->timeslot_array[1];
      LOG_DBG("Timseslots : 1) %lu ; 2) %lu : \n", data_to_send.timeslot_array[0],data_to_send.timeslot_array[1]);
    }
    else if(data_receive->step_signal == 12){
      data_to_send.data[0] = data_receive->data[0];
      data_to_send.data[1] = data_receive->data[1];
      LOG_DBG("RECEIVE DATA FROM NODE %d : %d\n", data_to_send.data[0], data_to_send.data[1]);
      data_to_send.step_signal = 12;
      NETSTACK_NETWORK.output(&(my_node.parent));
    }
  }
} 

static void get_sensor_data(void* ptr){
  ctimer_reset(&get_sensor_data_timer);
  if(data_to_send.timeslot_array[1] != 0){
    //Check if the current "synchronised" clock is in the allocated time slot
    if((clock_time() + clock_compensation)>data_to_send.timeslot_array[0] && (clock_time() + clock_compensation)<data_to_send.timeslot_array[1]){
      for(int i=0; i < my_node.nb_children; i++){ //Notify the children to send data if they have any
        data_to_send.step_signal = 11;
        NETSTACK_NETWORK.output(&(my_node.children[i]));
      }
    }
    else if((clock_time() + clock_compensation)>data_to_send.timeslot_array[1]){  //If the timeslot is already passed, addition it to the time windows
      data_to_send.timeslot_array[0]+=TIME_WINDOW;
      data_to_send.timeslot_array[1]+=TIME_WINDOW;
    }
  }
}

/* CALLBACK TO CHECK REACHABLE NODES */
static void get_node_availability(void* ptr){
  ctimer_reset(&check_network_timer);
  for (int i = 0; i < my_node.nb_children; i++) {
    if(my_node.child_reach_count[i]>=1){
      LOG_DBG_("Child : ");
      LOG_DBG_LLADDR(&(my_node.children[i]));
      LOG_DBG_("not reachable anymore\n");
      remove_child(&my_node, my_node.children[i]);
    }
    else{
      data_to_send.step_signal = 4; //Aware that it's still reachable
      NETSTACK_NETWORK.output(&(my_node.children[i]));
    }
    my_node.child_reach_count[i] +=1;
  }
}

/* CONNECTION TO NETWORK */
void get_in_network(void* ptr){
  if(!in_network){
    ctimer_reset(&timer);
    // Not in the network at the moment -> broadcast a packet to know the neighboors
    LOG_DBG("Node %u broadcasts SGN 0\n", node_id);
    data_to_send.step_signal = 0;
    NETSTACK_NETWORK.output(NULL);
  }
}


/* MAIN PART PROCESS CODE */
PROCESS_THREAD(coordinator_process, ev, data)
{
  
  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&data_to_send;
  nullnet_len = sizeof(data_structure_t);
  nullnet_set_input_callback(input_callback);

  ctimer_set(&timer, SEND_INTERVAL, get_in_network, NULL);
  ctimer_set(&check_network_timer, CHECK_NETWORK, get_node_availability, NULL);
  ctimer_set(&get_sensor_data_timer, TIME_WINDOW/10, get_sensor_data, NULL);

  while (1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}