#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-g -std=gnu17 -Wno-error"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p ports/src
git clone https://github.com/zevweiss/grep ports/src/grep --depth=1
cd ports/src/grep
rm -f .gitmodules
rmdir gnulib
ln -sf ../gnulib gnulib

make clean
autoreconf -fvi
./bootstrap --gnulib-srcdir=../gnulib
cp ../../config.sub build-aux/
set -e
./configure --host=$ARCH-pc-bentobox --prefix=/usr
make -j"$(nproc)"
make DESTDIR=$BASE install