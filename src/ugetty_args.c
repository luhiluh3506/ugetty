// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#define _GNU_SOURCE

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "ugetty_args.h"

#include "baud.h"
#include "bnprintf.h"
#include "util.h"

ALWAYS_INLINE ssize_t strwrite(const char *s) {
  return writeall(STDOUT_FILENO, s, strlen(s));
}

int lookup_baud(int32_t baud_rate) {
  for (int ref, i = 0; (ref = BAUD_TAB[i].baud) >= 0; ++i) {
    if (ref == baud_rate) { return BAUD_TAB[i].speed; }
  }

  return -1;
}

void print_usage(void) {
  strwrite(
    "Usage: ugetty [OPTIONS] BAUD_RATE TTY [TERMTYPE]\r\n"
    "\r\n"
    "Open TTY, look for terminal, PPP, or SSH, then prompt for username and\r\n"
    "invoke `login`, `pppd`, or an SSH connection as appropriate.\r\n"
    "\r\n"
    "  -h               Enable hardware flow control\r\n"
    "  -L               Ignore carrier detect state\r\n"
    "  -n               Do not prompt for username\r\n"
    "  -w               Wait for CR or LF before prompting for username\r\n"
    "  -i               Don't display issue file\r\n"
    "  -t SEC           Timeout for username prompt\r\n"
    "  -f ISSUE_FILE    Alternate issue file\r\n"
    "  -l LOGIN         Alternate `login` binary\r\n"
    "  -P [PPPD]        Enable PPP support. Optionally specify `pppd` binary.\r\n"
    "  -S [PORT]        Enable SSH proxy support. Optionally specify port.\r\n"
    "\r\n"
    "BAUD_RATE of \"0\" leaves it unchanged\r\n"
  );
}

