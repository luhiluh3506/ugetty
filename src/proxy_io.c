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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "proxy_io.h"
#include "bnprintf.h"
#include "util.h"

#if defined(__OPTIMIZE_SIZE__)
static void _perror(const char *s) {
  // TODO: add a shared writeall function
  char ebuf[256];
  char *d = ebuf;
  size_t n = sizeof(ebuf);
  bnstrcat(&d, &n, s);
  bnstrcat(&d, &n, ": errno=");
  bnstrl(&d, &n, errno);
  bnchr(&d, &n, '\n');
  write(STDERR_FILENO, ebuf, strlen(ebuf));
}
#define perror(X) _perror(X)
#endif

// Make a list of file descriptors non-blocking.
#define set_fds_nonblocking(...) _set_fds_nonblocking((int[]){__VA_ARGS__, -1})
static int _set_fds_nonblocking(int *fds) {
  for (int fd = *fds; fd >= 0; fd = *++fds) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
      char ebuf[256];
      char *d = ebuf;
      size_t n = sizeof(ebuf);
      bnstrcat(&d, &n, "fcntl(");
      bnstrl(&d, &n, fd);
      bnstrcat(&d, &n, ", F_SETFL, O_NONBLOCK): errno=");
      bnstrl(&d, &n, errno);
      bnchr(&d, &n, '\n');
      write(STDERR_FILENO, ebuf, strlen(ebuf));
      return -1;
    }
  }

  return 0;
}


// Transfer the file descriptor over a socket to be handled. We take (and
// ignore) `read_fd` for signature consistency, but all we do is transfer our
// `io_fd` over `write_fd`.
static int io_sendfd(UNUSED(int read_fd), int write_fd, int io_fd) {
  struct msghdr msg[] = {{
    .msg_name = NULL,
    .msg_namelen = 0,
    .msg_iov = &((struct iovec){
      .iov_base = (char[]){0},
      .iov_len = 1,
    }),
    .msg_iovlen = 1,
    .msg_control = (alignas(struct cmsghdr) char[CMSG_SPACE(sizeof(int))]){0},
    .msg_controllen = CMSG_SPACE(sizeof(int)),
    .msg_flags = 0,
  }};

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
  cmsg->cmsg_len = msg->msg_controllen;
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  *(int*)CMSG_DATA(cmsg) = io_fd;

  return sendmsg(write_fd, msg, 0) >= 0 ? 0 : -1;
}


