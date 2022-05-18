// Add custom code for loop function here which will be included at the end of the function

/*
 * BSB-LAN monitor
 * by user -cr at forum.fhem.de
 * using esp32 with ssd1306 display
 * inspired by https://forum.fhem.de/index.php/topic,110599.0.html
 * (file BSB_LAN_custom.h)
 */

curr_ms = millis( );
if ( curr_ms - prev_ms > 2000 || curr_ms < prev_ms ) {
  // every 2s (or when millis() overflows)
  if ( i_udp_bsb_param >= N_UDP_BSB_PARAMS )
    i_udp_bsb_param = 0; 
  query( udp_bsb_params[i_udp_bsb_param] );
  // remove decimal point (in copy) if required ("12.3" => "123"):
  strncpy( udp_bsb_buf, decodedTelegram.value, UDP_BSB_BUFSIZE-1);
  remove_char( udp_bsb_buf, '.' );
  sprintf( udp_bsb_buf, "%i:%i", udp_bsb_params[i_udp_bsb_param], atoi(udp_bsb_buf) );
  udp.beginPacket( broadcast_ip, UDP_PORT );
  udp.print( udp_bsb_buf );
  udp.endPacket( );
  ++i_udp_bsb_param;
  prev_ms = curr_ms;
}
