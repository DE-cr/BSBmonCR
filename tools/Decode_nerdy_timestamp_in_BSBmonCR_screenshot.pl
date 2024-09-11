#!/usr/bin/perl -w \

# BSB-LAN monitor (https://github.com/DE-cr/BSBmonCR)
# using esp32 with (optional) ssd1306 display
# (file Decode_nerdy_timestamp_in_BSBmonCR_screenshot.pl)

use strict;
use warnings;

undef $/;       # slurp mode, i.e. read whole files at once
binmode STDIN;  # for MS Windows :/ (untested!)

~<> =~          # read file (<>), bit flip (~) and match (=~) to ...
  /(                  # capture these bytes to $1
     [\x14]           # year: 20..
     [\x18-\x20]      # year: ..24-32 (epoch year could be 1970-2106)
     [\x01-\x0c]      # month: 1-12
     [\x01-\x1f]      # day: 1-31
     [\0-\x17]        # hour: 0-23
     [\0-\x3b]{2}     # minute,second: 0-59
   )/x;
# Note: We're using the first matching byte sequence, although theoretically
# that pattern could also apply to a different part of the image. Since we're
# reading from the bottom of the image left to right, that could only be
# in one of the presence check log lines, if those are enabled in config.h.
# However, due to the date items' value limitations, an erroneous match is
# unlikely.

printf "%2d%02d-%02d-%02dT%02d:%02d:%02d\n",  # use ISO format for printing
  unpack"C*", $1;  # get bytes from pattern found
