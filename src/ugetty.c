// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#define _GNU_SOURCE

#if __STDC_VERSION__ < 202000L
#include <stdalign.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/sysmacros.h> // for `makedev()`, `major()`, and `minor()`.
#include <sys/types.h>
#include <sys/utsname.h>

#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "baud.h"
#include "bnprintf.h"
#include "debugp.h"
#include "proxy_io.h"
#include "ugetty_args.h"
#include "util.h"

#define WAIT_ACTIVE 100
#define WAIT_PASSIVE 1500
#define WAIT_PROTO 10
#define WAIT_CPR ((WAIT_ACTIVE * 3) / 2)
#define WAIT_USERNAME 5000

#define PPP_DEV_PATH "/dev/ppp"
#define PPP_DEV_MAJOR 108
#define PPP_DEV_MINOR 0

#define PPP_FRAME    0x7e // PPP Framing character
#define PPP_STATION  0xff // "All Station" character
#define PPP_ESCAPE   0x7d // Escape Character
#define PPP_CONTROL  0x03 // PPP Control Field
#define PPP_LCP_HI   0xc0 // LCP protocol - high byte
#define PPP_LCP_LO   0x21 // LCP protocol - low byte
#define PPP_LCP_CR   0x01 // LCP code - Configure Request

#define ST_PPP_INIT  0x11
#define ST_PPP_FOUND 0x15
#define ST_SSH_INIT  0x21
#define ST_SSH_FOUND 0x2F

// Un-escape a byte
#define PPP_UNESCAPE(c) ((c) ^ 0x20)

#define READCHAR_AGAIN -1
#define READCHAR_ERROR -2
#define READCHAR_ISERR(c) ((c) == READCHAR_ERROR)

#define BAUD_OKAY 0
#define BAUD_INVAL -1
#define BAUD_ERROR -2
#define BAUD_ISERR(c) ((c) == BAUD_ERROR)

#define C0(c) ((c) & 0x1f)
#define C1(c) (((c) & 0x1f) | 0x80)

struct detect {
  uint8_t state;
  bool ppp_escaped;
};

typedef int (*tty_fn)(struct termios *, const struct args *);

static uint32_t djb2(const char *s) {
  uint32_t hash = 5381;
  unsigned char c;
  while ((c = (unsigned char)*s++)) {
    hash = hash * 33 + c;
  }

  return hash;
}

ALWAYS_INLINE uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return htonl((a << 24) | (b << 16) | (c << 8) | d);
}

// Under -Os, this would call a helper function to handle the division, so we
// ask the compiler to optimize it as if -O2 were passed.
#pragma GCC push_options
#pragma GCC optimize("O2")
static uint8_t mod_u8(uint32_t v, uint8_t n) {
  return v % n;
}
#pragma GCC pop_options

// This deterministically generates an IP address in the range of 127.0.0.2 to
// 127.255.255.254 for a tty device using its major and minor ID values and
// a simple non-cryptographic hash over its name. These are used as source IPs
// for connections to localhost so that the tty can be distinguished. Major
// IDs are 12 bits and minor IDs are 20 bits, but in practice small numbers
// are used for serial devices.
static void connect_src(const struct args *args, void *dst) {
  // This gives a range of 2 to 254
  uint8_t hash = mod_u8(djb2(args->tty), 253) + 2;
  // We truncate the major and minor device IDs to 8 bit values due to lack of
  // space, and them normally being < 256 anyway.
  dev_t rdev = args->tty_rdev;
  uint32_t ip = ipv4(127, major(rdev) & 255, minor(rdev) & 255, hash);

  struct sockaddr_in *sin = (struct sockaddr_in *)dst;
  sin->sin_family = AF_INET;
  sin->sin_port = 0;
  sin->sin_addr.s_addr = ip;
}

ALWAYS_INLINE ssize_t dstrwrite(int fd, const char *s) {
  // fail on null string
  if (!s) { return -1; }

  return writeall(fd, s, strlen(s));
}

ALWAYS_INLINE ssize_t strwrite(const char *s) {
  return dstrwrite(STDOUT_FILENO, s);
}

