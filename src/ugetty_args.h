// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BAUD_OKAY 0
#define BAUD_INVAL -1
#define BAUD_ERROR -2
#define BAUD_ISERR(c) ((c) == BAUD_ERROR)

struct args {
  dev_t tty_rdev;
  int32_t timeout;
  int32_t baud_rate;
  const char *issue_file;
  const char *login_program;
  const char *pppd_program;
  char tty[256];
  const char *termtype;
  char **pppd_args;

  uint16_t ssh_port;

  bool flow_control;
  bool local_line;
  bool skip_login;
  bool wait_cr;
  bool noissue;
  bool enable_ppp;
  bool enable_ssh;
};

#define DEFAULT_SSH_PORT 22
#define DEFAULT_TERMTYPE "vt100"
#define DEFAULT_ISSUE_FILE "/etc/issue"
#define DEFAULT_LOGIN "/bin/login"
#define DEFAULT_PPPD "/usr/sbin/pppd"

#define PPP_PEERS_PREFIX "/etc/ppp/peers/"

int lookup_baud(int32_t baud_rate);
int parse_args(int argc, char *argv[], struct args *args);
