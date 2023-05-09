#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"

#include <string.h>
#include <stdio.h>

#include "sys/node-id.h"

/* LOG CONFIGURATION */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

/* OTHER CONFIGURATION */
#define SEND_INTERVAL (10 * CLOCK_SECOND)

static linkaddr_t null_addr = {{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};//TOCHANGE

static unsigned data_to_send = 10;  //TOCHANGE

static int in_network = 0; // Says if the node is already connected to the network ()

typedef struct neighboor
{
  linkaddr_t neighboor_addr;
}neighboor;

// Type of the different node
typedef enum {
  BORDER_ROUTER,
  COORDINATOR,
  SENSOR
} node_t; //_t to indicater the type of the node

typedef struct node {
  node_t type;
  linkaddr_t parent;
  linkaddr_t *children; // pointer to an array of linkaddr_t
  uint16_t nb_children;
} node;

void add_child(node *n, linkaddr_t child) {
  // Allocate memory for one additional child
  n->children = realloc(n->children, (n->nb_children + 1) * sizeof(linkaddr_t));   //-> is used to access the element of a struct through a pointer

  // Copy the new child's address into the new memory location
  linkaddr_copy(&n->children[n->nb_children], &child);

  // Increment the number of children
  n->nb_children++;
}

void remove_child(node *n, linkaddr_t child) {
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

//static neighboor neighboor_table;
static node my_node;
my_node.type = SENSOR;
my_node.nb_children = 0;

/* PROCESS CREATION */
PROCESS(node_example, "Node Example");
AUTOSTART_PROCESSES(&node_example);

/* CALL BACK FUNCTION WHEN MESSAGE IS RECEIVED*/
void input_callback(const void *data, uint16_t len,
  const linkaddr_t *src, const linkaddr_t *dest)
{
  if(len == sizeof(unsigned)) {
    unsigned data_receive;
    memcpy(&data_receive, data, sizeof(data_receive));
    if(data_receive == 0){
      LOG_INFO("Received a connection request from ");
      LOG_INFO_LLADDR(src);
      LOG_INFO_("\n");
      data_to_send   = 1;
      NETSTACK_NETWORK.output(src);
      linkaddr_copy(&(neighboor_table.neighboor_addr), src);
      //memcpy(&(routing_table.routes[routing_table.nb_route].next_hop_addr), src, sizeof(linkaddr_t));
      in_network = 1;
    }
    else if(data_receive == 1){
      LOG_INFO_LLADDR(src);
      LOG_INFO_(" has accepted my connection request\n");
      linkaddr_copy(&(neighboor_table.neighboor_addr), src);
      in_network = 1;
      }
    else{
      LOG_INFO("We are already connected, you send me %u\n", data_receive);
    }    
  }
}

/* MAIN PART PROCESS CODE */
PROCESS_THREAD(node_example, ev, data)
{
  // Declare the timer
  static struct etimer timer;

  PROCESS_BEGIN();

  /* Initialize NullNet */
  nullnet_buf = (uint8_t *)&data_to_send;
  nullnet_len = sizeof(data_to_send);
  nullnet_set_input_callback(input_callback);

  if(!linkaddr_cmp(&null_addr, &linkaddr_node_addr)) {    //&linkaddr_node_addr is the current node address
    LOG_INFO("The clock second is %lu\n", CLOCK_SECOND);
    etimer_set(&timer, SEND_INTERVAL);
    while(1) {
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));


    if(!in_network){
      // Not in the network at the moment -> broadcast a packet to know the neighboors
      LOG_INFO("Hi, I\'m the node %u would like to join the network, let's broadcast the signal 0\n", node_id); //node_id return the ID of the current node
      data_to_send = 0;
      NETSTACK_NETWORK.output(NULL);
    }

    else{
      LOG_INFO("My data to send is %u\n", data_to_send);
      NETSTACK_NETWORK.output(&(neighboor_table.neighboor_addr));  // Use to sent data to the destination
      data_to_send++;

    }
    etimer_reset(&timer);
    }
  }
  PROCESS_END();
}