ALWAYS_INLINE ssize_t chrwrite(char c) {
  return writeall(STDOUT_FILENO, &c, 1);
}

static void alarm_handler(UNUSED(int sig)) {
  strwrite("\r\nTimed out.\r\n");
  // Signal handlers need to use `_exit()` instead of `exit()`.
  _exit(EXIT_FAILURE);
}

static int take_ownership(void) {
  pid_t pid = getpid();
  pid_t sid = getsid(0);
  pid_t pgrp = getpgrp();
  if (sid == -1) {
    return -1;
  } else if (pid != sid && pid != pgrp) {
    if (setsid() < 0) {
      return -1;
    }
  }

  // We continue if this fails.
  ioctl(STDIN_FILENO, TIOCSCTTY, 1);

  return 0;
}

static int set_tty(const struct args *args, tty_fn fn) {
  int r;
  // We use array syntax here so that `tty` can decay to a pointer.
  struct termios tty[] = {0};

  if (tcgetattr(STDIN_FILENO, tty) != 0) { return -1; }
  if ((r = fn(tty, args)) < 0) { return r; }
  if (tcsetattr(STDIN_FILENO, TCSANOW, tty) != 0) { return -1; }

  return r;
}

static int tty_8n1(struct termios *tty, UNUSED(const struct args *args)) {
  tty->c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
  tty->c_cflag |=   CS8;

  return 0;
}

static int tty_baud(struct termios *tty, const struct args *args) {
  int speed = lookup_baud(args->baud_rate);

  if (speed > 0 && cfsetspeed(tty, speed) < 0) {
    return BAUD_ERROR;
  } else if (speed < 0) {
    return BAUD_INVAL;
  }

  return BAUD_OKAY;
}

static int tty_raw(struct termios *tty, const struct args *args) {
  // Put the terminal into "raw" mode.
  cfmakeraw(tty);

  if (args->local_line) {
    tty->c_cflag |= CLOCAL;
  } else {
    tty->c_cflag &= ~CLOCAL;
  }

  if (args->flow_control) {
    tty->c_cflag |= CRTSCTS;
  } else {
    tty->c_cflag &= ~CRTSCTS;
  }

  return 0;
}

// Set flags based on `stty sane`.
static int tty_sane(struct termios *tty, const struct args *args) {
  // Input flags
  tty->c_iflag &= ~(IGNBRK | INLCR | IGNCR | IXANY);
  tty->c_iflag |=   BRKINT | ICRNL | IXON | IXOFF;
#if defined(IUCLC)
  tty->c_iflag &= ~IUCLC;
#endif
#if defined(IUTF8)
  // XXX `stty sane` clears this, but we may want to make it configureable
  tty->c_iflag &= ~IUTF8;
#endif

  // Output flags
  tty->c_oflag &= ~(NLDLY | CRDLY | TABDLY | BSDLY | VTDLY | FFDLY |
            OCRNL | OFILL | ONOCR | ONLRET | OFDEL);
  tty->c_oflag |=   OPOST | ONLCR;
#if defined(OLCUC)
  tty->c_oflag &= ~OLCUC;
#endif

  // Local flags
  tty->c_lflag &= ~(ECHONL | NOFLSH | TOSTOP | ECHOPRT | FLUSHO);
  tty->c_lflag |=   ICANON | IEXTEN | ECHO | ECHOE | ECHOK | ISIG | ECHOCTL | ECHOKE;
#if defined(XCASE)
  tty->c_lflag &= ~XCASE;
#endif
#if defined(EXTPROC)
  tty->c_lflag &= ~EXTPROC;
#endif

  // Control flags
  tty->c_cflag |=  CREAD | HUPCL;

  if (args->local_line) {
    tty->c_cflag |= CLOCAL;
  } else {
    tty->c_cflag &= ~CLOCAL;
  }

  if (args->flow_control) {
    tty->c_cflag |= CRTSCTS;
  } else {
    tty->c_cflag &= ~CRTSCTS;
  }

  // Set special characters to standard defaults
  tty->c_cc[VEOF]   = C0('D');
  tty->c_cc[VEOL]   = _POSIX_VDISABLE; // Disable
  tty->c_cc[VERASE] = 0x7F; // Backspace/Delete
  tty->c_cc[VINTR]  = C0('C');
  tty->c_cc[VKILL]  = C0('U');
  tty->c_cc[VQUIT]  = C0('\\');
  tty->c_cc[VSTART] = C0('Q');
  tty->c_cc[VSTOP]  = C0('S');
  tty->c_cc[VSUSP]  = C0('Z');

  return 0;
}

