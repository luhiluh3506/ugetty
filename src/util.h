// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#pragma once

#include <stdint.h>
#include <sys/types.h>

#define ALWAYS_INLINE static inline __attribute__((always_inline))
#define NEVER_INLINE __attribute__((noinline))

#define UNUSED(X) __attribute__((unused)) X

#define NITEMS(A) (sizeof((A)) / sizeof((A)[0]))

#define _STR(X) #X
#define STR(X) _STR(X)

ssize_t writeall(int fd, const void *buf, size_t n);
