// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#pragma once

int io_buffered(int read_fd, int write_fd, int io_fd);
int proxy_io(int read_fd, int write_fd, int io_fd);