static int write_queued(void) {
  int queued;
  if (ioctl(STDOUT_FILENO, TIOCOUTQ, &queued) == -1) {
    return -1;
  }
  return queued;
}

static int readchar(int timeout_ms) {
  unsigned char c;

  struct pollfd fds[] = {{ .fd = STDIN_FILENO, .events = POLLIN }};
  int res = poll(fds, NITEMS(fds), timeout_ms);

  if (res < 0) {
    if (errno == EINTR) {
      // Interrupted by signal, try again.
      return READCHAR_AGAIN;
    }

    return READCHAR_ERROR;
  } else if (res == 0) {
    // Read timed out, try again.
    return READCHAR_AGAIN;
  }

  if (read(STDIN_FILENO, &c, 1) <= 0) {
    if (errno == EAGAIN) {
      // No data on a non-blocking read.
      errno = 0;
      return READCHAR_AGAIN;
    }
    // Read error or disconnect.
    return READCHAR_ERROR;
  } else {
    return c;
  }
}

#define DISPLAY_UTS(U, N) \
  strwrite((U)->N[0] != 0 ? (U)->N : "unknown[" #N "]")

static void display_banner(const struct args *args, const struct utsname *uts) {
  strwrite(
    "µgetty (built: " __DATE__ " " __TIME__ ")\r\n"
    "Written by Ryan (they/them) Castellucci - https://rya.nc/\r\n"
    "Protocols available: Login"
  );
  if (args->enable_ssh) { strwrite(" SSH"); }
  if (args->enable_ppp) { strwrite(" PPP"); }
  strwrite("\r\n\r\n");

  if (args->noissue) { return; }
  // We trust the content of `/etc/issue`.
  int fd = open(args->issue_file, O_RDONLY);
  if (fd < 0) {
    // Fail silently if we can't open the issue file.
    return;
  }

  char buf[512], *end = buf, *c = buf;
  ssize_t len = 0;

  for (int i = 0; i < 64; ++i) {
    // (Re)fill buffer if needed.
    if (++c >= end) {
      if ((len = read(fd, buf, sizeof(buf))) <= 0) { break; }
      c = buf; end = buf + len;
    }

    if (*c == '\n') {
      strwrite("\r\n");
    } else if (*c == '\\') {
      // Handle escape sequences.
      if (++c >= end) {
        if ((len = read(fd, buf, sizeof(buf))) <= 0) { break; }
        c = buf; end = buf + len;
      }

      switch (*c) {
#if defined(_GNU_SOURCE)
        case 'D': // DNS domain name
        case 'o':
        case 'O':
          DISPLAY_UTS(uts, domainname); break;
#endif
        case 'l': // tty line name
          strwrite(args->tty); break;
        case 'm': // machine architecture
          DISPLAY_UTS(uts, machine); break;
        case 'n': // node name (hostname)
        case 'h':
          DISPLAY_UTS(uts, nodename); break;
        case 'r': // kernel release
          DISPLAY_UTS(uts, release); break;
        case 's': // system name (os name)
          DISPLAY_UTS(uts, sysname); break;
        case 'v': // kernel version
          DISPLAY_UTS(uts, version); break;
        case '\\': // literal backslash
          chrwrite('\\'); break;
        default: // unrecognized escape, print literally
          chrwrite('\\');
          chrwrite(*c);
          break;
      }
    } else {
      chrwrite(*c);
    }
  }

  close(fd);
}

// Finite state machine based on mgetty's autoppp detection. Looks for the
// following sequence, based on RFC1662, with any of the bytes escaped:
//
// PPP_FRAME, PPP_STATION, PPP_CONTROL, PPP_LCP_HI, PPP_LCP_LO
static int protocol_state(int c, struct detect *detect) {
  unsigned char ppp_c;

  if (c == READCHAR_AGAIN) {
    detect->ppp_escaped = false;
    return (detect->state = 0);
  } else if (detect->ppp_escaped) {
    ppp_c = PPP_UNESCAPE(c);
    detect->ppp_escaped = false;
  } else {
    ppp_c = c;
  }

  if (ppp_c == PPP_ESCAPE) {
    detect->ppp_escaped = true;
  } else if (ppp_c == PPP_FRAME) { detect->state = ST_PPP_INIT;
  } else if (ppp_c == PPP_STATION && detect->state == ST_PPP_INIT + 0) { ++(detect->state);
  } else if (ppp_c == PPP_CONTROL && detect->state == ST_PPP_INIT + 1) { ++(detect->state);
  } else if (ppp_c == PPP_LCP_HI  && detect->state == ST_PPP_INIT + 2) { ++(detect->state);
  } else if (ppp_c == PPP_LCP_LO  && detect->state == ST_PPP_INIT + 3) {
    detect->state = ST_PPP_FOUND;
  } else if (c == 'S' && detect->state == 0) { detect->state = ST_SSH_INIT;
  } else if (c == 'S' && detect->state == ST_SSH_INIT + 0) { ++(detect->state);
  } else if (c == 'H' && detect->state == ST_SSH_INIT + 1) { ++(detect->state);
  } else if (c == '-' && detect->state == ST_SSH_INIT + 2) { ++(detect->state);
  } else if (c == '2' && detect->state == ST_SSH_INIT + 3) { ++(detect->state);
  } else if (c == '.' && detect->state == ST_SSH_INIT + 4) { ++(detect->state);
  } else if (c == '0' && detect->state == ST_SSH_INIT + 5) { ++(detect->state);
  } else if (c == '-' && detect->state == ST_SSH_INIT + 6) {
    detect->state = ST_SSH_FOUND;
  } else {
    detect->state = 0;
  }

  return detect->state;
}

// On entry, *c holds the first character. On return of 0, *c holds the first
// non-PPP character.
static int check_protocol(int *c) {
  struct detect detect[] = {0};

  for (;;) {
    protocol_state(*c, detect);
    if (detect->state == ST_PPP_FOUND) {
      return ST_PPP_FOUND;
    } else if (detect->state == ST_SSH_FOUND) {
      return ST_SSH_FOUND;
    } else if (detect->state == 0) {
      return 0;
    } else if (READCHAR_ISERR(*c = readchar(WAIT_PROTO))) {
      return -1;
    }
  }
}

// `pppd` will error out if `/dev/ppp` does not exist. This function makes a
// basic attempt to ensure it exists, and then washes its hands of the matter.
// Actual diagnostics are delegated to `pppd` in case of problems.
static void ensure_dev_ppp(void) {
  if (access(PPP_DEV_PATH, F_OK) == 0) {
    // We assume that if it exists, it's fine. `pppd` will do further checks,
    // and if it's wrong, that's weird enough that we shouldn't mess with it
    // ourselves.
    return;
  } else if (errno == ENOENT) {
    // There is a TOCTOU race here, which we don't care about. If `/dev/ppp` is
    // deleted after we check if it's there, `pppd` will fail, and `init` will
    // restart us. Regardless, `/dev/ppp` could still be deleted between when
    // we make sure it's there and when we start `pppd`. Also, if an attacker
    // can manipulate files within `/dev`, we have bigger problems.
    mode_t mode = S_IFCHR | 0600;
    dev_t dev = makedev(PPP_DEV_MAJOR, PPP_DEV_MINOR);
    if (mknod(PPP_DEV_PATH, mode, dev) == 0) {
      return;
    }
  } else {
    // An unexpected failure that we don't care about.
  }
}

// Create `/dev/ppp` if needed, claim the tty, and exec pppd.
// Only returns on error.
static int start_pppd(const struct args *args) {
  ensure_dev_ppp();

  if (take_ownership() != 0) {
    return EXIT_FAILURE;
  }

  char **pppd_args;
  char *default_args[] = {
    "pppd", "local",
    "noauth", "nodetach",
    "noip", "noipdefault",
    "+ipv6", "ipv6cp-use-persistent",
    NULL,
  };

  char peer_path[strlen(PPP_PEERS_PREFIX) + strlen("ugetty.") + strlen(args->tty) + 1];
  char *call_args[] = { "pppd", "call", peer_path + strlen(PPP_PEERS_PREFIX), NULL };

  if (args->pppd_args != NULL) {
    pppd_args = args->pppd_args;
  } else {
    // See if a peer file for µgetty exists.
    char *d = peer_path;
    size_t n = sizeof(peer_path);
    bnstrcat(&d, &n, PPP_PEERS_PREFIX);
    bnstrcat(&d, &n, "ugetty.");
    char *dot = d - 1;
    bnstrcat(&d, &n, args->tty);

    if (access(peer_path, F_OK) == 0) {
      pppd_args = call_args;
    } else {
      *dot = '\0';
      if (access(peer_path, F_OK) == 0) {
        pppd_args = call_args;
      } else {
        pppd_args = default_args;
      }
    }
  }

  // We use `execv()` to completely replace this µgetty process with
  // `pppd`, which inherits the open serial port.
  execv(args->pppd_program, pppd_args);

  return EXIT_FAILURE;
}

static int connect_ssh(const struct args *args) {
  int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0) { return -1; }

  struct sockaddr_in src;
  connect_src(args, &src);

  // Connecting to 127.255.255.254 will work for services bound to 0.0.0.0, but
  // not those bound to 127.0.0.1 or other specific IP addresses. This is a
  // guardrail to limit the risk of inadvertently exposing a service.
  struct sockaddr_in dst = {
    .sin_family = AF_INET,
    .sin_port = htons(args->ssh_port),
    .sin_addr = {ipv4(127, 255, 255, 254)}
  };

  if (bind(sock_fd, (struct sockaddr *)&src, sizeof(src)) != 0) {
    goto connect_ssh_err;
  }
  if (connect(sock_fd, (struct sockaddr *)&dst, sizeof(dst)) != 0) {
    goto connect_ssh_err;
  }

  // Replay the consumed portion of the client's identification string.
  dstrwrite(sock_fd, "SSH-2.0-");

  if (io_buffered(STDIN_FILENO, STDOUT_FILENO, sock_fd) == 0) {
    return EXIT_SUCCESS;
  } else if (errno == EPIPE) {
    return EXIT_SUCCESS;
  }

connect_ssh_err:
  close(sock_fd);
  return EXIT_FAILURE;
}

