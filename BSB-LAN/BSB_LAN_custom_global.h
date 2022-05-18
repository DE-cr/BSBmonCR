// Add custom code for setup function here which will be included at the end of the global section

/*
 * BSB-LAN monitor
 * by user -cr at forum.fhem.de
 * using esp32 with ssd1306 display
 * inspired by https://forum.fhem.de/index.php/topic,110599.0.html
 * (file BSB_LAN_custom_global.h)
 */

//- config:
#define UDP_PORT 28000
IPAddress broadcast_ip = IPAddress( 192, 168, 178, 255 );
int udp_bsb_params[] = { 8700, 8770, 8830, 8001, 8003, 8005 };

//- other:
#define N_UDP_BSB_PARAMS ( sizeof( udp_bsb_params ) / sizeof( *udp_bsb_params ) )
int i_udp_bsb_param = 0;
unsigned long prev_ms = 0;
unsigned long curr_ms;
#define UDP_BSB_BUFSIZE 20
char udp_bsb_buf[ UDP_BSB_BUFSIZE ];
