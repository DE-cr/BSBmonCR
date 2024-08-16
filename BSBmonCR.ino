/*
 * BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
 * using esp32 with (optional) ssd1306 display
 * (file BSBmonCR.ino)
 */

//-- config:

#include "config.h"

#define BSBmonCRversion "0.10.9"
#define HELLO "-- Welcome to BSBmonCR v" BSBmonCRversion "! --"

#define BIN_WIDTH_S ( 24*60*60 / DATA_SIZE ) // set to e.g. 60 for plot speedup in testing
#define WIFI_CHECK_INTERVAL  7000L // [ms]
#define ADDR_CHECK_INTERVAL 15000L // [ms]
#define PV_UPDATE_INTERVAL 300000L // [ms]

#define OLED_TYPE U8G2_SSD1306_128X64_NONAME_F_HW_I2C
#define OLED_ROTATION U8G2_R0 // U8G2_R2 for 180°
#define OLED_FONT u8g2_font_helvR12_te
#define TEMP_FMT "%.1f"

#define UPDATE_DROPBOX_TOKEN_AT ":42" // when hh:mm ends like this

#define OTA_UPDATE_PORT 8080

//-- libs:

#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Ping.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Update.h>
#include <sunset.h>

//-- vars:

unsigned long last_wifi_check_ms = 0;

OLED_TYPE oled( OLED_ROTATION, U8X8_PIN_NONE );
WiFiUDP udp;

#define UDP_BUF_SIZE 99
char udp_buf[ UDP_BUF_SIZE ];
WiFiServer server( 80 );

#include "outside.xbm.h"
#include "house_off.xbm.h"
#include "house_semi_on.xbm.h"
#include "house_on.xbm.h"
#include "water_off.xbm.h"
#include "water_semi_on.xbm.h"
#include "water_on.xbm.h"
#include "bmp_header.h"
#include "favicon.ico.h"

const char* addr_to_check[] = { ADDR_TO_CHECK };
#define N_ADDR_TO_CHECK ( sizeof( addr_to_check ) / sizeof( *addr_to_check ) )
#define PLOT_REDUCTION ( ( N_ADDR_TO_CHECK + 2 ) / 3 )
#define ICON_SHIFT ( PLOT_REDUCTION ? -1 : 0 )
bool addr_available[ N_ADDR_TO_CHECK ];
int i_addr_to_check = 0;
unsigned long last_addr_check_ms = 0;

bool boiler_running = false;
bool rooms_heating = false;
bool water_heating = false;
double outsd_temp = -12.3;
double rooms_temp = 45.6;
double water_temp = 78.9;
double buffr_temp = 0;

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
t_log_data buffr[ DATA_SIZE ];
t_log_data ipadr[ N_ADDR_TO_CHECK ][ DATA_SIZE ];
int prev_pos = -1;

int time_now_seconds;
char time_now[8], time_prev[8];
char date_now[11], date_prev[11];
#ifdef BUFFER_TEMPERATURE
#define N_RECENT 7
#else
#define N_RECENT 6
#endif
int recent[N_RECENT], recent_set = 0;
#define MAX_LINE_LEN ( 5 + N_RECENT * 5 + 1 ) // e.g.: "00:00 -123 45 6 7 8 9\n"
char recent_line[ MAX_LINE_LEN + 1 ];
#define MAX_LOG_SIZE ( MAX_LINE_LEN * 24 * 60 + 1 )
char complete_log[ MAX_LOG_SIZE ];
unsigned long log_size = 0;

#define DROPBOX_TOKEN_SERVER "api.dropbox.com"
#define DROPBOX_CONTENT_SERVER "content.dropboxapi.com"
#define CHUNK_SIZE (8*1024)
#define TOKEN_INTRO "{\"access_token\": \""
#define MAX_TOKEN_LENGTH 150 // the ones I've seen are 139 chars each
char dropbox_access_token[ MAX_TOKEN_LENGTH + 1 ];
bool access_token_ok = false;

WebServer update_server( OTA_UPDATE_PORT );
const char* update_server_index =
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='update'>"
    "<input type='submit' value='Update'>"
  "</form>";

int pv_watts = 0;
double pv_kwh = 0;

char sunrise[8], sunset[8];  // hh:mm

