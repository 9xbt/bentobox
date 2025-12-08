#!/bin/bash
[ -z "$BASE" ] || [ -z "$TARGET" ] && echo "Please run . build/mlibc-root before building GCC!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET=x86_64-pc-bentobox
PREFIX="$CWD/build"
export PATH="$CWD/build/bin:$PATH"

set -e
mkdir -p /tmp/gcc-stage2
cd /tmp/gcc-stage2
CC="ccache gcc" CXX="ccache g++" $CWD/gcc/configure --target=$TARGET --prefix=$PREFIX --with-sysroot=$BASE --disable-nls --disable-multilib --enable-languages=c,c++ --disable-shared
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
make all-target-libstdc++-v3 -j$(nproc)
make install-gcc
make install-target-libgcc
make install-target-libstdc++-v3

cd $PREFIX/lib/gcc/x86_64-pc-bentobox/16.0.0/
ln -sf crtbegin.o crtbeginT.o
ln -sf crtend.o crtendT.o
ln -sf crtbegin.o crtbeginS.o
ln -sf crtend.o crtendS.o