// Spliced non-blocking I/O loop.
static int io_spliced(int r_pipe_fd, int w_pipe_fd, int io_fd) {
  if (set_fds_nonblocking(r_pipe_fd, w_pipe_fd, io_fd) != 0) { return -1; }

  bool r_pipe_eof = false;
  bool io_fd_eof = false;

  // Backpressure state flags.
  bool wait_sock_out = false;
  bool wait_w_pipe_out = false;

  struct pollfd fds[3];

  // Loop until both directions have received EOF.
  while (!(r_pipe_eof && io_fd_eof)) {
    // Setup poll for read (input) pipe.
    fds[0].fd = r_pipe_fd;
    fds[0].events = (!r_pipe_eof && !wait_sock_out) ? POLLIN : 0;
    fds[0].revents = 0;

    // Setup poll for io (bidirectional) file descriptor.
    fds[1].fd = io_fd;
    fds[1].events = 0;
    if (!io_fd_eof && !wait_w_pipe_out) { fds[1].events |= POLLIN; }
    if (wait_sock_out) { fds[1].events |= POLLOUT; }
    fds[1].revents = 0;

    // Setup poll for write (output) pipe.
    fds[2].fd = w_pipe_fd;
    fds[2].events = wait_w_pipe_out ? POLLOUT : 0;
    fds[2].revents = 0;

    if (poll(fds, NITEMS(fds), -1) < 0) {
      if (errno == EINTR) {
        // Try again on interrupted system call.
        continue;
      }
      perror("poll");
      return -1;
    }

    // Check for invalid file descriptors.
    for (unsigned i = 0; i < NITEMS(fds); ++i) {
      if (fds[i].revents & POLLNVAL) { return -1; }
    }

    // If the destination has drained, clear the wait flag so we can resume
    // splicing.
    if (wait_sock_out && (fds[1].revents & POLLOUT)) {
      wait_sock_out = false;
    }
    if (wait_w_pipe_out && (fds[2].revents & POLLOUT)) {
      wait_w_pipe_out = false;
    }

    // Transmit: `r_pipe_fd` -> `io_fd`
    if (!r_pipe_eof && !wait_sock_out && (fds[0].revents & POLLIN)) {
      // Per docs on SPLICE_F_MOVE: 'starting in Linux 2.6.21 it is a no-op; in
      // the future, a correct implementation may be restored.'
      ssize_t res = splice(
        r_pipe_fd, NULL,
        io_fd, NULL,
        65536, SPLICE_F_NONBLOCK | SPLICE_F_MOVE
      );
      if (res > 0) {
        // Partial transfers are implicitly handled. The kernel moves
        // up to `res` bytes.
      } else if (res == 0) {
        // Signal that we're done transmitting to the socket.
        r_pipe_eof = true;
        shutdown(io_fd, SHUT_WR);
      } else {
        if (errno == EAGAIN) {
          // Since `poll()` told us the pipe has data, we can infer that
          // `EAGAIN` means the socket is full.
          wait_sock_out = true;
        } else if (errno == EINTR) {
          // Ignore interrupted system call.
        } else {
          perror("splice r_pipe_fd -> io_fd");
          return -1;
        }
      }
    }

    // Receive: `io_fd` -> `w_pipe_fd`
    if (!io_fd_eof && !wait_w_pipe_out && (fds[1].revents & POLLIN)) {
      ssize_t res = splice(
        io_fd, NULL,
        w_pipe_fd, NULL,
        65536, SPLICE_F_NONBLOCK | SPLICE_F_MOVE
      );
      if (res > 0) {
        // Spliced successfully.
      } else if (res == 0) {
        // Signal that we're done receiving from the socket.
        io_fd_eof = true;
        close(w_pipe_fd);
        fds[2].fd = -1;
      } else {
        if (errno == EAGAIN) {
          // Since `poll()` told us the socket has data, we can infer that
          // `EAGAIN` means the pipe is full.
          wait_w_pipe_out = true;
        } else if (errno != EINTR) {
          perror("splice io_fd -> w_pipe_fd");
          return -1;
        }
      }
    }

    // Handle disconnections and errors (receive).
    if ((fds[1].revents & (POLLERR | POLLHUP)) && !(fds[1].revents & POLLIN)) {
      if (!io_fd_eof) {
        io_fd_eof = true;
        close(w_pipe_fd);
        fds[2].fd = -1;
      }
    }

    // Handle EOF and errors (transmit).
    if ((fds[0].revents & (POLLERR | POLLHUP)) && !(fds[0].revents & POLLIN)) {
      r_pipe_eof = true;
      shutdown(io_fd, SHUT_WR);
    }
  }

  return 0;
}


