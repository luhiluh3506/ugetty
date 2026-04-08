#!/bin/bash
set -uo pipefail
trap 's=$?; printf "%s: Error on line %s: %s\n" "" $LINENO "$BASH_COMMAND"; exit $s' ERR
IFS=$'\n\t'
# end 'strict mode' preamble

cat <<EoF
// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#pragma once

#include <termios.h>

static const struct { int baud; speed_t speed; } BAUD_TAB[] = {
EoF

for baud in $(grep -rE '^#define\s+B(0|[1-9][0-9]*)\s+' /usr/include/ | awk '{print$2}' | sed 's/B//' | sort -un);
do
  if [ $baud -le 9600 ]; then
    printf '  {%d, B%d},\n' $baud $baud
  else
    printf '#if defined(B%d)\n  {%d, B%d},\n' $baud $baud $baud
    if [ $baud -eq 19200 ]; then
      printf '#elif defined(EXTA)\n  {%d, EXTA},\n' $baud
    elif [ $baud -eq 38400 ]; then
      printf '#elif defined(EXTB)\n  {%d, EXTB},\n' $baud
    fi
    printf '#endif\n'
  fi
done
printf '  {-1, 0}\n};\n'
