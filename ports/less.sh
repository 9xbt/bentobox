#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-g -std=gnu17"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p ports/src
git clone https://github.com/gwsw/less ports/src/less --depth=1
cd ports/src/less

make clean
make -f Makefile.aut distfiles
autoreconf -fvi
cp ../../config.sub .
./bootstrap --gnulib-srcdir=../gnulib
set -e
./configure --host=$ARCH-pc-bentobox --prefix=/usr
make -j"$(nproc)"
make DESTDIR=$BASE install