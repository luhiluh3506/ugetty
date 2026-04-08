// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#define _GNU_SOURCE

#include <errno.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/types.h>

#include "util.h"

ssize_t writeall(int fd, const void *_buf, size_t n) {
  ssize_t ret;
  const uint8_t *buf = _buf;
  const uint8_t *end = buf + n;

  // Write until done or error.
  while (end > buf) {
    if ((ret = write(fd, buf, end - buf)) < 0) {
      // Retry on interrupts.
      if (errno == EINTR) { continue; }
      return ret;
    }

    buf += ret;
  }

  return n;
}
