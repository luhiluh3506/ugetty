// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#define _GNU_SOURCE

#define _STR(X) #X
#define STR(X) _STR(X)

#include <string.h>

#include "bnprintf.h"

static const char hex_lc[] = {
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

static ssize_t _bnchr(char **d, size_t *n, char c) {
  if (*n > 1) {
    (*d)[0] = c;
    --*n;
    ++*d;
    return 1;
  } else {
    return -1;
  }
}

static ssize_t _bnterm(char **d, size_t *n) {
  if (*n > 0) {
    (*d)[0] = '\0';
    return 0;
  } else {
    return -1;
  }
}

ssize_t bnnul(char **d, size_t *n) {
  if (*n > 1) {
    (*d)[0] = 0;
    --*n;
    ++*d;
    return 1;
  } else {
    return -1;
  }
}

ssize_t bnchr(char **d, size_t *n, char c) {
  if (c == 0) {
    return _bnterm(d, n);
  } else {
    ssize_t r = _bnchr(d, n, c);
    _bnterm(d, n);
    return r;
  }
}

ssize_t bnutf8(char **d, size_t *n, uint32_t cp) {
  if (cp == 0) {
    return _bnterm(d, n);
  } else if (cp < 0x80 && *n > 1) {
    (*d)[0] = cp & 0x7F;
    *n -= 1; *d += 1; return 1;
  } else if (cp < 0x800 && *n > 2) {
    (*d)[0] = 0xC0 | ((cp >> 6) & 0x1F);
    (*d)[1] = 0x80 | (cp & 0x3F);
    *n -= 2; *d += 2; return 2;
  } else if (cp >= 0xD800 && cp <= 0xDFFF) {
    // Reject UTF-16 surrogate codepoints.
    return -1;
  } else if (cp < 0x10000 && *n > 3) {
    (*d)[0] = 0xE0 | ((cp >> 12) & 0x0F);
    (*d)[1] = 0x80 | ((cp >> 6) & 0x3F);
    (*d)[2] = 0x80 | (cp & 0x3F);
    *n -= 3; *d += 3; return 3;
  } else if (cp < 0x110000 && *n > 4) {
    (*d)[0] = 0xF0 | ((cp >> 18) & 0x07);
    (*d)[1] = 0x80 | ((cp >> 12) & 0x3F);
    (*d)[2] = 0x80 | ((cp >> 6) & 0x3F);
    (*d)[3] = 0x80 | (cp & 0x3F);
    *n -= 4; *d += 4; return 4;
  } else {
    return -1;
  }
}

// Under -Os, this would call a helper function to handle the division, so we
// ask the compiler to optimize it as if -O2 were passed.
#pragma GCC push_options
#pragma GCC optimize("O2")
ssize_t bnstrull(char **d, size_t *n, uint64_t v) {
  char digits[21];
  char *c = digits + sizeof(digits) - 1;
  *c = '\0';
  do {
    *(--c) = '0' + (v % 10);
    v /= 10;
  } while (v > 0);
  return bnstrcat(d, n, c);
}

ssize_t bnstrul(char **d, size_t *n, uint32_t v) {
  char digits[11];
  char *c = digits + sizeof(digits) - 1;
  *c = '\0';
  do {
    *(--c) = '0' + (v % 10);
    v /= 10;
  } while (v > 0);
  return bnstrcat(d, n, c);
}
#pragma GCC pop_options


ssize_t bnstrll(char **d, size_t *n, int64_t v) {
  if (v >= 0) {
    return bnstrull(d, n, v);
  } else if (v == INT64_MIN) {
    return bnstrcat(d, n, "-9223372036854775808");
  } else if (*n > 2) {
    char *dx = *d + 1;
    size_t nx = *n - 1;
    ssize_t r = bnstrull(&dx, &nx, -v);
    if (r > 0) {
      (*d)[0] = '-';
      *d = dx; *n = nx;
      return r + 1;
    } else {
      return r;
    }
  } else {
    return -1;
  }
}

ssize_t bnstrl(char **d, size_t *n, int32_t v) {
  if (v >= 0) {
    return bnstrul(d, n, v);
  } else if (v == INT32_MIN) {
    return bnstrcat(d, n, "-2147483648");
  } else if (*n > 2) {
    char *dx = *d + 1;
    size_t nx = *n - 1;
    ssize_t r = bnstrul(&dx, &nx, -v);
    if (r > 0) {
      (*d)[0] = '-';
      *d = dx; *n = nx;
      return r + 1;
    } else {
      return r;
    }
  } else {
    return -1;
  }
}

ssize_t bnhex(char **d, size_t *n, uint32_t x, int width) {
  size_t start = *n;
  width = 8 - width;

  for (int shift = 28; shift >= 0; shift -= 4) {
    uint32_t digit = x >> shift;
    if (digit > 0 || --width < 0) {
      if (bnchr(d, n, hex_lc[digit & 15]) < 0) return -1;
    }
  }

  return start - *n;
}

ssize_t bnmemcat(char **d, size_t *n, const char *s, size_t len) {
  if (len >= *n) return -1;
  memcpy(*d, s, len);
  *n -= len;
  *d += len;
  return len;
}

ssize_t bnstrcat(char **d, size_t *n, const char *s) {
  // length excluding null byte
  size_t len = strnlen(s, *n);
  if (len == *n) return -1;
  memcpy(*d, s, len + 1);
  *n -= len;
  *d += len;
  return len;
}

ssize_t bnprintf(char **d, size_t *n, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  // length excluding null byte
  ssize_t len = vsnprintf(*d, *n, format, ap);
  va_end(ap);
  if (len < 0 || (size_t)len >= *n) return -1;
  *n -= len;
  *d += len;
  return len;
}