// Discard any cursor position reports (CPRs) starting with `c`.
// Yes, parsing terminal escape sequences requires timeouts. :-(
static int discard_cpr(int c) {
  int cpr_state = 0;
  for (;;) {
    if (cpr_state == 0 && c == '\033') {
      cpr_state = 1;
    } else if (cpr_state == 1 && c == '[') {
      cpr_state = 2;
    } else if (cpr_state >= 2 && cpr_state <= 3 && c >= '0' && c <= '9') {
      cpr_state = 3;
    } else if (cpr_state == 3 && c == ';') {
      cpr_state = 4;
    } else if (cpr_state >= 4 && cpr_state <= 5 && c >= '0' && c <= '9') {
      cpr_state = 5;
    } else if (cpr_state == 5 && c == 'R') {
      // Escape sequence complete.
      cpr_state = 0;
    } else {
      // This is not a cursor position report.
      return 0;
    }

    if (READCHAR_ISERR(c = readchar(WAIT_CPR))) {
      return -1;
    } else if (c == READCHAR_AGAIN) {
      // The timeout should be sufficient to avoid dropping non-escape
      // bytes in most cases, and in the worst case the user can hit the
      // key again.
      return 0;
    }
  }
}

static int get_username(
  const struct args *args,
  char *username, size_t username_sz
) {
  int c;
  char *p = username;
  char *end = username + username_sz - 1;
  struct detect detect[] = {0};

  for (;;) {
    if (READCHAR_ISERR(c = readchar(detect->state == 0 ? WAIT_USERNAME : WAIT_PROTO))) {
      return READCHAR_ERROR;
    }

    // Protocol detection timed out.
    if (c == READCHAR_AGAIN) {
      if (detect->state >= ST_SSH_INIT && detect->state < ST_SSH_FOUND) {
        // We were processing a potential SSH banner, display the typed
        // characters now.
        writeall(STDOUT_FILENO, "SSH-2.0", (detect->state - ST_SSH_INIT) + 1);
      } else if ((p - username) > 0) {
        // Signal `start_login()` to prompt again.
        return READCHAR_AGAIN;
      } else {
        // Reset state, and read again.
        protocol_state(c, detect);
        continue;
      }
    }

    protocol_state(c, detect);
    // We only check for a partial sequence here because the third character in
    // the ppp frame is ctrl-c which would trigger an exit.
    if (detect->state == ST_PPP_INIT + 2 && args->enable_ppp) {
      exit(start_pppd(args));
    } else if (detect->state == ST_SSH_FOUND && args->enable_ssh) {
      exit(connect_ssh(args));
    } else if (detect->state >= ST_SSH_INIT && detect->state < ST_SSH_FOUND) {
      continue;
    }

    if (c == READCHAR_AGAIN) {
      if ((p - username) > 0) {
        // Signal `start_login()` to prompt again.
        return READCHAR_AGAIN;
      } else {
        // Nothing typed, keep waiting.
        continue;
      }
    }

    switch (c) {
      case '\r':
      case '\n':
        *p = '\0';
        return p - username;
      case 0x7F:
      case C0('H'):
        if (p > username) {
          strwrite("\b \b");
          --p;
        }
        break;
      case C0('U'):
        while (p > username) {
          strwrite("\b \b");
          --p;
        }
        break;
      case C0('C'):
      case C0('D'):
        set_tty(args, tty_sane);
        exit(EXIT_SUCCESS);
      default:
        if (c >= ' ') {
          if (p < end) {
            write(STDOUT_FILENO, (char[]){c}, 1);
            *p++ = c;
          } else {
            // Username length limit reached, beep without echoing.
            write(STDOUT_FILENO, (char[]){'\a'}, 1);
          }
        }
    }
  }
}

