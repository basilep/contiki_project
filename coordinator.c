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

/* OTHER CONFIGURATION */
#define SEND_INTERVAL (2 * CLOCK_SECOND)
#define CHECK_NETWORK (5 * CLOCK_SECOND)

//-------------------------------------

static int in_network = 0; // Says if the node is already connected to the network ()

typedef enum {
  BORDER_ROUTER,
  COORDINATOR,
  SENSOR
} node_type_t;

typedef struct node {
  linkaddr_t parent;
  int parent_reach_count; // number or round, the parent didn't anwser (to know if it still reachable)
  linkaddr_t *children; // pointer to an array of linkaddr_t
  int *child_reach_count; // same as the parent_reach_count but for children
  uint16_t nb_children;
} node_t;

typedef struct data_structure{
  uint8_t step_signal;
  int node_rank;
  node_type_t node_type;
  clock_time_t clock;
}data_structure_t;

static node_t my_node = { 
  .parent = {{0}}, // initialize all 8 bytes to 0
  .parent_reach_count = 0,
  .children = NULL,
  .child_reach_count = NULL,
  .nb_children = 0 
};

static data_structure_t data_to_send ={
  .node_type = COORDINATOR,
  .node_rank = 1,
  .clock = 0
};

static struct ctimer timer;
static struct ctimer check_network_timer;
static int best_rssi = -100;

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
  if(data_receive->step_signal == 1 && !in_network){ // CONNECTION RESPONSE
    if(data_receive->node_rank == 0){ //Check that it's well the biorder router
      in_network = 1;
      LOG_INFO("SGN 1 (ACCEPTED) with rssi %d from ",packetbuf_attr(PACKETBUF_ATTR_RSSI));
      LOG_INFO_LLADDR(&src_copy);
      best_rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
      linkaddr_copy(&(my_node.parent), &src_copy);  //Save the parent address
      LOG_INFO_(" rank: %d ; SGN 2 (ack) sent to ", data_to_send.node_rank);
      LOG_INFO_LLADDR(&(my_node.parent));
      LOG_INFO_("\n");
      data_to_send.step_signal = 2; // Send an ACK to the connection
      NETSTACK_NETWORK.output(&(my_node.parent));
    }
  }
  else if(in_network){
    if(data_receive->step_signal == 0 && data_receive->node_rank != 1){ // CONNECTION REQUEST from sensors
      LOG_INFO("SGN 0 (connexion request) received from ");
      LOG_INFO_LLADDR(&src_copy);
      data_to_send.step_signal = 1; // Send a connection response
      LOG_INFO_(" ; SGN 1 (connexion response) send to ");
      LOG_INFO_LLADDR(&src_copy);
      LOG_INFO_("\n");
      NETSTACK_NETWORK.output(&src_copy);
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
    else if(data_receive->step_signal == 4){
      data_to_send.step_signal = 5;
      NETSTACK_NETWORK.output(&src_copy);
    }
    else if(data_receive->step_signal == 5){
      if (linkaddr_cmp(&my_node.parent, &src_copy)) {
        my_node.parent_reach_count=0;
      }   
      for (int i = 0; i < my_node.nb_children; i++) {
        if (linkaddr_cmp(&my_node.children[i], &src_copy)) { //Get the children
          my_node.child_reach_count[i] = 0;  //reset its count
          break;
        }
      }
    }
    else if(data_receive->step_signal == 6){
      LOG_INFO("RECEIVED CLOCK REQUEST FROM ");
      LOG_INFO_LLADDR(&src_copy);
      

      data_to_send.clock = clock_time(); // get its own clock
      data_to_send.step_signal = 7;
      //TODO: changer le step_signal à envoyer à 7
      LOG_INFO_(" ; My clock is %lu", data_to_send.clock);
      LOG_INFO_("\n");
      NETSTACK_NETWORK.output(&src_copy);

    }
    else if(data_receive->step_signal> 50){
      LOG_INFO(" ");
      LOG_INFO_LLADDR(&src_copy);
      LOG_INFO_(" sent me the signal %u and ", data_receive->step_signal);
      LOG_INFO_("its rssi is %d : \n",packetbuf_attr(PACKETBUF_ATTR_RSSI));
    }
  }
} 

