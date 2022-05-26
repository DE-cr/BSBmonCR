/*
 * BSB-LAN monitor
 * by user -cr at forum.fhem.de
 * using esp32 with ssd1306 display
 * inspired by https://forum.fhem.de/index.php/topic,110599.0.html
 * (file BSBmonCR.ino)
 */

//-- config:

#define HELLO "-- Welcome to BSBmonCR v0.3.1! --"

#define MY_SSID "MyWLAN"
#define MY_PASSWORD "MyPassword"
#define WIFI_CHECK_INTERVAL 7000  // [ms]

// names of computers to periodically check their network presence:
#define ADDR_TO_CHECK  // e.g.: "Smartphone1", "Smartphone2", "Notebook"
#define ADDR_CHECK_INTERVAL 15000
#define ADDR_CHECK_HOLD_STATUS 0  // !0 = stabilized presence indicator / "hysteresis"

#define UDP_PORT 28000

#define BIN_WIDTH ( 24*60*60 / DATA_SIZE )  // set to e.g. 10 for plot speedup in testing

#define OLED_TYPE U8G2_SSD1306_128X64_NONAME_F_HW_I2C
#define OLED_ROTATION U8G2_R0  // U8G2_R2 for 180°
#define OLED_FONT u8g2_font_helvR12_te
#define TEMP_FMT "%.1f"

//-- libs:

#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Ping.h>

//-- vars:

unsigned long last_wifi_check_ms = 0;

OLED_TYPE oled( OLED_ROTATION, U8X8_PIN_NONE );
WiFiUDP udp;

#define UDP_BUF_SIZE 20
char udp_buf[ UDP_BUF_SIZE ];
WiFiServer server( 80 );

#include "outside.xbm.h"
#include "house_off.xbm.h"
#include "house_on.xbm.h"
#include "water_off.xbm.h"
#include "water_on.xbm.h"
#include "bmp_header.h"

const char* addr_to_check[] = { ADDR_TO_CHECK };
#define N_ADDR_TO_CHECK ( sizeof( addr_to_check ) / sizeof( *addr_to_check ) )
#define PLOT_REDUCTION ( ( N_ADDR_TO_CHECK + 2 ) / 3 )
bool addr_available[ N_ADDR_TO_CHECK ];
int i_addr_to_check = 0;
unsigned long last_addr_check_ms = 0;

bool rooms_heating = 0;
bool water_heating = 0;
double outsd_temp = -12.3;
double rooms_temp = 45.6;
double water_temp = 78.9;

#define DATA_SIZE (24*3)  // 3 bins / h => bin width = 20 min
typedef struct {
  int sum;
  int cnt;
  int avg;
} t_log_data;
t_log_data outsd[ DATA_SIZE ];
t_log_data rooms[ DATA_SIZE ];
t_log_data water[ DATA_SIZE ];
t_log_data heatg[ DATA_SIZE ];
t_log_data ipadr[ N_ADDR_TO_CHECK ][ DATA_SIZE ];
int prev_pos = -1;

//-- code:

int find_min( t_log_data* data ) {
  int min = 0;
  int i;
  for ( i=0; i<DATA_SIZE-1; ++i )
    if ( data[i].cnt ) {
      min = data[i].avg;
      break;
    }
  for ( ++i; i<DATA_SIZE  ; ++i )
    if ( data[i].cnt  &&  data[i].avg < min )
      min = data[i].avg;
  return min;
}

int find_max( t_log_data* data ) {
  int max = 0;
  int i;
  for ( i=0; i<DATA_SIZE-1; ++i )
    if ( data[i].cnt ) {
      max = data[i].avg;
      break;
    }
  for ( ++i; i<DATA_SIZE  ; ++i )
    if ( data[i].cnt  &&  data[i].avg > max )
      max = data[i].avg;
  return max;
}

void log_data( t_log_data* data, int val ) {
  data->avg = ( data->sum += val ) / ++ data->cnt;
}

void draw_temp( double val, int y_pos, unsigned char* xbm_bits ) {
  char str[ 9 ];
  sprintf( str, TEMP_FMT, val );
  if ( strlen( str ) > 4 ) str[3] = '\0';  // -10.0 (too wide) => -10
  oled.drawXBMP( -1, y_pos, 20, 20, xbm_bits );
  oled.setCursor( 20, y_pos + 16 );
  oled.print( str );
}

