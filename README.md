# BSBmonCR
**BSB-LAN monitor by user -cr**

<img src="BSBmonCR.gif" size="400%">

Monitoring some heating related parameters via [BSB-LAN](https://github.com/fredlcore/bsb-lan),
using an esp32 board (optionally) fitted with an ssd1306 display.
<br>
*(Inspired by https://forum.fhem.de/index.php/topic,110599.0.html)*

<img alt="Creative Commons License" style="border-width:0"
     src="https://i.creativecommons.org/l/by-nc-sa/4.0/88x31.png" />
This work is licensed under a
<a rel="license" href="http://creativecommons.org/licenses/by-nc-sa/4.0/">
  Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License</a>.

## How To Use

Put the files from the BSB-LAN sub-directory into your BSB-LAN sketch directory
and don't forget to put '#define CUSTOM_COMMANDS' into BSB_LAN_config.h,
then (re-) compile BSB-LAN.ino for the unit attached to your BSB.
(I've been using BSB-LAN Version 2.1.3-20220209235153, btw.)

Edit BSBmonCR.ino here to include your WLAN name and password,
then compile BSBmonCR.ino for your OLED fitted esp32 to be used as a monitor.

(You may also have/want to adjust the BSB parameters and values to fit your
heating system's configuration. Look for the 'switch' statement in
BSBmonCR.ino and also adapt BSB-LAN/BSB_LAN_custom_global.h accordingly.)

If all goes well, you should see data from your BSB-LAN system displayed
on the BSBmonCR unit. You should also be able to display its screen contents
(even without an ssd1306 display attached) on any web browser in your network
by loading http://\<your-BSBmonCR-address\>/

## Display Contents

Outside, room and water temperature (current value and preceding 24 h plot)
are displayed.
There's also a thin line below the temperature plots, showing when the boiler
was active.
The house/water symbol is filled when the house/water is being heated.
  
Upon power-up, the monitor will display placeholder values for the
temperatures, which should within seconds be replaced by values from your
BSB-LAN unit.
The plots on the right will take at least 20 minutes to begin to show,
longer when the average temperature in the first 20 minutes is the same
as in the currently running 20 minutes. Vertically, the plots scale
automatically to fully use the limited space available. They will look best
once the first 24 hours have elapsed and the plots take up their whole width.
  
### Bonus Display
  
You can add a presence check for computer systems (e.g. cell phones, which
roughly translates to people ;) in your network. Note that for simplicity
reasons this has been implemented using ping, which is not the most
reliable method. If you are willing to sacrifice some display space for
this feature, '#define ADDR_TO_CHECK' in BSBmonCR.ino. 
If activated, each of the systems monitored will take up a single pixel
row at the bottom of the screen, plus 0-2 empty lines above them.

## Code Comments / Warnings
  
* There's minimal error handling in some cases.
* HTTP handling is pragmatic, not polite.
* In several/most places, screen coordinates are hard-coded for a 128*64
  display, not calculated to fit a screen with possibly differing dimensions.