/* CALLBACK TO CHECK REACHABLE NODES */
static void send_reachable_state(void* ptr){
  ctimer_reset(&check_network_timer);
  if(!linkaddr_cmp(&(my_node.parent), &linkaddr_null)){ // Check If there is a parent
    if(my_node.parent_reach_count>=1){
      LOG_INFO_("Parent not reachable anymore : ");
      LOG_INFO_LLADDR(&my_node.parent);
      LOG_INFO_("; temporary removal\n");
      linkaddr_copy(&my_node.parent, &linkaddr_null); //setting parent to the null address
      my_node.parent_reach_count = -1;  //So it goes to 0 after the next increment
      in_network = 0;
      best_rssi=-100;
    }
    else{
      data_to_send.step_signal = 4; //Aware that it's still reachable
      NETSTACK_NETWORK.output(&my_node.parent);
    }
    my_node.parent_reach_count+=1;
  }
  for (int i = 0; i < my_node.nb_children; i++) {
    if(my_node.child_reach_count[i]>=1){
      LOG_INFO_(" child : ");
      LOG_INFO_LLADDR(&my_node.children[i]);
      LOG_INFO_("not reachable anymore\n");
      remove_child(&my_node, my_node.children[i]);
    }
    else{
      data_to_send.step_signal = 4; //Aware that it's still reachable
      NETSTACK_NETWORK.output(&my_node.children[i]);
    }
    my_node.child_reach_count[i] +=1;
  }
}

/* TIMER CALLBACK MAIN FUNCTION */
void timer_callback(void* ptr){
  ctimer_reset(&timer);
  if(!in_network){
    // Not in the network at the moment -> broadcast a packet to know the neighboors
    LOG_INFO("Node %u broadcasts SGN 0\n", node_id); //node_id return the ID of the current node
    //LOG_INFO_("\t\tI'm the type %u of mote\n", my_node.type);
    data_to_send.step_signal = 0;
    NETSTACK_NETWORK.output(NULL);
  }
  else{
    if(!linkaddr_cmp(&(my_node.parent), &linkaddr_null)){
      data_to_send.step_signal = 100;
      LOG_INFO("I'm sending %u to my parent ", data_to_send.step_signal);
      LOG_INFO_LLADDR(&(my_node.parent));
      LOG_INFO_("\n");
      data_to_send.step_signal = 100; //DEBUG
      NETSTACK_NETWORK.output(&(my_node.parent));  // Use to sent data to the destination
    }
    if(my_node.nb_children > 0){        
      data_to_send.step_signal = 150;
      LOG_INFO("I have %u children\n", my_node.nb_children);
      LOG_INFO("I'm sending %u to my children ", data_to_send.step_signal);
      for(int i=0; i < my_node.nb_children; i++){
        LOG_INFO_LLADDR(&(my_node.children[i]));
        LOG_INFO_(" ; child");
        NETSTACK_NETWORK.output(&(my_node.children[i]));  // Use to sent data to the destination
      }
      LOG_INFO_("\n");
    }
  }
}

/* MAIN PART PROCESS CODE */
PROCESS_THREAD(node_example, ev, data)
{
  
  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&data_to_send;
  nullnet_len = sizeof(data_to_send); //PUT IT EVERYTIME IT CHANGES ??
  nullnet_set_input_callback(input_callback);

  ctimer_set(&timer, SEND_INTERVAL, timer_callback, NULL);
  ctimer_set(&check_network_timer, CHECK_NETWORK, send_reachable_state, NULL);
  while (1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}