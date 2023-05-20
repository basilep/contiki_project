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
#define BERKELEY_INTERVAL (5 * CLOCK_SECOND)
#define TIME_WINDOW (2 * CLOCK_SECOND)

//-------------------------------------

static int in_network = 0; // Says if the node is already connected to the network ()

// ROUTING TABLE
typedef struct node {
  linkaddr_t *children;
  uint16_t nb_children;
} node_t;

typedef struct data_structure{
  uint8_t step_signal;
  int node_rank;
  uint8_t data [2];
  clock_time_t clock;
  clock_time_t timeslot_array [2];
}data_structure_t;

static node_t my_node = { 
  .children = NULL,
  .nb_children = 0 
};

static data_structure_t data_to_send ={
  .node_rank = 0,
  .clock = 0
};

static struct ctimer berkeley_timer;
static int clock_compensation = 0;
static clock_time_t *clock_array;
static size_t clock_array_size = 0;

void add_child(node_t *n, linkaddr_t child) {
  if(n->nb_children == 0){
    n->children = (linkaddr_t *) malloc(sizeof(linkaddr_t));
  }
  else{
    // Allocate memory for one additional child
    n->children = realloc(n->children, (n->nb_children + 1) * sizeof(linkaddr_t));
  }
  // Copy the new child's address into the new memory location
  linkaddr_copy(&n->children[n->nb_children], &child);
  n->nb_children++;
}

long int handle_clock(clock_time_t received_clock){
  
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
    }
    clock_compensation = num / clock_array_size;

    long int synchronized_clock = clock_time() - clock_compensation; 

    free(clock_array);
    clock_array_size = 0;
    
    return synchronized_clock;
  }

  return 0;
}

void timeslots_allocation(){
 if(my_node.nb_children > 0){
    int timeslot = TIME_WINDOW/my_node.nb_children;

    for (int i=0; i< my_node.nb_children; i++){
      clock_time_t synchronized_clock = clock_time() + clock_compensation;
      data_to_send.timeslot_array[0] = synchronized_clock + i*timeslot + TIME_WINDOW/20;  // TIME_WINDOW/20 is a guardtime
      data_to_send.timeslot_array[1] = synchronized_clock + (i+1)*timeslot;
      data_to_send.step_signal = 9;
      NETSTACK_NETWORK.output(&(my_node.children[i]));
    }
  }
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
    else if(data_receive->step_signal == 7){  //MANAGE CLOCK BERKELEY
      long int synchronized_clock = handle_clock(data_receive->clock);
      if (synchronized_clock != 0){
        data_to_send.clock = synchronized_clock;
        for (int i = 0; i < my_node.nb_children; i++) {
          data_to_send.step_signal = 8;
          NETSTACK_NETWORK.output(&(my_node.children[i]));
        }
      }
      timeslots_allocation();
    }
    else if(data_receive->step_signal == 12){
      data_to_send.data[0] = data_receive->data[0];
      data_to_send.data[1] = data_receive->data[1];
      LOG_INFO("RECEIVE DATA FROM NODE %d : %d\n", data_to_send.data[0], data_to_send.data[1]);
    }
}

static void send_clock_request(void* ptr){
  ctimer_reset(&berkeley_timer);

  data_to_send.step_signal = 6;
  //LOG_INFO("I'm sending clock request %u to my children : ", data_to_send.step_signal);
  for (int i = 0; i < my_node.nb_children; i++) {
    //LOG_INFO_LLADDR(&(my_node.children[i]));
    //LOG_INFO_(" ; ");
    NETSTACK_NETWORK.output(&(my_node.children[i]));  // Use to sent data to the destination
  }
  //LOG_INFO_("\n");
}

/* MAIN PART PROCESS CODE */
PROCESS_THREAD(border_router_process, ev, data)
{
  
  if(!in_network){
    NETSTACK_NETWORK.output(NULL);  // Needed to activate the antenna has it must do a broadcast first before any communication
    in_network = 1;
  }

  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&data_to_send;
  nullnet_len = sizeof(data_structure_t);
  nullnet_set_input_callback(input_callback);

  ctimer_set(&berkeley_timer, BERKELEY_INTERVAL, send_clock_request , NULL);
  while (1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}