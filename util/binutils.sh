#!/bin/bash
CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH=x86_64
TARGET=x86_64-pc-bentobox
PREFIX="$CWD/build"
SYSROOT="$PREFIX/$TARGET"

set -e
mkdir -p /tmp/binutils
cd /tmp/binutils
CC="ccache gcc" CXX="ccache g++" $CWD/binutils-gdb/configure --target=$TARGET --prefix=$PREFIX --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
make install
