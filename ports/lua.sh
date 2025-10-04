#!/bin/bash
WD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MLIBC="$WD/../build/mlibc/x86_64"

mkdir -p ports/src
cd ports/src
git clone https://github.com/lua/lua
cd lua
git apply ../../lua.diff
make CC=gcc MYCFLAGS="-I$MLIBC/include" MYLDFLAGS="-L$MLIBC/lib -nostdlib -static $MLIBC/lib/crt0.o" MYLIBS="-Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group" -j$(nproc)
cp lua ../../../base/usr/bin