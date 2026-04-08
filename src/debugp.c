// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#define _GNU_SOURCE

#include "debugp.h"

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
void _vdebugp(
  const char *file, const char *func, unsigned int line,
  char *modified_fmt, size_t n,
  const char *fmt, va_list args
) {
  if (fmt == NULL) fmt = "(null)";
  int saved_errno = errno;
  // ANSI SGR escape code parameters, e.g. `31` for red
  // https://en.wikipedia.org/wiki/ANSI_escape_code#SGR_.28Select_Graphic_Rendition.29_parameters
  char *color = getenv("DEBUGP_COLOR");
  int fd = STDERR_FILENO;

  // copy format *without* null terminator
  memcpy(modified_fmt, fmt, n);

  // add null terminator, stripping newline if present
  modified_fmt[n-(n > 0 && modified_fmt[n-1] == '\n' ? 1 : 0)] = '\0';
  // set color if supplied via environment
  if (color != NULL) dprintf(fd, "\033[%sm", color);
  // line header
  dprintf(fd, "%s(%s:%u,%d): ", file, func, line, saved_errno);
  // actual content
  vdprintf(fd, modified_fmt, args);
  // reset color if required, and print and ending newline
  dprintf(fd, color != NULL ? "\033[0m\n" : "\n");
  fdatasync(fd);
  errno = saved_errno;
}

void _debugp(
  const char *file, const char *func, unsigned int line,
  const char *fmt, ...
) {
  va_list args;
  va_start(args, fmt);

#if __STDC_VERSION__ >= 199901L
  size_t n;
  if (__builtin_constant_p(strlen(fmt))) {
    n = strlen(fmt);
  } else {
    n = strnlen(fmt, 65535);
  }
  char modified_fmt[n + 1];
#else
  char modified_fmt[65536];
  size_t n = strnlen(fmt, sizeof(modified_fmt) - 1);
#endif

  _vdebugp(file, func, line, modified_fmt, n, fmt, args);
}
#pragma GCC diagnostic pop
