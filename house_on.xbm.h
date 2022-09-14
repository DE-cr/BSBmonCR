/*
 * BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
 * using esp32 with (optional) ssd1306 display
 * (file house_on.xbm.h)
 */

#define house_on_width 20
#define house_on_height 20
static unsigned char house_on_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x0f, 0x00, 0x80, 0x1f, 0x00,
   0xc0, 0x39, 0x00, 0xe0, 0x70, 0x00, 0x70, 0xe6, 0x00, 0x38, 0xcf, 0x01,
   0x9c, 0x9f, 0x03, 0xcc, 0x3f, 0x03, 0xe0, 0x7f, 0x00, 0xf0, 0xff, 0x00,
   0x30, 0xcf, 0x00, 0xf0, 0xff, 0x00, 0xf0, 0xff, 0x00, 0xf0, 0xf9, 0x00,
   0xf0, 0xf9, 0x00, 0xf0, 0xf9, 0x00, 0xfe, 0xff, 0x07, 0x00, 0x00, 0x00 };
