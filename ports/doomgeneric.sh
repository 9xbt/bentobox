#!/bin/bash
[ -z "$MLIBC_ROOT" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

git clone https://github.com/ozkl/doomgeneric ports/src/doomgeneric --depth=1
cd ports/src/doomgeneric/
git apply ../../doomgeneric.diff
cd doomgeneric/
make -f Makefile.fblinux -j$nproc CC="${TOOLCHAIN_PREFIX:-}gcc"
mkdir -p ../../../../base/usr/bin
cp doomgeneric ../../../../base/usr/bin/
cd ../../../../