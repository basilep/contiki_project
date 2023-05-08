#include "contiki.h"
#include "net/netstack.h"
#include "net/nullnet/nullnet.h"

#include <string.h>
#include <stdio.h>

/* LOG CONFIGURATION */
#include "sys/log.h"
#define LOG_MODULE "App"
#define LOG_LEVEL LOG_LEVEL_INFO

/* CONFIGURATION */
#define SEND_INTERVAL (500 * CLOCK_SECOND)
static linkaddr_t null_addr = {{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};//TOCHANGE
static linkaddr_t first_addr = {{ 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};
static linkaddr_t second_addr  = {{ 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }};

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
    LOG_INFO("Received %u from ", data_receive);
    LOG_INFO_LLADDR(src);
    LOG_INFO_("\n");
  }
}

/* MAIN PART PROCESS CODE */
PROCESS_THREAD(node_example, ev, data)
{
  // Declare the timer
  static struct etimer timer;
  static unsigned data_to_send = 0;

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
      LOG_INFO("My data to send is %u\n", data_to_send);
      //LOG_INFO_LLADDR(&dest_addr);
      //LOG_INFO_("\n");
      if(linkaddr_cmp(&first_addr, &linkaddr_node_addr)){
        NETSTACK_NETWORK.output(&second_addr);  // Use to sent data to the destination
        data_to_send++;
      }
      else if(linkaddr_cmp(&second_addr, &linkaddr_node_addr)){
        NETSTACK_NETWORK.output(&first_addr);  // Use to sent data to the destination
        data_to_send=data_to_send+2;
      }
      else{
        LOG_INFO("I\'m not first or second node, but lets broadcast 0 to annoy everyone\n");
        NETSTACK_NETWORK.output(NULL);
      }
      etimer_reset(&timer);
    }
  }
  PROCESS_END();
}

