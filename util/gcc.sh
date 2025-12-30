#!/bin/bash
[ -z "$BASE" ] || [ -z "$TARGET" ] && echo "Please run . build/mlibc-root before building GCC!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET=x86_64-pc-bentobox
PREFIX="$CWD/build"
export PATH="$CWD/build/bin:$PATH"

set -e
mkdir -p /var/tmp/gcc
cd /var/tmp/gcc
CC="ccache gcc" CXX="ccache g++" $CWD/gcc/configure --target=$TARGET --prefix=$PREFIX --with-sysroot=$BASE --disable-nls --disable-multilib --enable-languages=c,c++ --enable-shared --enable-host-shared --enable-default-pie
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
# make all-target-libstdc++-v3 -j$(nproc)
make install-gcc
make install-target-libgcc
# make install-target-libstdc++-v3

cp -r $PREFIX/$TARGET/lib/* $BASE/usr/lib/