//-- code:

bool pv_update( unsigned long ms ) {
#ifndef PV_IDENT
  return false;
#else
  static unsigned long last_pv_check_ms = 0;
  if ( ms - last_pv_check_ms < PV_UPDATE_INTERVAL // not yet time to update?
       && ms > last_pv_check_ms ) // no overflow?
    return false;
  last_pv_check_ms = ms;
  if ( strcmp( time_now, sunrise ) < 0 || strcmp( time_now, sunset ) > 0 ) {
    if ( !strncmp( time_now, "01:0x", 4 ) )
      pv_kwh = pv_watts = 0;
    return false;
  }
  WiFiClient client;
  #define PV_SERVER "user.nepviewer.com"
  if ( !client.connect( PV_SERVER, 80 ) ) return false;
  client.println( String( "GET /pv_monitor/home/index/" ) + PV_IDENT + " HTTP/1.1" );
  client.println( String( "Host: " ) + PV_SERVER );
  client.println( );
  for (int i=0; i<11 && !client.available(); ++i)
    delay(100);
  if ( !client.find( "round(" ) ) return false;  // e.g. "var now = Math.round(206);"
  int tmp_pv_watts = client.parseInt( );
  if ( !client.find( "Today:" ) ) return false;
  double tmp_pv_kwh = client.parseFloat( );
  if ( !client.find( "Last update:" ) ) return false;
  #define N 11
  char when[N];
  memset( when, 0, N );
  client.read( (uint8_t*)when, N-1 );
  #undef N
  Serial.println( String( "PV (" ) + when + ") = " + pv_watts + " W / " + pv_kwh + " kWh" );
  if ( strcmp( when, date_now ) ) return false;
  if ( tmp_pv_watts == 0 && (int)tmp_pv_kwh == 0 ) return false;
  pv_watts = tmp_pv_watts;
  pv_kwh = tmp_pv_kwh;
  return true;
#endif
}

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
  static int y_shift = 0;
  #ifdef PV_IDENT
    if ( --y_shift < 0 ) y_shift = 2;
    #define X_SHIFT 3
  #else
    #define X_SHIFT 0
  #endif
  oled.drawXBMP(  0 + X_SHIFT, y_pos + y_shift +  1, 18, 18, xbm_bits );
  oled.setCursor(20 + X_SHIFT, y_pos + y_shift + 16 );
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

unsigned char* compile_bitmap( ) {
  static unsigned char buf[ BMP_SIZE ];
  unsigned char* pbuf = buf;
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
  return buf;
}

bool updateDropboxToken( ) {
  #ifndef USE_DROPBOX
  return false;
  #endif
  bool success = false;
  Serial.print( "UpdateDropboxToken:" );
  WiFiClientSecure client;
  client.setInsecure( );
  String request = "grant_type=refresh_token&refresh_token=";
  request += DROPBOX_REFRESH_TOKEN;
  if ( client.connect( DROPBOX_TOKEN_SERVER, 443 ) ) {
    client.print( "POST /1/oauth2/token HTTP/1.1\r\n"
                  "Host: " DROPBOX_TOKEN_SERVER "\r\n"
                  "Authorization: Basic Ynl6cGF5bnNxc2JnN3ByOjVnYW8wdXpzeXVoZXgzYw==\r\n"
                  "Accept-Encoding: identity\r\n"
                  "Content-Type: application/x-www-form-urlencoded\r\n"
                  "Content-Length: " );
    client.print( request.length( ) );
    client.print( "\r\n\r\n" );
    client.print( request );
    while ( client.connected( ) && ! client.available( ) ) delay( 10 );
    String response = client.connected() ? client.readStringUntil('\n') : "diconnected";
    Serial.println( response );
    success = response == "HTTP/1.1 200 OK\r";
    while ( client.connected( ) ) // skip rest of header
      if ( client.readStringUntil( '\n' ) == "\r" )
        break;
    String line = client.readStringUntil( '\n' );
    success = line.startsWith( TOKEN_INTRO );
    if ( success ) {
      const char* p_line = line.c_str( ) + strlen( TOKEN_INTRO );
      char* p_token = dropbox_access_token;
      for ( int n = 0;  *p_line && *p_line != '"' && n < MAX_TOKEN_LENGTH;  ++n )
        *p_token++ = *p_line++;
      *p_token = 0;
    } else {
      Serial.print( ':' );
      Serial.println( line );
    }
    // we don't bother extracting the "expires_in" value from line
    while ( client.available() ) client.read();
    client.stop( );
  } else Serial.println( "connectFailed" );
  return success;
}

