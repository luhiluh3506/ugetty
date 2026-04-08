#!/bin/bash
set -uo pipefail
trap 's=$?; printf "%s: Error on line %s: %s\n" "" $LINENO "$BASH_COMMAND"; exit $s' ERR
IFS=$'\n\t'
# end 'strict mode' preamble

export CROSS_TRIPLE=armv5l-linux-musleabihf
make cross/$CROSS_TRIPLE/bin/ugetty-stripped bin/proxycommand
export CROSS_TRIPLE=armv6-linux-musleabihf
make cross/$CROSS_TRIPLE/bin/ugetty-stripped bin/proxycommand
export CROSS_TRIPLE=armv7l-linux-musleabihf
make cross/$CROSS_TRIPLE/bin/ugetty-stripped bin/proxycommand
export CROSS_TRIPLE=aarch64-linux-musl
make cross/$CROSS_TRIPLE/bin/ugetty-stripped bin/proxycommand
