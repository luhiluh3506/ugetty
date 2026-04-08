// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#pragma once

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>

// Bounded buffer string-building API.
//
// All functions take a write cursor `d` and a remaining-space counter `n`:
//   - `*d` points to the next byte to write (the current null terminator).
//   - `*n` is the number of bytes remaining, including the null terminator
//     slot at `*d`. Invariant: `*n >= 1` and `**d == '\0'` after every call.
//
// On success, `*d` is advanced past the written bytes, `*n` is decremented
// accordingly, and the byte count written (excluding the null) is returned.
// The buffer is always left null-terminated at `*d`.
//
// On overflow (not enough space), -1 is returned and the buffer state is
// unmodified (the existing null terminator is preserved).
//
// Typical usage:
//   char buf[256];
//   char *d = buf;
//   size_t n = sizeof(buf);
//   bnstrcat(&d, &n, "hello ");
//   bnstrl(&d, &n, some_int);
//   write(fd, buf, strlen(buf));

// Append the literal bytes given as a comma-separated list.
#define bnchrcat(D, N, ...) bnmemcat((D), (N), (char[]){__VA_ARGS__, 0}, sizeof((char[]){__VA_ARGS__, 0}))

// Append an embedded null byte (advances past it). Use to build multi-string
// buffers (e.g. for argv or environment). Unlike bnchr('\0'), this advances
// the cursor so subsequent writes appear after the embedded NUL.
ssize_t bnnul(char **d, size_t *n);

// Append a single character and maintain the null terminator. If `c` is NUL,
// writes only the terminator (no advance). Returns -1 if `*n < 2`.
ssize_t bnchr(char **d, size_t *n, char c);

// Append the decimal representation of an unsigned/signed 64-bit integer.
ssize_t bnstrull(char **d, size_t *n, uint64_t v);
ssize_t bnstrll(char **d, size_t *n, int64_t v);

// Append the decimal representation of an unsigned/signed 32-bit integer.
ssize_t bnstrul(char **d, size_t *n, uint32_t v);
ssize_t bnstrl(char **d, size_t *n, int32_t v);

// Append the lowercase hex representation of `x`. Leading zeros are
// suppressed down to `width` digits (clamped to 1–8). For example,
// bnhex(&d, &n, 0xAB, 4) writes "00ab".
ssize_t bnhex(char **d, size_t *n, uint32_t x, int width);

// Append `len` raw bytes from `s`. Does *not* write a null terminator.
// The buffer is not a valid C string until a null is written by a subsequent
// call (e.g. bnchr, bnstrcat, or explicitly via *d[0] = '\0').
ssize_t bnmemcat(char **d, size_t *n, const char *s, size_t len);

// Append the null-terminated string `s` (without its null). The buffer
// remains null-terminated at `*d`.
ssize_t bnstrcat(char **d, size_t *n, const char *s);

// Append a formatted string using vsnprintf. Pulls in printf bloat,
// prefer the typed bn* functions in size-constrained builds.
ssize_t bnprintf(char **d, size_t *n, const char *format, ...);
