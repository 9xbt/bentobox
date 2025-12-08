#!/bin/bash
[ -z "$BASE" ] && echo "Please run . build/mlibc-root before building GCC!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/../util
PREFIX="$BASE/usr"
export PATH="$CWD/build/bin:$PATH"

set -e
cd $CWD/gcc
./contrib/download_prerequisites
cd -

mkdir -p /tmp/gcc-stage3
cd /tmp/gcc-stage3
CC="ccache $TARGET-gcc" CXX="ccache $TARGET-g++" $CWD/gcc/configure --host=$TARGET --target=$TARGET --prefix=$PREFIX --with-sysroot=/ --with-build-sysroot=$BASE --libexecdir="$PREFIX/lib" --disable-nls --disable-multilib --enable-languages=c,c++ --disable-libssp --disable-libgomp --disable-libquadmath --disable-lto-plugin --disable-default-pie --disable-shared --enable-static
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
# make all-target-libstdc++-v3 -j$(nproc)
make install-gcc
make install-target-libgcc
# make install-target-libstdc++-v3
