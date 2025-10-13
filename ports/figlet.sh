#!/bin/bash
mkdir -p ports/src
mkdir -p base/usr/local/share/figlet
cd ports/src/
git clone https://github.com/cmatsuoka/figlet --depth=1
cd figlet/
git apply ../../figlet.diff
make CC=gcc CFLAGS="-g -O2 -Wall -std=gnu99 -I$MLIBC_ROOT/include" LD=gcc LDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group" -j$(nproc)
cp figlet ../../../base/usr/bin/
cp fonts/standard.flf ../../../base/usr/local/share/figlet/