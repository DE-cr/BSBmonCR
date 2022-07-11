// Add custom code for setup function here which will be included at the end of the global section

/*
 * BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
 * using esp32 with (optional) ssd1306 display
 * (file BSB_LAN_custom_global.h)
 */

//- config:
#define UDP_PORT 28000
#include "BSBmonCR_config.h"
IPAddress broadcast_ip = IPAddress( NETWORK, 255 );
int udp_bsb_params[] = {
  OUTSIDE_TEMPERATURE,
  ROOMS_TEMPERATURE,
  WATER_TEMPERATURE,
  HEATING_STATUS,
  WATER_STATUS,
  BOILER_STATUS,
};

//- other:
#define N_UDP_BSB_PARAMS ( sizeof( udp_bsb_params ) / sizeof( *udp_bsb_params ) )
int i_udp_bsb_param = 0;
unsigned long prev_ms = 0;
unsigned long curr_ms;
#define UDP_BSB_BUFSIZE 20
char udp_bsb_buf[ UDP_BSB_BUFSIZE ];
