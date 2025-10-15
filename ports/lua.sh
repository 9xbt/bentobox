#!/bin/bash
[ -z "$MLIBC_ROOT" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

mkdir -p ports/src
cd ports/src
git clone https://github.com/lua/lua --depth=1
cd lua
git apply ../../lua.diff
make CC=gcc MYCFLAGS="-I$MLIBC_ROOT/include" MYLDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o" MYLIBS="-Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group" -j$(nproc)
cp lua ../../../base/usr/bin