void draw_log( t_log_data* data, int pos, int y_offset ) {
  int p, x1, x2, y1, y2, n=0;
  int min = find_min( data );
  int max = find_max( data );
  if ( min == max ) return;  // constant value, don't know how to scale
  for ( int i=0; i<DATA_SIZE-1; ++i ) {
    p = ( pos + DATA_SIZE - i ) % DATA_SIZE;
    if ( data[ p ].cnt == 0 )
      continue;  // no data here, try next bin
    x1 = 127 - i;  // most recent is on the very right
    #define MAXY ( 19 - PLOT_REDUCTION )
    y1 = y_offset + MAXY - (long)( data[p].avg - min ) * MAXY / ( max - min );
    if ( n++ )  // only after more than one point found can we draw a line
      oled.drawLine( x1, y1, x2, y2 );
    x2 = x1;
    y2 = y1;
  }
}

void setup( ) {
  Serial.begin( 115200 );
  Serial.println( HELLO );
  memset( udp_buf, 0, UDP_BUF_SIZE );
  memset( outsd, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( rooms, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( water, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( heatg, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( ipadr, 0, N_ADDR_TO_CHECK * DATA_SIZE * sizeof( t_log_data ) );
  memset( addr_available, 0, N_ADDR_TO_CHECK * sizeof( *addr_available ) );
  oled.begin( );
  oled.enableUTF8Print( );
  oled.setFont( OLED_FONT );
  oled.setDrawColor( 1 );
  WiFi.mode( WIFI_STA );
  WiFi.begin( MY_SSID, MY_PASSWORD );
  udp.begin( UDP_PORT );
  server.begin( );
  Serial.println( "setup() done." );
}

void loop( ) {
  unsigned long ms = millis( );
  //- wifi check?:
  if ( ms - last_wifi_check_ms > WIFI_CHECK_INTERVAL
       || ms < last_wifi_check_ms  // overflow?
     ) {
    if ( WiFi.status( ) != WL_CONNECTED ) {
      Serial.println( "Reconnecting WiFi..." );
      WiFi.disconnect( );
      WiFi.begin( );
     }
    last_wifi_check_ms = ms;  
  }
  int screen_update_reqd = 0;
  //- which log data bin to use?:
  int pos = ms            // overflow after ca. 54 d ignored!
            / 1000        // => s
            / BIN_WIDTH   // => bin
            % DATA_SIZE;  // overflow => wrap around
  if ( pos != prev_pos ) {  // entering new log bin? => reset contents
    Serial.println( pos );
    memset( &outsd[pos], 0, sizeof( t_log_data ) );
    memset( &rooms[pos], 0, sizeof( t_log_data ) );
    memset( &water[pos], 0, sizeof( t_log_data ) );
    memset( &heatg[pos], 0, sizeof( t_log_data ) );
    for ( int i=0; i<N_ADDR_TO_CHECK; ++i )
      memset( &ipadr[i][pos], 0, sizeof( t_log_data ) );
    prev_pos = pos;
    screen_update_reqd = 1;
  }
  //- check presence of IP addresses given:
  if ( N_ADDR_TO_CHECK &&  // feature enabled?
       ( ms - last_addr_check_ms > ADDR_CHECK_INTERVAL  // time to check?
         || ms < last_addr_check_ms )  // overflow?
     ) {
    last_addr_check_ms = ms;
    if ( i_addr_to_check >= N_ADDR_TO_CHECK )
      i_addr_to_check = 0;
    Serial.print( addr_to_check[ i_addr_to_check ] );
    Serial.print( ':' );
    addr_available[ i_addr_to_check ] = Ping.ping( addr_to_check[ i_addr_to_check ], 1 );
    Serial.println( addr_available[ i_addr_to_check ] );
    log_data( &ipadr[i_addr_to_check][pos], addr_available[i_addr_to_check] ? 1 : 0 );
    screen_update_reqd = 1;
    ++ i_addr_to_check;
  }
  //- UDP packet received?:
  if ( udp.parsePacket( ) ) {
    memset( udp_buf, 0, UDP_BUF_SIZE );
    udp.read( udp_buf, UDP_BUF_SIZE-1 );
    Serial.println( udp_buf );
    int par, val;
    if ( 2 == sscanf( udp_buf, "%d:%d", &par, &val ) )
      switch ( par ) {
        case 8001: rooms_heating = 111 <= val && val <= 116;
                   screen_update_reqd = 1;
                   break;
        case 8003: water_heating =  92 <= val && val <=  97;
                   screen_update_reqd = 1;
                   break;
        case 8700: outsd_temp = 0.1 * val;
                   log_data( &outsd[pos], val );
                   screen_update_reqd = 1;
                   break;
        case 8770: rooms_temp = 0.1 * val;
                   log_data( &rooms[pos], val );
                   screen_update_reqd = 1;
                   break;
        case 8830: water_temp = 0.1 * val;
                   log_data( &water[pos], val );
                   screen_update_reqd = 1;
                   break;
        case 8005: log_data( &heatg[pos], val != 25 ? 1 : 0 );
                   screen_update_reqd = 1;
                   break;
      }
  }
  //- redraw screen?:
  if ( screen_update_reqd ) {
    oled.clearBuffer( );
    // symbols and current values on the left:
    draw_temp( outsd_temp,  0, outside_bits );
    draw_temp( rooms_temp, 21 - PLOT_REDUCTION,
               rooms_heating ? house_on_bits : house_off_bits );
    draw_temp( water_temp, 42 - PLOT_REDUCTION * 2,
               water_heating ? water_on_bits : water_off_bits );
    // addr presence bars on lower left:           
    #define len ( ( 128 - DATA_SIZE ) / N_ADDR_TO_CHECK )
    for ( int addr=0; addr<N_ADDR_TO_CHECK; ++addr )
      if ( ADDR_CHECK_HOLD_STATUS ? ipadr[addr][pos].sum : addr_available[ addr ] )
        oled.drawLine( addr*len, 62, (addr+1)*len-2, 62 );
    // log plots on the right:
    draw_log( outsd, pos,  0 );
    draw_log( rooms, pos, 21 - PLOT_REDUCTION );
    draw_log( water, pos, 42 - PLOT_REDUCTION * 2 );
    // heating on/off and addr presence marker lines at bottom of screen:
    for ( int i=0; i<DATA_SIZE; ++i ) {
      if ( heatg[ ( pos + DATA_SIZE - i ) % DATA_SIZE ].sum )
        oled.drawPixel( 127 - i, 63 - PLOT_REDUCTION * 3 );
      for ( int addr=0; addr<N_ADDR_TO_CHECK; ++addr )
        if ( ipadr[ addr ][ ( pos + DATA_SIZE - i ) % DATA_SIZE ].sum )
          oled.drawPixel( 127-i, 63-(N_ADDR_TO_CHECK-1)+addr );
    }
    oled.sendBuffer( );
  }
  //- HTTP client request?:
  WiFiClient client = server.available( );
  if ( client ) {
    unsigned char buf[ BMP_SIZE ];
    unsigned char* pbuf = buf;
    // don't wait forever:
    while ( client.connected( ) && millis( ) - ms < 100 )
      if ( client.available( ) ) {
        if ( ( *pbuf  = client.read( ) ) == '\n' )
          break;  // we're not interested in what comes after the first \n
        Serial.write( *pbuf );
        *++pbuf = '\0';
        if ( ! strcmp( (char*)buf, "GET / " ) ) {
          // ready to go, forget about the rest
          client.println( "HTTP/1.1 200 OK" );
          client.println( "Content-Type: image/bmp" );
          client.println( "Connection: close" );
          client.println( );
          // assemble BMP data in buffer to do just one (slow) client write:
          pbuf = buf;
          for ( int i=0; i<BMP_HEADER_SIZE; ++i )
            *pbuf++ = bmp_header[i];
          unsigned char* p = oled.getBufferPtr( );
          int tw = oled.getBufferTileWidth( );
          int th = oled.getBufferTileHeight( );
          for ( int y=0; y<th*8; ++y )
            for ( int x=0; x<tw*8; x+=8 ) {
              unsigned char v = 0;
              for ( int b=0; b<8; ++b ) {
                v <<= 1;
                if ( u8x8_capture_get_pixel_1( x+b, th*8-1-y, p, tw ) )
                  v |= 1;
              }
              *pbuf++ = ~v;  // black on white looks better in BMP
            }
          client.write( buf, BMP_SIZE );
          break;
        }
      }
    client.stop( );
    Serial.println( );
  }
}