// Set up the tty, display the banner, and exec login.
static int start_login(const struct args *args) {
  if (take_ownership() != 0) {
    return EXIT_FAILURE;
  }

  // If timeout is zero, the alarm won't actually be set.
  struct sigaction sa[] = {{ .sa_handler = alarm_handler }};
  sigaction(SIGALRM, sa, NULL);
  alarm(args->timeout);

  struct utsname uts[] = {0};
  if (uname(uts) != 0) {
    memset(uts, 0, sizeof(*uts));
  }

login_display_banner:
  display_banner(args, uts);

  char username_buf[UT_NAMESIZE+1];
  char *username = username_buf;

  if (args->skip_login) {
    username = NULL;
  } else {
    for (;;) {
      int r;
      if (uts->nodename[0] != 0) {
        strwrite(uts->nodename);
        chrwrite(' ');
      }
      strwrite("login: ");
      // NOTE: This may start a ppp or ssh session without returning.
      if ((r = get_username(args, username_buf, sizeof(username_buf))) > 0) {
        strwrite("\r\n");
        break;
      } else if (r == READCHAR_AGAIN) {
        // Timed out, reset the terminal and display the banner again.
        if (strwrite("\033c") < 0) {
          return EXIT_FAILURE;
        }
        goto login_display_banner;
      } else if (r < 0) {
        return EXIT_FAILURE;
      }
      strwrite("\r\n");
    }
  }

  alarm(0);

  if (set_tty(args, tty_sane) != 0) {
    return EXIT_FAILURE;
  }

  // Build environment with only `TERM` set, `login` will populate the rest.
  char termenv[strlen("TERM=") + strlen(args->termtype) + 1];
  char *d = termenv;
  size_t n = sizeof(termenv);
  bnstrcat(&d, &n, "TERM=");
  bnstrcat(&d, &n, args->termtype);

  // Setup the execve parameters.
  char *login_argv[] = {"login", "--", username, NULL};
  char *login_envp[] = {termenv, NULL};

  // Replace this process with a login prompt.
  execve(args->login_program, login_argv, login_envp);

  return EXIT_FAILURE;
}

