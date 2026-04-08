# µgetty

A minimal getty for embedded Linux that auto-detects the client and dispatches
incoming serial connections to a login prompt, a PPP session, or the local SSH
server as appropriate.

## Description

`ugetty` replaces a conventional getty on a serial TTY. Rather than
immediately prompting for a username, it watches the first bytes from the
remote end and routes accordingly:

* **PPP** (RFC 1662 LCP frame) -> exec `pppd`
* **SSH** (`SSH-2.0-` identification string) -> proxy the connection to the local `sshd`
* **Anything else** -> display `/etc/issue`, prompt for username, exec `login`

While waiting for a connection, `ugetty` actively probes for an attached
terminal by periodically sending cursor position requests. A real terminal
will respond with a cursor position report, which `ugetty` silently discards
before proceeding. This means the port starts responding promptly rather than
waiting for the remote end to send the first byte.

This allows a single serial port on a headless device to serve as a universal
out-of-band access channel. Plug in a USB-serial adapter or connect via a
hardware UART, and whatever you send just works.

`proxycommand` is a companion binary intended to be used as an SSH
`ProxyCommand` on the connecting host. It handles the serial connection and
the banner detection handshake with `ugetty`.

## Getting Started

### Dependencies

**`ugetty`** (target device):
* Any Linux system with a vaguely modern `gcc` and `make`. Building with
  `clang` should also work, but has not been tested.
