#!/bin/bash
[ -z "$BASE" ] || [ -z "$TARGET" ] && echo "Please run . build/mlibc-root before building binutils!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$CWD/build"

set -e
mkdir -p /tmp/binutils-stage1
cd /tmp/binutils-stage1
CC="ccache gcc" CXX="ccache g++" $CWD/binutils-gdb/configure --target=$TARGET --prefix=$PREFIX --with-sysroot=$BASE --disable-nls --disable-werror
make -j$(nproc)
make install
