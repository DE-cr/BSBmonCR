#!/usr/bin/perl -w \

# BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
# using esp32 with (optional) ssd1306 display
# (file bsb-mon_decode.pl)

use strict;
use warnings;

undef $/;  # slurp mode
open STDIN, "wget -qO- bsb-mon|" if -t and !@ARGV;
while (<>) {
  ($_ = ~$_) =~  # bit flip bitmap
    /(                  # capture these bytes to $1
       (                # epoch year can be 1970-2106:
        \x07[\xb2-\xff] # 1970-2047
        |               # or
        \x08[\0-\x3a]   # 2048-2106
       )
       [\x01-\x0c]      # month: 1-12
       [\x01-\x1f]      # day: 1-31
       [\0-\x17]        # hour: 0-23
       [\0-\x3b]{2}     # minute,second: 0-59
     )/x;
  printf "%04d-%02d-%02d %02d:%02d:%02d", unpack"nC*", $1;
  s/.{130}//s;  # remove bmp header
  my @p = reverse map { unpack "B*" } /(.{16})/gs;  # -> lines array
  if (substr($p[0],0,56) !~ /^0.+1.+0$/) {          # pv values available?
    my $kwh = count(scalar reverse substr($p[0],1,55)) / 10;
    my $w = count(join "", map { substr($_,0,1) } @p[0..59]) * 10;
    print " / $w W / $kwh kWh";
  }
  my $c = join "", reverse map { substr($_,56,1) } @p;
  if ($c =~ /^111111111/) {  # buffer values available?
    # assumes BUFFER_TEMP_OFFSET==0 in config.h
    $c = count($c);
    print " / $c °C";
  }
  print "\n";
}

sub count {
  (local $_ = shift) =~ s/0+$//;
  length
}
