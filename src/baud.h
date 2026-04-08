// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#pragma once

#include <termios.h>

static const struct { int baud; speed_t speed; } BAUD_TAB[] = {
  {0, B0},
  {50, B50},
  {75, B75},
  {110, B110},
  {134, B134},
  {150, B150},
  {200, B200},
  {300, B300},
  {600, B600},
  {1200, B1200},
  {1800, B1800},
  {2400, B2400},
  {4800, B4800},
  {9600, B9600},
#if defined(B19200)
  {19200, B19200},
#elif defined(EXTA)
  {19200, EXTA},
#endif
#if defined(B38400)
  {38400, B38400},
#elif defined(EXTB)
  {38400, EXTB},
#endif
#if defined(B57600)
  {57600, B57600},
#endif
#if defined(B115200)
  {115200, B115200},
#endif
#if defined(B230400)
  {230400, B230400},
#endif
#if defined(B460800)
  {460800, B460800},
#endif
#if defined(B500000)
  {500000, B500000},
#endif
#if defined(B576000)
  {576000, B576000},
#endif
#if defined(B921600)
  {921600, B921600},
#endif
#if defined(B1000000)
  {1000000, B1000000},
#endif
#if defined(B1152000)
  {1152000, B1152000},
#endif
#if defined(B1500000)
  {1500000, B1500000},
#endif
#if defined(B2000000)
  {2000000, B2000000},
#endif
#if defined(B2500000)
  {2500000, B2500000},
#endif
#if defined(B3000000)
  {3000000, B3000000},
#endif
#if defined(B3500000)
  {3500000, B3500000},
#endif
#if defined(B4000000)
  {4000000, B4000000},
#endif
  {-1, 0}
};
