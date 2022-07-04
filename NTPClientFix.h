/*
 * BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
 * using esp32 with (optional) ssd1306 display
 * (file NTPClientFix.h)
 */

// NTPClient sometimes returns bogus values (date=1970/2036).
// We try to fix this here by reverting its most recent update, if necessary.
// Note: This works only for a single NTPClient instance!

#define private public // hack to access NTPClient's internal state variables
#include <NTPClient.h>
#undef private

unsigned long previousEpoch = 0;
unsigned long prevEpochUpdate = 0;

void NTPClientFix( NTPClient &ntp ) {
  if ( ntp._currentEpoc > 50 * 365ul * 24 * 60 * 60 &&  // year ca. 2020
       ntp._currentEpoc < 65 * 365ul * 24 * 60 * 60 ) { // year ca. 2035
    // epoch seems legit, use it as backup
    previousEpoch = ntp._currentEpoc;
    prevEpochUpdate = ntp._lastUpdate;
  } else {
    // bogus epoch, restore from backup!
    #ifdef DEBUG_NTPClient
    Serial.print( "NTPClient._currentEpoc:" );
    Serial.println( ntp._currentEpoc );
    #endif
    // let's just hope we've already had valid values...
    ntp._currentEpoc = previousEpoch;
    ntp._lastUpdate = prevEpochUpdate;
  }
}
