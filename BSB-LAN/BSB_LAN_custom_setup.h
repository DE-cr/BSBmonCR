// Add custom code for setup function here which will be included at the end of the function

/*
 * BSB-LAN monitor
 * by user -cr at forum.fhem.de
 * using esp32 with ssd1306 display
 * inspired by https://forum.fhem.de/index.php/topic,110599.0.html
 * (file BSB_LAN_custom_setup.h)
 */

memset( udp_bsb_buf, 0, UDP_BSB_BUFSIZE );