static int ugetty(int argc, char *argv[]) {
  int c, res;

  struct args args[] = {0};
  if (parse_args(argc, argv, args) != 0) {
    return EXIT_FAILURE;
  }

  // do/while block for scoping
  do {
    size_t n = 512;
    // We "leak" this on purpose, it needs to live as long as the process.
    char *buf = mmap(NULL, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (buf == MAP_FAILED) { return EXIT_FAILURE; }
    char *d = buf;

    bnstrcat(&d, &n, argv[0]);
    bnnul(&d, &n);
    bnstrul(&d, &n, args->baud_rate);
    bnnul(&d, &n);
    bnstrcat(&d, &n, args->tty);
    bnnul(&d, &n);
    bnstrcat(&d, &n, args->termtype);
    bnnul(&d, &n);

    // Failures here are silently ignored, since setting the process arguments
    // is a non-fatal error and there's nothing that can be done about it.
    prctl(PR_SET_MM, PR_SET_MM_ARG_START, (unsigned long) buf, 0, 0);
    prctl(PR_SET_MM, PR_SET_MM_ARG_END, (unsigned long) d, 0, 0);
  } while(0);

  // Drain and flush i/o queues.
  if (tcdrain(STDIN_FILENO) != 0) {
    return EXIT_FAILURE;
  }
  if (tcflush(STDIN_FILENO, TCIOFLUSH) != 0) {
    return EXIT_FAILURE;
  }
  // Set up raw mode at specified baud
  if (set_tty(args, tty_8n1) != 0) {
    return EXIT_FAILURE;
  }
  if ((res = set_tty(args, tty_baud)) != 0) {
    if (res == BAUD_INVAL) {
      dstrwrite(STDERR_FILENO, "Invalid baud rate!\r\n");
    }
    return EXIT_FAILURE;
  }
  if (set_tty(args, tty_raw) != 0) {
    return EXIT_FAILURE;
  }

  // Wait for the first byte.
  for (;;) {
    int queued = write_queued();
    if (queued < 0) {
      // We ignore this error and assume the queue is empty for now.
      queued = 0;
    }
    bool active = !args->wait_cr && queued == 0;

    // If the write queue is empty, that implies there's probably
    // something connected, so we want to poll more frequently. When
    // something is in the queue, it'll get flushed right away when we get
    // a connection, so a long timeout for `poll()` is fine.
    int wait = active ? WAIT_ACTIVE : WAIT_PASSIVE;
    if ((c = readchar(wait)) == READCHAR_AGAIN) {
      // Read timed out.
      if (active) {
        // Send device status report request.
        strwrite("\033[6n");
      }
      continue;
    } else if (READCHAR_ISERR(c)) {
      return EXIT_FAILURE;
    }

    break;
  }

  for (;;) {
    // `c` will contain the first non-protocol byte.
    if ((res = check_protocol(&c)) < 0) {
      return EXIT_FAILURE;
    } else if (res == ST_PPP_FOUND && args->enable_ppp) {
      return start_pppd(args);
    } else if (res == ST_SSH_FOUND && args->enable_ssh) {
      return connect_ssh(args);
    }

    if (args->wait_cr) {
      if (c != '\r' && c != '\n') {
        if (READCHAR_ISERR(c = readchar(WAIT_PASSIVE))) {
          return EXIT_FAILURE;
        } else {
          continue;
        }
      }
    } else {
      if (discard_cpr(c) != 0) {
        return EXIT_FAILURE;
      }
    }

    break;
  }

  return start_login(args);
}

int main(int argc, char *argv[]) {
  int ret;
  struct termios tty[] = {0};

  // Save the current terminal settings so we can restore them later
  if (tcgetattr(STDIN_FILENO, tty) != 0) {
    // This program is intended to be statically compiled for embedded
    // devices, so we don't want to bloat the binary by pulling in things
    // like `perror()`, `printf()`, or `strerror()`.
    return EXIT_FAILURE;
  }

  ret = ugetty(argc, argv);

  // Restore terminal settings.
  tcsetattr(STDIN_FILENO, TCSANOW, tty);

  return ret;
}
