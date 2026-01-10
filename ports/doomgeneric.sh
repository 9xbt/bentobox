#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

mkdir -p $BASE/usr/bin
mkdir -p ports/src
git clone https://github.com/ozkl/doomgeneric ports/src/doomgeneric --depth=1
cd ports/src/doomgeneric/
# git apply ../../doomgeneric.diff
cd doomgeneric/
make clean
make -f Makefile.freebsd -j$nproc CC="${TOOLCHAIN_PREFIX:-}gcc" CFLAGS="-std=c99 -DNORMALUNIX -DLINUX -D_DEFAULT_SOURCE" LDFLAGS="-Wl,--gc-sections"
cp doomgeneric $BASE/usr/bin