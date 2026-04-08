// SPDX-License-Identifier: 0BSD OR MIT-0 OR CC0-1.0+
// Copyright © 2026 Ryan Castellucci, no rights reserved
// https://rya.nc/
// https://github.com/ryancdotorg

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>

#include "ugetty_args.h"

static int total, passed, failed;

#define CHECK(cond) check_impl((cond), #cond, __LINE__)

static void check_impl(int ok, const char *expr, int line) {
  total++;
  if (ok) {
    passed++;
  } else {
    failed++;
    fprintf(stderr, "FAIL [line %d]: %s\n", line, expr);
  }
}

// Suppress stdout around calls that print usage on error, then restore.
static int saved_stdout = -1;

static void stdout_quiet(void) {
  int devnull = open("/dev/null", O_WRONLY);
  saved_stdout = dup(STDOUT_FILENO);
  dup2(devnull, STDOUT_FILENO);
  close(devnull);
}

static void stdout_restore(void) {
  if (saved_stdout >= 0) {
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    saved_stdout = -1;
  }
}

// Count a NULL-terminated argv and call parse_args.
static int pa(struct args *a, char **argv) {
  int argc = 0;
  while (argv[argc] != NULL) argc++;
  return parse_args(argc, argv, a);
}

static void print_args(const struct args *a) {
  printf("  tty          = \"%s\"\n",  a->tty);
  printf("  tty_rdev     = %lu\n",     (unsigned long)a->tty_rdev);
  printf("  baud_rate    = %d\n",      a->baud_rate);
  printf("  timeout      = %d\n",      a->timeout);
  printf("  issue_file   = %s\n",      a->issue_file   ? a->issue_file   : "\033[31m(null)\033[m");
  printf("  login_program= %s\n",      a->login_program? a->login_program: "\033[31m(null)\033[m");
  printf("  termtype     = %s\n",      a->termtype     ? a->termtype     : "\033[31m(null)\033[m");
  printf("  pppd_program = %s\n",      a->pppd_program ? a->pppd_program : "\033[31m(null)\033[m");
  printf("  pppd_args    =");
  if (a->pppd_args) {
    for (char **p = a->pppd_args; *p; ++p) printf(" \"%s\"", *p);
  } else {
    printf(" \033[31m(null)\033[m");
  }
  printf("\n");
  printf("  ssh_port     = %u\n",      a->ssh_port);
  printf("  flow_control = %s\n",      a->flow_control ? "\033[32mtrue\033[m" : "\033[31mfalse\033[m");
  printf("  local_line   = %s\n",      a->local_line   ? "\033[32mtrue\033[m" : "\033[31mfalse\033[m");
  printf("  skip_login   = %s\n",      a->skip_login   ? "\033[32mtrue\033[m" : "\033[31mfalse\033[m");
  printf("  wait_cr      = %s\n",      a->wait_cr      ? "\033[32mtrue\033[m" : "\033[31mfalse\033[m");
  printf("  noissue      = %s\n",      a->noissue      ? "\033[32mtrue\033[m" : "\033[31mfalse\033[m");
  printf("  enable_ppp   = %s\n",      a->enable_ppp   ? "\033[32mtrue\033[m" : "\033[31mfalse\033[m");
  printf("  enable_ssh   = %s\n",      a->enable_ssh   ? "\033[32mtrue\033[m" : "\033[31mfalse\033[m");
}

