/*
 * BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
 * using esp32 with (optional) ssd1306 display
 * (file BSBmonCR_config.h)
 */

//- the first three parts of your network address, comma (!) separated:
#define NETWORK 192,168,178

//- BSB parameters:
#define OUTSIDE_TEMPERATURE 8700
#define ROOMS_TEMPERATURE   8770
#define WATER_TEMPERATURE   8830
#define HEATING_STATUS      8001
#define WATER_STATUS        8003
#define BOILER_STATUS       8005
#define BUFFER_TEMPERATURE  8980 // optional, comment out if not available