bool readFromDropbox( const char* path, const char* basename, const char* extension,
                      byte* data, unsigned long &size, unsigned long max_size, unsigned retries ) {
  #ifndef USE_DROPBOX
  return false;
  #endif
  if ( !access_token_ok ) return false;
  bool success = false;
  for (;;) {
    String fn = String( path ) + basename + extension;
    Serial.print( String( "<Dropbox" ) + fn + ':' );
    WiFiClientSecure client;
    client.setInsecure();
    if ( client.connect( DROPBOX_CONTENT_SERVER, 443 ) ) {
      client.print( "POST /2/files/download HTTP/1.1\r\n"
                    "Host: " DROPBOX_CONTENT_SERVER "\r\n"
                    "Accept-Encoding: identity\r\n"
                    "Authorization: Bearer " );
      client.print( dropbox_access_token );
      client.print( "\r\n"
                    "Dropbox-API-Arg: {\"path\":\"" );
      client.print( fn );
      client.print( "\"}\r\n\r\n" );
      while ( client.connected( ) && ! client.available( ) ) delay( 10 );
      String response = client.connected() ? client.readStringUntil('\n') : "diconnected";
      Serial.println( response );
      success = response == "HTTP/1.1 200 OK\r";
      unsigned long data_size = 0;
      while ( client.connected( ) ) { // process rest of header
        String line = client.readStringUntil( '\n' );
        sscanf( line.c_str( ), "Content-Length: %lu", &data_size );
        if ( line == "\r" ) break;
      }
      if ( success )
        for ( size = 0;  client.connected() && size < data_size && size < max_size;  ++size, ++data ) {
          // WAIT for client data, just to be safe:
          while ( client.connected( ) && ! client.available( ) ) delay( 10 );
          *data = client.read( );
        }
      client.stop( );
      Serial.print( size );
      Serial.print( '/' );
      Serial.println( data_size );
    } else Serial.println( "connectFailed" );
    if ( success || !(retries--) ) return success;
    delay( 1000 );
  }
}
                       