// Standard buffered non-blocking I/O loop.
int io_buffered(int read_fd, int write_fd, int io_fd) {
  if (set_fds_nonblocking(read_fd, write_fd, io_fd) != 0) { return -1; }

  ssize_t n;

  bool read_eof = false;
  bool io_fd_eof = false;
  bool write_closed = false;

  // Transmit buffer: `read_fd` -> `tx_buf` -> `io_fd`
  char tx_buf[65536];
  size_t tx_len = 0, tx_off = 0;

  // Receive buffer: `io_fd` -> `rx_buf` -> `write_fd`
  char rx_buf[65536];
  size_t rx_len = 0, rx_off = 0;

  struct pollfd fds[3];

  // Loop until both directions have received EOF and both buffers are drained.
  while (!(read_eof && io_fd_eof && tx_len == 0 && rx_len == 0)) {
    // Setup poll for read (input): read only when `tx_buf` is empty.
    fds[0].fd = read_fd;
    fds[0].events = (!read_eof && tx_len == 0) ? POLLIN : 0;
    fds[0].revents = 0;

    // Setup poll for io (bidirectional) file descriptor
    fds[1].fd = io_fd;
    fds[1].events = 0;
    if (!io_fd_eof && rx_len == 0) { fds[1].events |= POLLIN; }
    if (tx_len > 0) { fds[1].events |= POLLOUT; }
    fds[1].revents = 0;

    // Setup poll for write (output): write only when `rx_buf` has data.
    fds[2].fd = write_fd;
    fds[2].events = (rx_len > 0 && !write_closed) ? POLLOUT : 0;
    fds[2].revents = 0;

    if (poll(fds, NITEMS(fds), -1) < 0) {
      if (errno == EINTR) {
        // Try again on interrupted system call.
        continue;
      }
      perror("poll");
      return -1;
    }

    // Check for invalid file descriptors.
    for (unsigned i = 0; i < NITEMS(fds); ++i) {
      if (fds[i].revents & POLLNVAL) { return -1; }
    }

    // Read from `read_fd` into `tx_buf`.
    if (!read_eof && tx_len == 0 && (fds[0].revents & POLLIN)) {
      if ((n = read(read_fd, tx_buf, sizeof(tx_buf))) > 0) {
        tx_len = n;
        tx_off = 0;
      } else if (n == 0) {
        read_eof = true;
        // `tx_buf` is empty here, so shutdown immediately.
        shutdown(io_fd, SHUT_WR);
      } else if (errno != EAGAIN && errno != EINTR) {
        perror("read read_fd");
        return -1;
      }
    }

    // Flush `tx_buf` to `io_fd`.
    if (tx_len > 0 && (fds[1].revents & POLLOUT)) {
      if ((n = write(io_fd, tx_buf + tx_off, tx_len)) > 0) {
        tx_off += n;
        tx_len -= n;
        if (tx_len == 0) {
          tx_off = 0;
          // Drain complete. If `read_fd` is done, signal end of transmission.
          if (read_eof) { shutdown(io_fd, SHUT_WR); }
        }
      } else if (errno != EAGAIN && errno != EINTR) {
        perror("write io_fd");
        return -1;
      }
    }

    // Read from `io_fd` into `rx_buf`.
    if (!io_fd_eof && rx_len == 0 && (fds[1].revents & POLLIN)) {
      if ((n = read(io_fd, rx_buf, sizeof(rx_buf))) > 0) {
        rx_len = n;
        rx_off = 0;
      } else if (n == 0) {
        io_fd_eof = true;
        // `rx_buf` is empty here, so close immediately.
        if (!write_closed) {
          close(write_fd);
          write_closed = true;
          fds[2].fd = -1;
        }
      } else if (errno != EAGAIN && errno != EINTR) {
        perror("read io_fd");
        return -1;
      }
    }

    // Flush `rx_buf` to `write_fd`.
    if (rx_len > 0 && !write_closed && (fds[2].revents & POLLOUT)) {
      if ((n = write(write_fd, rx_buf + rx_off, rx_len)) > 0) {
        rx_off += n;
        rx_len -= n;
        if (rx_len == 0) {
          rx_off = 0;
          // Drain complete. If `io_fd` is done, close `write_fd`.
          if (io_fd_eof) {
            close(write_fd);
            write_closed = true;
          }
        }
      } else if (errno != EAGAIN && errno != EINTR) {
        perror("write write_fd");
        return -1;
      }
    }

    // Handle disconnections and errors (receive).
    if ((fds[1].revents & (POLLERR | POLLHUP)) && !(fds[1].revents & POLLIN)) {
      if (!io_fd_eof) {
        io_fd_eof = true;
        if (rx_len == 0 && !write_closed) {
          close(write_fd);
          write_closed = true;
        }
      }
    }

    // Handle EOF and errors (transmit).
    if ((fds[0].revents & (POLLERR | POLLHUP)) && !(fds[0].revents & POLLIN)) {
      if (!read_eof) {
        read_eof = true;
        if (tx_len == 0) {
          shutdown(io_fd, SHUT_WR);
        }
      }
    }
  }

  return 0;
}


static bool can_sendfd(int fd) {
  // We can only do `io_sendfd()` on Unix stream sockets.
  int v; socklen_t n;
  n = sizeof(int);
  if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &v, &n) != 0) { return false; }
  if (v != AF_UNIX) { return false; }
  n = sizeof(int);
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &v, &n) != 0) { return false; }
  if (v != SOCK_STREAM) { return false; }
  return true;
}


// Dispatch to the appropriate handler.
int proxy_io(int read_fd, int write_fd, int io_fd) {
  struct stat st[] = {0};
  // Since `st` is zero initalized, on `fstat()` failure `st->st_mode` will be
  // 0 and `io_buffered()` will be used via the default fallthrough path. We
  // therefore don't bother to check the return code.
  fstat(read_fd, st);
  switch (st->st_mode & S_IFMT) {
    case S_IFSOCK:
      if (can_sendfd(write_fd)) {
        return io_sendfd(read_fd, write_fd, io_fd);
      } // fallthrough
    default: // fallthrough
    case S_IFCHR:  return io_buffered(read_fd, write_fd, io_fd);
    case S_IFIFO:  return io_spliced(read_fd, write_fd, io_fd);
  }

  // Unreachable.
  __builtin_unreachable();
}