int parse_args(int argc, char *argv[], struct args *args) {
  memset(args, 0, sizeof(*args));
  args->baud_rate = -1;
  int32_t ssh_port_or_baud = 0;

  char optst = '\0';

  for (int n = 1; n < argc; ++n) {
    char *arg = argv[n];
    int i = 0;

    if (optst == '\0' && arg[0] == '-') {
      if (arg[1] == '-' && arg[2] == '\0') {
        if (!args->enable_ppp) {
          // `--` is an error without `-P`.
          optst = '\xFF';
          goto args_done;
        }
        // Replace the `--` argument and start our list there.
        argv[n] = "pppd";
        args->pppd_args = &argv[n];
        // We're done parsing arguments.
        break;
      }

      while (arg[++i] != '\0') {
        switch (arg[i]) {
          case 'h': args->flow_control = true; break;
          case 'L': args->local_line = true; break;
          case 'n': args->skip_login = true; break;
          case 'w': args->wait_cr = true; break;
          case 'i': args->noissue = true; break;


          case 't':
          case 'f':
          case 'l':
            optst = arg[i++];
            if (arg[i] == '\0') {
              // Value is the next argv element.
              if (++n >= argc) {
                optst = '\xFF';
                goto args_done;
              }
              arg = argv[n];
              i = 0;
            }
            goto handle_value;


          case 'P':
            // We support an optional argument for `-P`.
            args->enable_ppp = true;
            if (arg[i+1] == '\0') {
              // Value, if it exists, is the next argv element.
              if (n + 1 >= argc) {
                goto args_done;
              // The value for `-P` needs to start with a `/`.
              } else if (argv[n+1][0] == '/') {
                // Prepare to handle the next argument.
                optst = arg[i];
                arg = argv[++n];
                i = 0;
                goto handle_value;
              }
            } else if (arg[i+1] == '/') {
              // Handle the rest of the argument.
              optst = arg[i++];
              goto handle_value;
            }
            break;


          case 'S':
            // We support an optional argument for `-S`
            args->enable_ssh = true;
            if (arg[i+1] == '\0') {
              // Value, if it exists, is the next argv element.
              if (n + 1 >= argc) {
                goto args_done;
              // The value for `-S` needs to start with `1` - `9`.
              } else if (argv[n+1][0] >= '1' && argv[n+1][0] <= '9') {
                // Prepare to handle the next argument.
                optst = arg[i];
                arg = argv[++n];
                i = 0;
                goto handle_value;
              }
            } else if (arg[i+1] >= '1' && arg[i+1] <= '9') {
              // Handle the rest of the argument.
              optst = arg[i++];
              goto handle_value;
            }
            break;


          default:
            optst = '\xFF';
            goto args_done;
        }
      }
      continue;
    }

handle_value:
    switch (optst) {
      case 't': {
        int timeout = 0;
        while (timeout <= 3600 && arg[i] >= '0' && arg[i] <= '9') {
          timeout = timeout * 10 + (arg[i++] - '0');
        }
        if (timeout > 3600 || arg[i] != '\0') {
          optst = '\xFF';
          goto args_done;
        }
        args->timeout = timeout;
        break;
      }
      case 'S':
        // NOTE: We parse values larger than 65535 because this might
        // actually be the baud rate.
        ssh_port_or_baud = 0;
        while (arg[i] >= '0' && arg[i] <= '9') {
          ssh_port_or_baud = ssh_port_or_baud * 10 + (arg[i++] - '0');
          if (ssh_port_or_baud > 999999999) {
            optst = '\xFF';
            goto args_done;
          }
        }
        if (arg[i] != '\0') {
          optst = '\xFF';
          goto args_done;
        }
        if (ssh_port_or_baud <= 65535) {
          args->ssh_port = ssh_port_or_baud;
          if (lookup_baud(ssh_port_or_baud) < 0) {
            // Not a baud rate, clear it.
            ssh_port_or_baud = 0;
          } else {
            // Ambigious, leave it for later.
          }
        } else if (lookup_baud(ssh_port_or_baud) < 0) {
          optst = '\xFF';
          goto args_done;
        }
        break;
      case 'f': args->issue_file = arg + i; break;
      case 'l': args->login_program = arg + i; break;
      case 'P': args->pppd_program = arg + i; break;
      case '\0':
        // Positional: baud rate, tty line, or term type.
        // Accepts both "BAUD_RATE TTY" and "TTY BAUD_RATE" orderings.
        if (args->baud_rate == -1 && arg[i] == '0' && arg[i+1] == '\0') {
          args->baud_rate = 0;
        } else if (args->baud_rate == -1 && arg[i] >= '1' && arg[i] <= '9') {
          int baud = 0;
          while (arg[i] >= '0' && arg[i] <= '9') {
            baud = baud * 10 + (arg[i++] - '0');
            if (baud > 99999999) {
              optst = '\xFF';
              goto args_done;
            }
          }
          if (arg[i] != '\0') {
            optst = '\xFF';
            goto args_done;
          }
          args->baud_rate = baud;
        } else if (args->tty[0] == '\0' || strcmp(arg, "auto") == 0) {
          // Get the tty name and strip the "/dev/" prefix.
          char *tty, tty_buf[sizeof(args->tty)];
          if (ttyname_r(STDIN_FILENO, tty_buf, sizeof(tty_buf)) != 0) {
            // XXX: Under what circumstances can this happen?
            return -1;
          }

          char devpfx[] = "/dev/";
          if (strncmp(tty_buf, devpfx, strlen(devpfx)) == 0) {
            tty = tty_buf + strlen(devpfx);
          } else {
            tty = tty_buf;
          }

          struct stat stt[] = {0};
          if (stat(tty_buf, stt) != 0) {
            return -1;
          }
          args->tty_rdev = stt->st_rdev;

          if (strcmp(arg, "auto") != 0) {
            // Validate that the detected tty and the argument
            // resolve to the same inode. Yes, there is a TOCTOU
            // race condition here, but we don't care because only
            // root should be able to change stuff under `/dev`.
            struct stat sta[] = {0};

            // Build the full device path.
            char buf[sizeof(args->tty)];
            char *d = buf;
            size_t n = sizeof(buf);
            bnstrcat(&d, &n, "/dev/");
            bnstrcat(&d, &n, arg);
            if (stat(buf, sta) != 0) {
              return -1;
            }

            if (stt->st_dev != sta->st_dev || stt->st_ino != sta->st_ino) {
              return -1;
            }
          }

          strcpy(args->tty, tty);
        } else if (args->termtype == NULL) {
          args->termtype = arg + i;
        } else {
          optst = '\xFF';
          goto args_done;
        }
        break;
      default:
        optst = '\xFF';
        goto args_done;
    }
    optst = '\0';
  }

  if (args->tty[0] == '\0') {
    // TTY is a required argument.
    //debugp("no tty argument");
    optst = '\xFF';
  }

  if (args->baud_rate == -1) {
    if (ssh_port_or_baud > 0) {
      args->baud_rate = ssh_port_or_baud;
      args->ssh_port = DEFAULT_SSH_PORT;
    } else {
      // Baud rate is a required argument.
      //debugp("no baud argument");
      optst = '\xFF';
    }
  }

  if (args->issue_file == NULL) {
    args->issue_file = DEFAULT_ISSUE_FILE;
  }

  if (args->termtype == NULL) {
    args->termtype = DEFAULT_TERMTYPE;
  }

  if (args->login_program == NULL) {
    args->login_program = DEFAULT_LOGIN;
  }

  if (args->enable_ppp && args->pppd_program == NULL) {
    args->pppd_program = DEFAULT_PPPD;
  }

  if (args->enable_ssh && args->ssh_port == 0) {
    args->ssh_port = DEFAULT_SSH_PORT;
  }

args_done:
  if (optst == '\0') {
    return 0;
  } else {
    print_usage();
    return -1;
  }
}