* For the default ARM cross-compiled build: a musl-cross toolchain in
  `/dev/shm/armv6-linux-musleabihf-cross/`. Pre-built tool chains are
  available from various sources, including [musl.cc](https://musl.cc/).

**`proxycommand`** (connecting host):
* Any Linux system with `gcc` and `make`, as with `ugetty`

### Building

```
git clone https://github.com/ryancdotorg/ugetty.git
cd ugetty
```

Native build (both binaries, useful for testing `proxycommand` locally):

```
make bin/ugetty bin/proxycommand
```

Cross-compiled, statically linked ARM build of `ugetty` plus a native
`proxycommand`:

```
./build.sh
```

Or with an explicit triple:

```
CROSS_TRIPLE=armv6-linux-musleabihf make cross/$CROSS_TRIPLE/bin/ugetty-stripped bin/proxycommand
```

## Usage

### ugetty

```
Usage: ugetty [OPTIONS] BAUD_RATE TTY [TERMTYPE]

Open TTY, look for terminal, PPP, or SSH, then prompt for username and
invoke `login`, `pppd`, or an SSH connection as appropriate.

    -h               Enable hardware flow control
    -L               Ignore carrier detect state
    -n               Do not prompt for username
    -w               Wait for CR or LF before prompting for username
    -i               Don't display issue file
    -t SEC           Timeout for username prompt
    -f ISSUE_FILE    Alternate issue file
    -l LOGIN         Alternate `login` binary
    -P [PPPD]        Enable PPP support. Optionally specify `pppd` binary.
    -S [PORT]        Enable SSH proxy support. Optionally specify port.

BAUD_RATE of "0" leaves it unchanged
```

The TTY argument can be a device name (e.g. `ttyUSB0`) or the special value
`auto`, which determines the tty name based on the file descriptor passed by
`init` or `systemd`. Note that `ugetty` _will not_ open a TTY device itself,
it _must_ inherit one on STDIO. If the TTY argument does not match STDIO,
`ugetty` will fail to start.

Arguments after `--` are passed directly to `pppd` as its argument vector,
bypassing the peer file lookup and overriding the built-in defaults. Note
that `-P` must be specified for `--` to be valid.

The command line arguments for `ugetty` have been chosen to match (and not
conflict with) those of common getty software to the extent feasible.

Explicit non-features (do not ask me to add them):

* Autobaud *(janky)*
* Autologin *(footgun)*
* Data/parity/stop bit detection *(y u no use 8n1‽)*
* Modem support *(where did you even get a pots line?)*
* Uppercase terminal detection/support *(ykinmkbykiok)*

### proxycommand

```
proxycommand /dev/ttyXXX BAUD_RATE    # connect via serial port
proxycommand HOST PORT                # connect via TCP
```

Intended to be called by `ssh` as a `ProxyCommand`, not run directly. It will
auto-detect whether `ProxyUseFdpass` is enabled and operate accordingly.

You can set up `ssh` to use it automatically by adding something like this to
`~/.ssh/config`:

```
Match host tty* exec "test -c /dev/%n -a -w /dev/%n"
    ControlMaster auto
    ControlPath ${XDG_RUNTIME_DIR}/ssh/%n-%C.sock
    ProxyUseFdpass yes
    ProxyCommand /path/to/proxycommand /dev/%n 115200
```

You need to use `%n` rather than `%h` because the `%h` is normalized to
lowercase, whereas `%n` is not. The `ControlMaster` and `ControlPath` settings
are needed for multiplexing if you want to be able to have multiple connections
to the device.

## Examples

### Interactive login over a USB serial adapter

On the device with systemd, create `/etc/systemd/system/serial-getty@ttyUSB0.service.d/override.conf`:

```ini
[Service]
ExecStart=
ExecStart=-/sbin/ugetty 115200 %I
```

On the connecting host:

```
cu -l /dev/ttyUSB0
```

To also support PPP (with explicit IPv4 addresses), use `/etc/inittab` with busybox init:

```
ttyUSB0::respawn:/sbin/ugetty -L -P 115200 ttyUSB0 -- local noauth nodetach 192.168.100.1:192.168.100.2
```

### SSH via serial

On the device, ensure `ugetty` is started with `-S`:

```
ttyUSB0::respawn:/sbin/ugetty -S 115200 ttyUSB0
```

On the connecting host:

```
ssh -o ProxyCommand='path/to/proxycommand /dev/%n 115200' user@ttyUSB0
```

### SSH via a TCP serial console server

If `ugetty` is started with `-w -S`, it stays silent until the client sends the
first byte, which means an unmodified `ssh` client can connect directly to the
TCP serial server. `ugetty` will detect the SSH identification string and proxy
it automatically. Without `-w`, the active terminal probes would interfere with
the SSH handshake.

### PPP networking over serial

When `-P` is given and a PPP LCP frame is detected, `ugetty` selects `pppd`
arguments in the following order:

1. Explicit arguments following `--` (bypasses peer file lookup entirely)
2. `/etc/ppp/peers/ugetty.TTYNAME` (e.g. `ugetty.ttyUSB0`) — port-specific config
3. `/etc/ppp/peers/ugetty` — shared config for all ports
4. Built-in defaults: `local noauth nodetach noip noipdefault +ipv6 ipv6cp-use-persistent`

The built-in defaults bring up an IPv6 link-local PPP session without
negotiating IPv4.

For the default IPv6 link-local-only setup, start `ugetty` with `-P` and run
something like this on the connecting host:

```
sudo pppd /dev/ttyUSB0 115200 local noauth nodetach
```

`ugetty` will detect the LCP frame and hand the port directly to `pppd` on the
device. If you want IPv4 on the device side, set up a peer file or pass an
explicit `pppd` argument list after `--` when starting `ugetty`, as in the
earlier `/etc/inittab` example (which uses `-P ... -- ...`).

### Restricting SSH access per serial port in sshd_config

When `ugetty` proxies an SSH connection it binds the source address to
`127.<major & 0xFF>.<minor & 0xFF>.<hash(name) % 253 + 2>`, which is stable
for a given device node. This lets `sshd` apply per-port policy:

```
# /etc/ssh/sshd_config

# Must listen on 0.0.0.0 for ugetty's proxy connections to reach it.
ListenAddress 0.0.0.0

# Allow only key-based auth for connections via USB ACM serial (major 166).
Match Address 127.166.*.*
    PasswordAuthentication no

# Completely block SSH-over-serial on a specific port (ttyUSB0, major 188, minor 0).
Match Address 127.188.0.*
    DenyUsers *
```

To find the source address for a given tty:
```
ls -la /dev/ttyUSB0    # shows major, minor
# or check /var/log/auth.log after connecting
```

## Help

You can file an issue on GitHub, however I may not respond. This software is
being provided without warranty in the hopes that it may be useful.

## Author

[Ryan Castellucci](https://rya.nc/) ([@ryancdotorg](https://github.com/ryancdotorg))

## Donations

I am currently involved in a protracted
[civil rights case](https://www.leighday.co.uk/news/news/2023-news/legal-challenge-urges-government-to-give-legal-recognition-to-nonbinary-people/)
against the British government. If you find my work useful,
**please donate to my [crowdfunding effort](https://enby.org.uk/)**.

## License

Your choice of 0BSD, MIT-0, or CC0-1.0+. Do what you want.
