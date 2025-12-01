#!/bin/bash
CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH=x86_64
TARGET=x86_64-pc-bentobox
PREFIX="$CWD/build"
export PATH="$PREFIX/bin:$PATH"

set -e
mkdir -p /tmp/gcc
cd /tmp/gcc
CC="ccache gcc" CXX="ccache g++" $CWD/gcc/configure --target=$TARGET --prefix=$PREFIX --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
#make all-target-libstdc++-v3 -j$(nproc)
make install-gcc
make install-target-libgcc
#make install-target-libstdc++-v3
