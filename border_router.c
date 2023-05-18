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
#define BERKELEY_INTERVAL (4.9 * CLOCK_SECOND)

//-------------------------------------

static int in_network = 0; // Says if the node is already connected to the network ()

typedef struct node {
  linkaddr_t *children; // pointer to an array of linkaddr_t
  int *child_reach_count; // number or round, the children didn't anwser (to know if they are still reachable)
  uint16_t nb_children;
} node_t;

typedef struct data_structure{
  uint8_t step_signal;
  int node_rank;
  clock_time_t clock;
}data_structure_t;

static node_t my_node = { 
  .children = NULL,
  .child_reach_count = NULL,
  .nb_children = 0 
};

static data_structure_t data_to_send ={
  .node_rank = 0,
  .clock = 0
};

static struct ctimer timer;
static struct ctimer check_network_timer;
static struct ctimer berkeley_timer;
static int clock_compensation = 0;
static clock_time_t *clock_array;
static size_t clock_array_size = 0;

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
  n->child_reach_count[n->nb_children] = 0;

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

long int handle_clock(clock_time_t received_clock){
  // initialize the memory for the array if first element
  // update array size 

  //add clock to array
  // if lengh(array) == nb_children
      // compute difference to each element in array
      // compute average of differences
      // put average in clock_compensation
      // Border update its own clock with the difference (compensation)

      // free the array memory
  
  // return clock

  if(clock_array_size == 0){
    clock_array = (clock_time_t*) malloc(sizeof(clock_time_t));
  }
  else{
    clock_array = realloc(clock_array, (clock_array_size+1)*sizeof(clock_time_t));
  }
  clock_array[clock_array_size] = received_clock;
  clock_array_size+=1;

  if(clock_array_size == my_node.nb_children){
    long int num = 0;
  
    for(int i=0; i<clock_array_size; i++){
      num += (clock_time() - clock_array[i]);
      LOG_INFO_("Ceci est le num : %ld\n", num);
    }
    clock_compensation = num / clock_array_size;

    long int synchronized_clock = clock_time() - clock_compensation; 

    free(clock_array);
    clock_array_size = 0;
    
    return synchronized_clock;
  }

  return 0;
}

/* PROCESS CREATION */
PROCESS(border_router_process, "Border Router");
AUTOSTART_PROCESSES(&border_router_process);

/* CALL BACK FUNCTION WHEN MESSAGE IS RECEIVED*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
    linkaddr_t src_copy;  // Need to do a copy, to prevent problems if src is changing during the execution
    linkaddr_copy(&src_copy, src);
    data_structure_t *data_receive = (data_structure_t *) data; // Cast the data to data_structure_t
    if(data_receive->step_signal == 0 && data_receive->node_rank == 1){ // CONNECTION REQUEST from coordinators
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
    else if(data_receive->step_signal == 5){
        for (int i = 0; i < my_node.nb_children; i++) {
            if (linkaddr_cmp(&my_node.children[i], &src_copy)) { //Get the children
                my_node.child_reach_count[i] = 0;  //reset its count
                break;
            }
        }
    }
    else if(data_receive->step_signal == 7){
      LOG_INFO("RECEIVED CLOCK RESPONSE FROM ");
      LOG_INFO_LLADDR(&src_copy);
      LOG_INFO_(" where clock is %lu", data_receive->clock);
      LOG_INFO_(" and my clock is %lu", clock_time());
      LOG_INFO_("\n");

      // function handle_clock()
      long int synchronized_clock = handle_clock(data_receive->clock);
      if (synchronized_clock != 0){
        data_to_send.clock = synchronized_clock;
        for (int i = 0; i < my_node.nb_children; i++) {
          data_to_send.step_signal = 8;
          NETSTACK_NETWORK.output(&(my_node.children[i]));  // Use to sent data to the destination
        }

      }
    }
    else if(data_receive->step_signal> 50){
        LOG_INFO(" ");
        LOG_INFO_LLADDR(&src_copy);
        LOG_INFO_(" sent me the signal %u and ", data_receive->step_signal);
        LOG_INFO_("its rssi is %d : \n",packetbuf_attr(PACKETBUF_ATTR_RSSI));
    }
}

/**/
static void send_clock_request(void* ptr){
  ctimer_reset(&berkeley_timer);

  data_to_send.step_signal = 6;
  LOG_INFO("I'm sending clock request %u to my children : ", data_to_send.step_signal);
  for (int i = 0; i < my_node.nb_children; i++) {
    LOG_INFO_LLADDR(&(my_node.children[i]));
    LOG_INFO_(" ; ");
    NETSTACK_NETWORK.output(&(my_node.children[i]));  // Use to sent data to the destination
  }
  LOG_INFO_("\n");
}

/* CALLBACK TO CHECK REACHABLE CHILDREN */
static void send_reachable_state(void* ptr){
  ctimer_reset(&check_network_timer);
  for (int i = 0; i < my_node.nb_children; i++) {
    if(my_node.child_reach_count[i]>1){
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

/* MAIN PART PROCESS CODE */
PROCESS_THREAD(border_router_process, ev, data)
{
  
  if(!in_network){
    NETSTACK_NETWORK.output(NULL);  // Needed to activate the antenna has he must do a broadcast first
    in_network = 1;
  }

  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&data_to_send;
  nullnet_len = sizeof(data_structure_t);
  nullnet_set_input_callback(input_callback);

  ctimer_set(&timer, SEND_INTERVAL, timer_callback, NULL);
  ctimer_set(&check_network_timer, CHECK_NETWORK, send_reachable_state, NULL);
  ctimer_set(&berkeley_timer, BERKELEY_INTERVAL, send_clock_request , NULL);
  while (1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}