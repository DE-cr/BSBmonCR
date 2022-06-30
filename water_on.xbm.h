/*
 * BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
 * using esp32 with (optional) ssd1306 display
 * (file water_on.xbm.h)
 */

#define water_on_width 20
#define water_on_height 20
static unsigned char water_on_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x80, 0x03, 0x00,
   0x80, 0x0b, 0x00, 0xc0, 0x1b, 0x00, 0xc0, 0x1b, 0x00, 0xe0, 0x3d, 0x00,
   0xe0, 0x3d, 0x00, 0xf0, 0x7e, 0x00, 0xf0, 0x7e, 0x00, 0x70, 0xff, 0x00,
   0x70, 0xff, 0x00, 0x60, 0xdf, 0x00, 0x40, 0xef, 0x00, 0x00, 0x7f, 0x00,
   0x00, 0x3e, 0x00, 0x00, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