bool send2dropbox( const char* path, const char* basename, const char* extension,
                   const byte* data, unsigned long size, unsigned retries ) {
  #ifndef USE_DROPBOX
  return false;
  #endif
  if ( !access_token_ok ) return false;
  bool success = false;
  for (;;) {
    String fn = String( path ) + basename + extension;
    Serial.print( String( ">Dropbox" ) + fn + ':' );
    WiFiClientSecure client;
    client.setInsecure();
    if ( client.connect( DROPBOX_CONTENT_SERVER, 443 ) ) {
      client.print( "POST /2/files/upload HTTP/1.1\r\n"
                    "Host: " DROPBOX_CONTENT_SERVER "\r\n"
                    "Authorization: Bearer " );
      client.print( dropbox_access_token );
      client.print( "\r\n"
                    "Dropbox-API-Arg: {\"path\":\"" );
      client.print( fn );
      client.print( "\",\"mode\":\"overwrite\"}\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    "Content-Length: " );
      client.print( size );
      client.print( "\r\n\r\n" );
      while ( size ) {
        unsigned n = size > CHUNK_SIZE ? CHUNK_SIZE : size;
        client.write( data, n );
        data += n;
        size -= n;
      }
      while ( client.connected( ) && ! client.available( ) ) delay( 10 );
      String response = client.connected() ? client.readStringUntil('\n') : "diconnected";
      Serial.println( response );
      success = response == "HTTP/1.1 200 OK\r";
      while ( client.available() ) client.read();
      client.stop( );
    } else Serial.println( "connectFailed" );
    if ( success || !(retries--) ) return success;
    delay( 1000 );
  }
}

void init_ota_update( ) {
  update_server.on( "/", HTTP_GET, []() {
    update_server.sendHeader( "Connection", "close" );
    update_server.send( 200, "text/html", update_server_index );
  });
  update_server.on( "/update", HTTP_POST, []() {
    update_server.sendHeader( "Connection", "close" );
    update_server.send( 200, "text/plain", Update.hasError( ) ? "Failed" : "Success" );
    if ( log_size )
      send2dropbox( DROPBOX_PATH, date_prev, ".csv", (byte*)complete_log, log_size, 2 );
    ESP.restart( );
  }, []() {
    HTTPUpload& upload = update_server.upload( );
    if ( upload.status == UPLOAD_FILE_START ) {
      Serial.println( "Updating ESP32 firmware..." );
      if ( ! Update.begin( UPDATE_SIZE_UNKNOWN ) ) // start with max available size
        Update.printError( Serial );
    } else if ( upload.status == UPLOAD_FILE_WRITE ) {
      if ( Update.write( upload.buf, upload.currentSize ) != upload.currentSize )
        Update.printError( Serial );
    } else if ( upload.status == UPLOAD_FILE_END ) {
      if ( Update.end( true ) ) // true to set the size to the current progress
        Serial.println( "Update success, rebooting..." );
      else
        Update.printError( Serial );
    }
  });
  update_server.begin( );
  Serial.print( "Update server started on port " );
  Serial.println( OTA_UPDATE_PORT );
}

void convert_bsblan_udp( char* udp_buf ) {
  // sample data w/o custom code in BSB_LAN unit:
  // 364593010;01.05.2022 00:00:15;8314;Kesselrücklauftemperatur Ist;66.7;°C
  // function to convert this to custom format (here: "8314:667")
  #define DELIM ";"
  int param;
  char *p = strtok( udp_buf, DELIM );  // millis
  if ( p ) p = strtok( 0, DELIM );     // timestamp
  if ( p ) p = strtok( 0, DELIM );     // param no.
  if ( p ) {
    param = atoi( p );
    p = strtok( 0, DELIM );            // name
  }
  if ( p ) p = strtok( 0, DELIM );     // value
  if ( p ) {
    int n = strlen( p );
    if ( n >= 2 && p[n-2] == '.' )
      strcpy( p+n-2, p+n-1 );
    sprintf( udp_buf, "%d:%s", param, p );
  }
}

void update_time( ) {
  struct tm timeinfo;
  if( !getLocalTime( &timeinfo ) ){
    Serial.println( "Failed to obtain time!" );
    return;
  }
  strftime( date_now, 11, "%F", &timeinfo );
  strftime( time_now,  6, "%R", &timeinfo );
  time_now_seconds = timeinfo.tm_sec;
  if ( !strcmp( date_now, date_prev ) ) return;  // no need for expensive sunrise/sunset calculation
  //
  Serial.println( String(date_now) + '_' + time_now + '+' + time_now_seconds + 's' );
  #ifdef PV_IDENT
  char utc_offset[6];
  strftime(utc_offset, 6, "%z", &timeinfo );
  int hh, mm;
  sscanf( utc_offset+1, "%2d%2d", &hh, &mm );
  double hours_from_utc = hh + mm/60.0;
  if ( *utc_offset == '-' ) hours_from_utc *= -1;
  Serial.println( String( "hours_from_utc = " ) + hours_from_utc );
  int y, m, d;
  sscanf( date_now, "%d-%d-%d", &y, &m, &d );
  SunSet location;
  location.setCurrentDate( y, m, d );
  location.setPosition( LATITUDE, LONGITUDE, hours_from_utc );
  double sun_up = location.calcSunrise( );
  if ( sun_up   != sun_up   ) strcpy( sunrise, "00:00" );  // NaN?
  else sprintf( sunrise, "%02d:%02d", (int)(sun_up  /60), ((int)sun_up  )%60 );
  double sun_down = location.calcSunset( );
  if ( sun_down != sun_down ) strcpy( sunset , "23:59" );  // NaN?
  else sprintf( sunset , "%02d:%02d", (int)(sun_down/60), ((int)sun_down)%60 );
  #endif
}

void send2bsblan( const char *path ) {
  WiFiClient client;
  if ( !client.connect( "bsb-lan", 80 ) ) return;
  client.print( String( "GET " ) + path + " HTTP/1.1\r\n\r\n" );
  while ( client.connected( ) )
    if ( client.available( ) ) client.read( ); else delay( 1 );
}

void limit_boiler_runs_for_water( ) {
  #ifdef LIMIT_BOILER_RUNS_FOR_WATER
  static bool did_run = false;
  static bool dhw_on = true;
  bool new_day = !strcmp( time_now, "01:00" );
  if ( new_day && !(boiler_running && water_heating) ) did_run = false;
  if ( !dhw_on && (    new_day
                    || boiler_running  // boiler already running, anyway (rooms, manual DHW)?
                    || buffr_temp - water_temp >= RE_SWITCH_DIFFERENCE  // use buffer again!
                  ) ) {
    Serial.println( "DHW = on" );
    dhw_on = true;
    send2bsblan( "/S1600=1" );
  }
  #ifdef NO_BOILER_STARTS_FOR_WATER_AFTER_INITIAL_RUN
  if ( boiler_running ) did_run = true;
  #endif
  if ( !water_heating ) return;
  if ( boiler_running ) did_run = true;
  else if ( dhw_on && did_run
            // not in boiler overrun?:
            && water_temp < TWW_Sollwert_1610
            // buffer soon not sufficient?:
            && buffr_temp - water_temp < TWW_Umladeueberhoehung_5021 + SWITCH_MARGIN ) {
    Serial.println( "DHW = off" );
    dhw_on = false;
    send2bsblan( "/S1600=0" );
  }
  #endif
}

void setup( ) {
  setCpuFrequencyMhz( 80 );  // 240->80 MHz = approx. -20% power consumption
  Serial.begin( 115200 );
  Serial.println( HELLO );
  memset( udp_buf, 0, UDP_BUF_SIZE );
  memset( outsd, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( rooms, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( water, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( heatg, 0, DATA_SIZE * sizeof( t_log_data ) );
  memset( buffr, 0, DATA_SIZE * sizeof( t_log_data ) );
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
  *date_prev = *time_prev = 0;
  *dropbox_access_token = 0;
  *sunset = *sunrise = 0;
  init_ota_update( );
  configTime( 0, 0, NTP_SERVER );
  setenv( "TZ", LOCAL_TIMEZONE, 1 );
  tzset( );
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
  bool screen_update_reqd = pv_update( ms );
  //- which log data bin to use?:
  int pos = ms              // overflow after ca. 54 d ignored!
            / 1000          // => s
            / BIN_WIDTH_S   // => bin
            % DATA_SIZE;    // overflow => wrap around
  if ( pos != prev_pos ) {  // entering new log bin? => reset contents
    Serial.println( pos );
    memset( &outsd[pos], 0, sizeof( t_log_data ) );
    memset( &rooms[pos], 0, sizeof( t_log_data ) );
    memset( &water[pos], 0, sizeof( t_log_data ) );
    memset( &heatg[pos], 0, sizeof( t_log_data ) );
    memset( &buffr[pos], 0, sizeof( t_log_data ) );
    for ( int i=0; i<N_ADDR_TO_CHECK; ++i )
      memset( &ipadr[i][pos], 0, sizeof( t_log_data ) );
    prev_pos = pos;
    screen_update_reqd = true;
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
    // ping takes about 1.2 s (!) on my system
    addr_available[ i_addr_to_check ] = Ping.ping( addr_to_check[ i_addr_to_check ], 1 );
    Serial.println( addr_available[ i_addr_to_check ] );
    log_data( &ipadr[i_addr_to_check][pos], addr_available[i_addr_to_check] ? 1 : 0 );
    screen_update_reqd = true;
    ++ i_addr_to_check;
  }
  //- UDP packet received?:
  bool new_data = false;
  if ( udp.parsePacket( ) ) {
    memset( udp_buf, 0, UDP_BUF_SIZE );
    udp.read( udp_buf, UDP_BUF_SIZE-1 );
    convert_bsblan_udp( udp_buf );
    Serial.println( udp_buf );
    int param, value;
    if ( 2 == sscanf( udp_buf, "%d:%d", &param, &value ) ) {
      switch ( param ) {
        #define ADD_TO_LOG(i) { recent[i] = value;  recent_set |= 1 << i; } while ( 0 )
        case HEATING_STATUS:
          rooms_heating = ROOMS_HEATING;
          new_data = true;
          ADD_TO_LOG( 0 );
          break;
        case WATER_STATUS:
          water_heating = WATER_HEATING;
          new_data = true;
          ADD_TO_LOG( 1 );
          break;
        case BOILER_STATUS:
          boiler_running = BOILER_RUNNING;
          log_data( &heatg[pos], boiler_running ? 1 : 0 );
          new_data = true;
          ADD_TO_LOG( 2 );
          break;
        case OUTSIDE_TEMPERATURE:
          outsd_temp = 0.1 * value;
          log_data( &outsd[pos], value );
          new_data = true;
          ADD_TO_LOG( 3 );
          break;
        case ROOMS_TEMPERATURE:
          rooms_temp = 0.1 * value;
          log_data( &rooms[pos], value );
          new_data = true;
          ADD_TO_LOG( 4 );
          break;
        case WATER_TEMPERATURE:
          water_temp = 0.1 * value;
          log_data( &water[pos], value );
          new_data = true;
          ADD_TO_LOG( 5 );
          break;
        #ifdef BUFFER_TEMPERATURE
        case BUFFER_TEMPERATURE:
          buffr_temp = 0.1 * value;
          log_data( &buffr[pos], value );
          new_data = true;
          ADD_TO_LOG( 6 );
          break;
        #endif
      }
      limit_boiler_runs_for_water( );
    }
    screen_update_reqd |= new_data;
  }//udp
  if ( screen_update_reqd ) update_time( );
  if ( new_data ) {
    //- new data -> log!:
    #define FULL_SET ( ( 1 << N_RECENT ) - 1 )
    if ( recent_set == FULL_SET ) { // only when all params have been set
      recent_set = 0;
      if ( *time_now && strcmp( time_now, time_prev ) ) { // new hh:mm
        if ( !access_token_ok ||
             !strcmp( time_now + 5 - strlen( UPDATE_DROPBOX_TOKEN_AT ), UPDATE_DROPBOX_TOKEN_AT ) )
          // Dropbox access tokens usually expire after 4h, but this is easier for us to handle
          access_token_ok = updateDropboxToken( );
        bool new_day = strcmp( date_now, date_prev );
        if ( log_size ) {
          if ( ! strcmp( time_now + 5 - strlen( DROPBOX_BMP_WRITE_TIME_PATTERN ),
                         DROPBOX_BMP_WRITE_TIME_PATTERN ) )
            send2dropbox( DROPBOX_PATH, date_prev, ".bmp", compile_bitmap( ), BMP_SIZE, 0 );
          if ( log_size && ( new_day ||
               ! strcmp( time_now + 5 - strlen( DROPBOX_CSV_WRITE_TIME_PATTERN ),
                         DROPBOX_CSV_WRITE_TIME_PATTERN ) ) )
            send2dropbox( DROPBOX_PATH, date_prev, ".csv", (byte*)complete_log, log_size, new_day ? 2 : 0 );
        } // log_size
        if ( new_day ) {
          Serial.println( String( date_now ) + " (" + sunrise + '-' + sunset + ')' );
          log_size = *complete_log = 0; // clear log for new day (even if the last send2dropbox failed!)
          strcpy( date_prev, date_now );
          // any data to restore from dropbox?:
          readFromDropbox( DROPBOX_PATH, date_now, ".csv", (byte*)complete_log, log_size, MAX_LOG_SIZE, 0 );
        }
        strcpy( recent_line, time_now );
        for ( int i = 0;  i < N_RECENT;  ++i ) {
          char x[9];
          sprintf( x, ",%d", recent[i] );
          strcat( recent_line, x );
        }
        strcat( recent_line, "\n" );
        Serial.print( recent_line );
        int n = strlen( recent_line );
        if ( log_size + n > MAX_LOG_SIZE )
          Serial.println( "log_full" );
        else {
          strcat( complete_log + log_size, recent_line );
          log_size += n;
        }
        Serial.print( "log_size=" );
        Serial.println( log_size );
        strcpy( time_prev, time_now );
      } // new hh:mm
    } // log
  } // new data
  //- redraw screen?:
  if ( screen_update_reqd ) {
    oled.clearBuffer( );
    // symbols and current values on the left:
    draw_temp( outsd_temp,  0 + ICON_SHIFT, outside_bits );
    draw_temp( rooms_temp, 21 + ICON_SHIFT - PLOT_REDUCTION,
               rooms_heating ? ( boiler_running ? house_on_bits : house_semi_on_bits )
                             : house_off_bits );
    draw_temp( water_temp, 42 + ICON_SHIFT - PLOT_REDUCTION * 2,
               water_heating ? ( boiler_running ? water_on_bits : water_semi_on_bits )
                             : water_off_bits );
    // addr presence bars on lower left:
    #define len ( ( 128 - DATA_SIZE ) / ( N_ADDR_TO_CHECK ? N_ADDR_TO_CHECK : 1 ) )
    for ( int addr=0; addr<N_ADDR_TO_CHECK; ++addr )
      if ( addr_available[ addr ] )
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
    #ifdef BUFFER_TEMPERATURE
      #ifdef BUFFER_TEMP_OFFSET // absolute scale
        int xb = 128 - DATA_SIZE;
        int yb = buffr_temp - BUFFER_TEMP_OFFSET - 0.5;
        for ( int i=0; i<=yb; ++i )
          if ( i%10!=9 || i==yb )
            oled.drawPixel( xb, 63-i );
      #else // relative scale
        #define MAXY 63
        int ymin = find_min( buffr );
        int ymax = find_max( buffr );
        if ( ymin != ymax ) { // otherwise we don't know how to scale
          int x = 128 - DATA_SIZE;
          int y = (long)( 10 * buffr_temp - ymin ) * MAXY / ( ymax - ymin );
          oled.drawLine( x, MAXY, x, MAXY-y );
        }
      #endif
    #endif
    #ifdef PV_IDENT
    // current power, vertical from top left, 10 Watts per pixel:
    int yp = (pv_watts+5)/10 - 1;
    for ( int i=0; i<=yp; ++i )
      if ( i%10!=9 || i==yp )
        oled.drawPixel( 0, i );
    // current day's energy generation, top line horizontal, 10 pixels per kWh:
    int xp = (int)(pv_kwh*10+0.5) - 1;
    for ( int i=0; i<=xp; ++i )
      if ( i%10!=9 || i==xp )
        oled.drawPixel( 128-DATA_SIZE-1-i, 0 );
    #endif
    #ifdef WITH_NERDY_TIMESTAMP_DISPLAY
    int y_, m_, d_;
    sscanf( date_now, "%d-%d-%d", &y_, &m_, &d_ );
    unsigned short yyyy = y_;
    byte mm=m_, dd=d_;
    unsigned long bits = yyyy<<16ul | mm<<8ul | dd;
    int y = 63 - PLOT_REDUCTION * 3;
    for ( int x=0;  x < 32;  ++x )
      if ( bits & 1ul << ( 31 - x ) )
        oled.drawPixel( x, y );
    int h_;
    sscanf( time_now, "%d:%d", &h_, &m_ );
    byte HH=h_, MM=m_, SS=time_now_seconds;
    bits = HH<<16ul | MM<<8ul | SS;
    for ( int x=0;  x < 24;  ++x )
      if ( bits & 1ul << ( 23 - x ) )
        oled.drawPixel( x + 32, y );
    #ifdef LSB_MARKERS_FOR_NERDY_TIMESTAMP
    y = y==63 ? y-1 : y+1;  // below timestamp, if possible, otherwise above
    for ( int i=0;  i<7;  ++i )
      oled.drawPixel( 8*i+7, y );
    #endif
    #endif
    oled.sendBuffer( );
  }
  //- HTTP client request?:
  // (takes about 35 ms on my system)
  WiFiClient client = server.available( );
  if ( client ) {
    unsigned char buf[ 999 ];  // to hold a single line from client
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
          client.write( compile_bitmap( ), BMP_SIZE );
          break;
        }
        else if ( ! strcmp( (char*)buf, "GET /favicon.ico " ) ) {
          // ready to go, forget about the rest
          client.println( "HTTP/1.1 200 OK" );
          client.println( "Content-Type: image/icon" );
          client.println( "Connection: close" );
          client.println( );
          client.write( favicon, sizeof(favicon) );
          break;
        }
      }
    client.stop( );
    Serial.println( );
  }
  //-
  update_server.handleClient( );
}
