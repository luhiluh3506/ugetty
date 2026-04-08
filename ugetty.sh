#!/bin/bash
set -eo pipefail
trap 's=$?; printf "%s: Error on line %s: %s\n" "" $LINENO "$BASH_COMMAND"; exit $s' ERR
# end 'strict mode' preamble

export BUILD_SCRIPT_DATE="$(date -r "$0" -Iseconds)"

ORIG_PATH="$PATH"

function setcross() {
    MUSL_CROSS=${1:-armv6-linux-musleabihf}
    CROSS_BIN="/dev/shm/$MUSL_CROSS-cross/bin"
    CROSS_PFX="$CROSS_BIN/$MUSL_CROSS"

    export CC=$CROSS_PFX-gcc
    export CXX=$CROSS_PFX-g++
    export AR=$CROSS_PFX-ar
    export AS=$CROSS_PFX-as
    export LD=$CROSS_PFX-ld
    export NM=$CROSS_PFX-nm
    export RANLIB=$CROSS_PFX-ranlib

    export PATH=$CROSS_BIN:$ORIG_PATH

    export STRIP="$CROSS_PFX-strip -s -R .comment -R .hash -R .gnu.hash"
    export LDFLAGS="-flto -Wl,--gc-sections"
    export CFLAGS="-flto -fno-inline-small-functions -ffunction-sections -fdata-sections -Wl,--gc-sections -static -Os"
    #export CFLAGS="$CFLAGS -march=armv6zk -mcpu=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp"
    export CXXFLAGS="$CFLAGS"
    export MAKEFLAGS="-sj$((`grep -c '^processor' /proc/cpuinfo` * 125 / 100))"
    export HOSTCC=gcc
}

setcross

$CC $CFLAGS $LDFLAGS \
    -std=c17 \
    -Wall -Wextra -pedantic \
    ugetty.c bnprintf.c \
    -o ugetty
$NM -S --size-sort -f bsd ugetty > ugetty.sym
$STRIP ugetty
ls -al ugetty
file -sk ugetty