int main(int argc, char *argv[]) {
  struct args a;

  if (argc > 1) {
    int ret = parse_args(argc, argv, &a);
    print_args(&a);
    if (ret != 0) fprintf(stderr, "parse_args returned %d\n", ret);
    exit(ret != 0 ? 1 : 0);
  }

  // Error cases (don't need tty detection)
  stdout_quiet();

  // Missing everything.
  CHECK(pa(&a, (char*[]){"ugetty", NULL}) < 0);

  // Baud given but no tty.
  CHECK(pa(&a, (char*[]){"ugetty", "115200", NULL}) < 0);

  // Unknown flag.
  CHECK(pa(&a, (char*[]){"ugetty", "-z", "115200", "auto", NULL}) < 0);

  // -t value too large.
  CHECK(pa(&a, (char*[]){"ugetty", "-t9999", "115200", "auto", NULL}) < 0);

  // -t non-numeric.
  CHECK(pa(&a, (char*[]){"ugetty", "-tabc", "115200", "auto", NULL}) < 0);

  // Baud rate out of range.
  CHECK(pa(&a, (char*[]){"ugetty", "999999999", "auto", NULL}) < 0);

  // -S with value > 65535 that is not a valid baud rate.
  CHECK(pa(&a, (char*[]){"ugetty", "-S70000", "115200", "auto", NULL}) < 0);

  // -- without -P is an error.
  CHECK(pa(&a, (char*[]){"ugetty", "115200", "auto", "--", "call", "ugetty", NULL}) < 0);

  stdout_restore();

  // These need a TTY.

  if (!isatty(STDIN_FILENO)) {
    fprintf(stderr, "\033[1mNOTE: stdin is not a tty, skipping %d tty-dependent tests\033[m\n", 28);
    goto done;
  }

  // Basic positional order: baud then tty.
  CHECK(pa(&a, (char*[]){"ugetty", "115200", "auto", NULL}) == 0);
  CHECK(a.baud_rate == 115200);
  CHECK(a.tty[0] != '\0');
  CHECK(strcmp(a.issue_file,    DEFAULT_ISSUE_FILE) == 0);
  CHECK(strcmp(a.login_program, DEFAULT_LOGIN)      == 0);
  CHECK(strcmp(a.termtype,      DEFAULT_TERMTYPE)   == 0);
  CHECK(a.timeout      == 0);
  CHECK(a.ssh_port     == 0);
  CHECK(!a.flow_control && !a.local_line && !a.enable_ppp && !a.enable_ssh);

  // Basic positional order: tty then baud.
  CHECK(pa(&a, (char*[]){"ugetty", "auto", "115200", NULL}) == 0);
  CHECK(a.baud_rate == 115200);
  CHECK(a.tty[0] != '\0');

  // Baud rate 0 (leave unchanged).
  CHECK(pa(&a, (char*[]){"ugetty", "0", "auto", NULL}) == 0);
  CHECK(a.baud_rate == 0);

  // Term type positional.
  CHECK(pa(&a, (char*[]){"ugetty", "115200", "auto", "vt220", NULL}) == 0);
  CHECK(strcmp(a.termtype, "vt220") == 0);

  // Combined boolean flags.
  CHECK(pa(&a, (char*[]){"ugetty", "-hLnwi", "115200", "auto", NULL}) == 0);
  CHECK(a.flow_control && a.local_line && a.skip_login && a.wait_cr && a.noissue);

  // -t inline.
  CHECK(pa(&a, (char*[]){"ugetty", "-t30", "115200", "auto", NULL}) == 0);
  CHECK(a.timeout == 30);

  // -t separate.
  CHECK(pa(&a, (char*[]){"ugetty", "-t", "30", "115200", "auto", NULL}) == 0);
  CHECK(a.timeout == 30);

  // -t max boundary.
  CHECK(pa(&a, (char*[]){"ugetty", "-t3600", "115200", "auto", NULL}) == 0);
  CHECK(a.timeout == 3600);

  // -f and -l inline.
  CHECK(pa(&a, (char*[]){"ugetty", "-f/etc/motd", "-l/bin/sh", "115200", "auto", NULL}) == 0);
  CHECK(strcmp(a.issue_file,    "/etc/motd") == 0);
  CHECK(strcmp(a.login_program, "/bin/sh")   == 0);

  // -f and -l separate.
  CHECK(pa(&a, (char*[]){"ugetty", "-f", "/etc/motd", "-l", "/bin/sh", "115200", "auto", NULL}) == 0);
  CHECK(strcmp(a.issue_file,    "/etc/motd") == 0);
  CHECK(strcmp(a.login_program, "/bin/sh")   == 0);

  // -P alone: enable ppp, default pppd path.
  CHECK(pa(&a, (char*[]){"ugetty", "-P", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ppp);
  CHECK(strcmp(a.pppd_program, DEFAULT_PPPD) == 0);

  // -P with separate path.
  CHECK(pa(&a, (char*[]){"ugetty", "-P", "/usr/local/sbin/pppd", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ppp);
  CHECK(strcmp(a.pppd_program, "/usr/local/sbin/pppd") == 0);

  // -P with inline path.
  CHECK(pa(&a, (char*[]){"ugetty", "-P/usr/local/sbin/pppd", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ppp);
  CHECK(strcmp(a.pppd_program, "/usr/local/sbin/pppd") == 0);

  // Without -P: pppd_program stays NULL.
  CHECK(pa(&a, (char*[]){"ugetty", "115200", "auto", NULL}) == 0);
  CHECK(!a.enable_ppp);
  CHECK(a.pppd_program == NULL);

  // -S alone: enable ssh, default port, explicit baud.
  CHECK(pa(&a, (char*[]){"ugetty", "-S", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ssh);
  CHECK(a.ssh_port  == DEFAULT_SSH_PORT);
  CHECK(a.baud_rate == 115200);

  // -S with unambiguous port (not a valid baud) + explicit baud.
  CHECK(pa(&a, (char*[]){"ugetty", "-S", "8080", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ssh);
  CHECK(a.ssh_port  == 8080);
  CHECK(a.baud_rate == 115200);

  // -S with ambiguous value (valid baud ≤ 65535) + separate baud: ssh_port wins.
  CHECK(pa(&a, (char*[]){"ugetty", "-S", "9600", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ssh);
  CHECK(a.ssh_port  == 9600);
  CHECK(a.baud_rate == 115200);

  // -S with ambiguous value (valid baud ≤ 65535), no other baud: promoted to baud.
  CHECK(pa(&a, (char*[]){"ugetty", "-S", "9600", "auto", NULL}) == 0);
  CHECK(a.enable_ssh);
  CHECK(a.baud_rate == 9600);
  CHECK(a.ssh_port  == DEFAULT_SSH_PORT);

  // -S with value > 65535 that is a valid baud, no other baud: promoted to baud.
  CHECK(pa(&a, (char*[]){"ugetty", "-S", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ssh);
  CHECK(a.baud_rate == 115200);
  CHECK(a.ssh_port  == DEFAULT_SSH_PORT);

  // -S inline port.
  CHECK(pa(&a, (char*[]){"ugetty", "-S8080", "115200", "auto", NULL}) == 0);
  CHECK(a.enable_ssh);
  CHECK(a.ssh_port  == 8080);
  CHECK(a.baud_rate == 115200);

  // -- pppd args: array starts with "pppd", rest are original args.
  { char *v[] = {"ugetty", "-P", "115200", "auto", "--", "noauth", "local", NULL};
    CHECK(pa(&a, v) == 0);
    CHECK(a.enable_ppp);
    CHECK(a.pppd_args != NULL);
    CHECK(strcmp(a.pppd_args[0], "pppd")   == 0);
    CHECK(strcmp(a.pppd_args[1], "noauth") == 0);
    CHECK(strcmp(a.pppd_args[2], "local")  == 0);
    CHECK(a.pppd_args[3] == NULL); }

done:
  printf("%d/%d passed", passed, total);
  if (failed > 0) printf(", %d FAILED", failed);
  printf("\n");
  return failed > 0 ? 1 : 0;
}
