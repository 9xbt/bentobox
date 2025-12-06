#!/bin/bash
[ -z "$BASE" ] && echo "Please run . build/mlibc-root before building binutils!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH=x86_64
TARGET=x86_64-pc-bentobox
PREFIX="$BASE/usr"

set -e
mkdir -p /tmp/binutils-stage3
cd /tmp/binutils-stage3
CC="ccache $TARGET-gcc" CXX="ccache $TARGET-g++" $CWD/binutils-gdb/configure --host=$TARGET --target=$TARGET --prefix=$PREFIX --with-sysroot=/usr --with-build-sysroot=$BASE --disable-nls --disable-werror --disable-gdb --disable-gdbserver --disable-shared --enable-static
make -j$(nproc)
make install
