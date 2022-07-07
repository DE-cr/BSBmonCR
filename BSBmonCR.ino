/*
 * BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
 * using esp32 with (optional) ssd1306 display
 * (file BSBmonCR.ino)
 */

//-- config:

#include "config.h"

#define BSBmonCRversion "0.6.0"
#define HELLO "-- Welcome to BSBmonCR v" BSBmonCRversion "! --"

#define BIN_WIDTH_S ( 24*60*60 / DATA_SIZE ) // set to e.g. 60 for plot speedup in testing
#define WIFI_CHECK_INTERVAL  7000 // [ms]
#define ADDR_CHECK_INTERVAL 15000 // [ms]
#define UDP_PORT 28000

#define OLED_TYPE U8G2_SSD1306_128X64_NONAME_F_HW_I2C
#define OLED_ROTATION U8G2_R0 // U8G2_R2 for 180°
#define OLED_FONT u8g2_font_helvR12_te
#define TEMP_FMT "%.1f"

#define UPDATE_DROPBOX_TOKEN_AT ":15" // when hh:mm ends like this

#define OTA_UPDATE_PORT 8080

//-- libs:

#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Ping.h>
#include <WiFiClientSecure.h>
#include "NTPClientFix.h"
#include <WebServer.h>
#include <Update.h>

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

WiFiUDP ntpUDP;
NTPClient timeClient( ntpUDP, HOURS_OFFSET_TO_UTC*60*60 );
char time_now[6], time_prev[6];
char date_now[11], date_prev[11];
int recent[6], recent_set = 0;
#define MAX_LINE_LEN ( 5 + 6*5 + 1 ) // e.g.: "00:00 -123 45 6 7 8 9\n"
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

int leapyear( int year ) {
  return !(year % 4) && (year % 100 || !(year % 400)) ? 1 : 0;
}

char* epoch2date( unsigned long epoch, char* date ) {
  // date must be at least char[11]!
  epoch /= 24 * 60 * 60;
  int year = 1970;
  while ( epoch >= 365 + leapyear( year ) )
    epoch -= 365 + leapyear( year ++ );
  int days_in_month[] =
    //Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30 };
  days_in_month[ 1 ] += leapyear( year ); // fix Feb?
  int* m_days = days_in_month;
  int month;
  for ( month = 0; epoch >= *m_days; ++ month )
    epoch -= *( m_days ++);
  sprintf( date, "%04d-%02d-%02d", year, month+1, (int)epoch+1 );
  return date;
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
    if ( success = line.startsWith( TOKEN_INTRO ) ) {
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
  timeClient.begin( );
  *date_prev = *time_prev = 0;
  *dropbox_access_token = 0;
  init_ota_update( );
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
    // ping takes about 1.2 s (!) on my system
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
        #define ADD_TO_LOG(i) { recent[i] = val;  recent_set |= 1 << i; } while ( 0 )
        case 8001: rooms_heating = 111 <= val && val <= 116;
                   screen_update_reqd = 1;
                   ADD_TO_LOG( 0 );
                   break;
        case 8003: water_heating =  80 <= val && val <=  97;
                   screen_update_reqd = 1;
                   ADD_TO_LOG( 1 );
                   break;
        case 8005: log_data( &heatg[pos], val != 25 ? 1 : 0 );
                   screen_update_reqd = 1;
                   ADD_TO_LOG( 2 );
                   break;
        case 8700: outsd_temp = 0.1 * val;
                   log_data( &outsd[pos], val );
                   screen_update_reqd = 1;
                   ADD_TO_LOG( 3 );
                   break;
        case 8770: rooms_temp = 0.1 * val;
                   log_data( &rooms[pos], val );
                   screen_update_reqd = 1;
                   ADD_TO_LOG( 4 );
                   break;
        case 8830: water_temp = 0.1 * val;
                   log_data( &water[pos], val );
                   screen_update_reqd = 1;
                   ADD_TO_LOG( 5 );
                   break;
      }
    //- new data -> log!:
    if ( recent_set == 0x3F ) { // only when all params have been set
      timeClient.update( );
      NTPClientFix( timeClient );
      sprintf( time_now, "%02d:%02d", timeClient.getHours( ), timeClient.getMinutes( ) );
      if ( strcmp( time_now, time_prev ) ) { // new hh:mm
        if ( !access_token_ok ||
             !strcmp( time_now + 5 - strlen( UPDATE_DROPBOX_TOKEN_AT ), UPDATE_DROPBOX_TOKEN_AT ) )
          // Dropbox access tokens usually expire after 4h, but this is easier for us to handle
          access_token_ok = updateDropboxToken( );
        epoch2date( timeClient.getEpochTime( ), date_now );
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
          Serial.println( date_now );
          log_size = *complete_log = 0; // clear log for new day (even if the last send2dropbox failed!)
          strcpy( date_prev, date_now );
          // any data to restore from dropbox?:
          readFromDropbox( DROPBOX_PATH, date_now, ".csv", (byte*)complete_log, log_size, MAX_LOG_SIZE, 0 );
        }
        sprintf( recent_line, "%s,%d,%d,%d,%d,%d,%d\n",
                 time_now,recent[0],recent[1],recent[2],recent[3],recent[4],recent[5] );
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
  } // udp
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
    #ifdef WITH_NERDY_TIMESTAMP_DISPLAY
    unsigned short yyyy;
    byte mm, dd;
    char date[11];
    sscanf( epoch2date( timeClient.getEpochTime(), date ), "%d-%d-%d", &yyyy, &mm, &dd );
    unsigned long bits = yyyy<<16ul | mm<<8ul | dd;
    int y = 63 - PLOT_REDUCTION * 3;
    for ( int x=0;  x < 32;  ++x )
      if ( bits & 1ul << ( 31 - x ) )
        oled.drawPixel( x, y );
    byte HH=timeClient.getHours(), MM=timeClient.getMinutes(), SS=timeClient.getSeconds();
    bits = HH<<16ul | MM<<8ul | SS;
    for ( int x=0;  x < 24;  ++x )
      if ( bits & 1ul << ( 23 - x ) )
        oled.drawPixel( x + 32, y );
    #ifdef LSB_MARKERS_FOR_NERDY_TIMESTAMP
    y = y==63 ? --y : ++y;  // below timestamp, if possible, otherwise above
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
      }
    client.stop( );
    Serial.println( );
  }
  //-
  update_server.handleClient( );
}
