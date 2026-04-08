// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#define _GNU_SOURCE

#if __STDC_VERSION__ < 202000L
#include <stdalign.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>

#include "baud.h"
#include "proxy_io.h"

#define UNUSED(X) __attribute__((unused)) X

// Extract the address from an addrinfo struct.
static void *ai_to_addr(const struct addrinfo *p) {
  if (p == NULL) { return NULL; }
  switch (p->ai_family) {
    case AF_INET:  return &(((struct sockaddr_in *)p->ai_addr)->sin_addr);
    case AF_INET6: return &(((struct sockaddr_in6 *)p->ai_addr)->sin6_addr);
    default:       return NULL;
  }
}

int connect_tcp(const char *host, const char *port) {
  int err, sock_fd = -1;

  // We want dual-stack TCP, and the kernel should pick our source IP.
  struct addrinfo *res, *p, hints[] = {{
    .ai_family = AF_UNSPEC,
    .ai_socktype = SOCK_STREAM,
  }};

  // Resolve the hostname.
  if ((err = getaddrinfo(host, port, hints, &res)) != 0) {
    fprintf(stderr, "gai error: %s\n", gai_strerror(err));
    return -1;
  }

  // Loop over resolved addresses.
  // TODO (maybe): Happy-eyeballs style dual-protocol connect?
  for (p = res; p != NULL; p = p->ai_next) {
    char ip_str[INET6_ADDRSTRLEN+1];

    void *addr;
    if ((addr = ai_to_addr(p)) == NULL) {
      continue;
    }

    inet_ntop(p->ai_family, addr, ip_str, sizeof(ip_str));
    fprintf(stderr, "Trying to connect to %s...\n", ip_str);
    if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
      perror("socket");
      sock_fd = -1;
      // Try the next socket.
      continue;
    }
    if ((err = connect(sock_fd, p->ai_addr, p->ai_addrlen)) != 0) {
      close(sock_fd);
      // Reset the file descriptor to -1 since we failed.
      sock_fd = -1;
      continue;
    }
    fprintf(stderr, "Connected to %s!\n", ip_str);
    break;
  }

  if (res != NULL) {
    freeaddrinfo(res);
  }

  return sock_fd;
}

int connect_serial(const char *dev, const char *baud) {
  errno = 0;
  int ser_fd = -1;
  struct termios tty[] = {0};

  if (baud[0] == '0' && baud[1] == '\0') {
    // OK
  } else if (baud[0] < '1' || baud[0] > '9') {
    fprintf(stderr, "Invalid baud: '%s'\n", baud);
    goto connect_ser_err;
  }

  char *end;
  long rate = strtol(baud, &end, 10);
  if (*end != '\0') {
    errno = EINVAL;
  }

  if (errno != 0) {
    perror("strtol");
    goto connect_ser_err;
  }

  if ((ser_fd = open(dev, O_RDWR)) < 0) {
    fprintf(stderr, "%s\n", dev);
    perror("open");
    goto connect_ser_err;
  }

  if (tcgetattr(ser_fd, tty) != 0) {
    perror("tcgetattr");
    goto connect_ser_err;
  }

  // Set tty in raw mode
  cfmakeraw(tty);
  // Set 8 data bits, no parity, 1 stop bit
  tty->c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
  tty->c_cflag |=   CS8;
  if (rate != 0) {
    // Set baud
    bool found = false;
    for (int i = 0; BAUD_TAB[i].baud >= 0; ++i) {
      if (rate == BAUD_TAB[i].baud) {
        if (cfsetspeed(tty, BAUD_TAB[i].speed) < 0) {
          perror("cfsetspeed");
          goto connect_ser_err;
        }
        found = true;
        break;
      }
    }

    if (!found) {
      fprintf(stderr, "Invalid baud: %ld\n", rate);
      goto connect_ser_err;
    }
  }

  if (tcsetattr(ser_fd, TCSANOW, tty) != 0) {
    perror("tcsetattr");
    goto connect_ser_err;
  }

  return ser_fd;
connect_ser_err:
  if (ser_fd >= 0) {
    close(ser_fd);
  }

  return -1;
}

int main(int argc, char *argv[]) {
  int fd = -1, banner_seen = 0, attempts = 0;

  if (isatty(STDOUT_FILENO)) {
    fprintf(stderr, "This program is meant to be called by `ssh` as a ProxyCommand.\n");
    return EXIT_FAILURE;
  } else if (argc < 3) {
    return EXIT_FAILURE;
  }

  //FILE *console = fopen("/dev/tty", "w");

  if (argv[1][0] == '/') {
    while (banner_seen == 0) {
      if (fd >= 0) { close(fd); }
      if (attempts++ > 16) { return EXIT_FAILURE; }
      //fprintf(console, "> CONNECT: %s\n", argv[1]);
      // NOTE: For serial, you want something like:
      //
      // ```ssh_config
      // Match host tty* exec "test -c /dev/%n"
      //   ControlMaster auto
      //   ControlPath ${XDG_RUNTIME_DIR}/ssh/%n-%C.sock
      //   ProxyUseFdpass yes
      //   ProxyCommand /path/to/proxycommand /dev/%n 115200`
      // ```
      //
      // You need to use `%n` rather than `%h` because the `%h` is normalized
      // to lowercase, whereas `%n` is not. The ControlMaster and ControlPath
      // settings are needed for multiplexing if you want to be able to have
      // multiple connections to the device.
      if ((fd = connect_serial(argv[1], argv[2])) < 0) {
        return EXIT_FAILURE;
      }

      // Wake up µgetty.
      if (write(fd, (char[]){'\r'}, 1) < 1) { continue; }
      if (write(fd, (char[]){'\n'}, 1) < 1) { continue; }

      // Now we look for some data, then a pause.
      for (;;) {
        unsigned char c;

        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        int res = select(fd + 1, &fds, NULL, NULL, &tv);

        if (res < 0) {
          if (errno == EINTR) { continue; }
          return EXIT_FAILURE;
        } else if (res == 0) {
          break;
        }

        int n = read(fd, &c, 1);
        if (n <= 0 || (c != '\r' && c != '\n' && !(c >= ' ' && c <= '~'))) {
          banner_seen = 0;
          break;
        }

        banner_seen = 1;
      }
    }
  } else {
    if ((fd = connect_tcp(argv[1], argv[2])) < 0) {
      return EXIT_FAILURE;
    }
  }

  return proxy_io(STDIN_FILENO, STDOUT_FILENO, fd) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
