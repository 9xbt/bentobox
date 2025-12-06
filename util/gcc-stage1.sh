#!/bin/bash
[ -z "$BASE" ] && echo "Please run . build/mlibc-root before building GCC!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH=x86_64
TARGET=x86_64-pc-bentobox
PREFIX="$CWD/build"
export PATH="$CWD/build/bin:$PATH"

set -e
mkdir -p /tmp/gcc-stage1
cd /tmp/gcc-stage1
CC="ccache gcc" CXX="ccache g++" $CWD/gcc/configure --target=$TARGET --prefix=$PREFIX --disable-nls --disable-multilib --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
make install-gcc
make install-target-libgcc
