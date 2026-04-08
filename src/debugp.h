// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#pragma once

#ifndef NDEBUG
#include <stdarg.h>
#include <stddef.h>
// void debugp(const char *format, ...);
#define debugp(...) _debugp(__FILE__, __func__, __LINE__, __VA_ARGS__)
void _vdebugp(
  const char *file, const char *func, unsigned int line,
  char *modified_fmt, size_t n,
  const char *fmt, va_list args
);
void _debugp(
  const char *file, const char *func, unsigned int line,
  const char *fmt, ...
);
#else
// no-op
#define debugp(...) do {} while (0)
#endif
