#!/bin/bash
[ -z "$BASE" ] || [ -z "$TARGET" ] && echo "Please run . build/mlibc-root before building binutils!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$CWD/build/$ARCH"

set -e
mkdir -p /var/tmp/binutils-$ARCH
cd /var/tmp/binutils-$ARCH
CC="ccache gcc" CXX="ccache g++" $CWD/binutils-gdb/configure --target=$TARGET --prefix=$PREFIX --with-sysroot=$BASE --disable-nls --disable-werror --enable-shared --enable-host-shared
make -j$(nproc)
make install
