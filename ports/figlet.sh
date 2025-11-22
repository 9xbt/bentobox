#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

mkdir -p ports/src
mkdir -p $BASE/usr/local/share/figlet
git clone https://github.com/cmatsuoka/figlet ports/src/figlet --depth=1
cd ports/src/figlet
git apply ../../figlet.diff
make clean
make CC="${TOOLCHAIN_PREFIX:-}gcc" CFLAGS="-g -O2 -Wall -std=gnu99 -I$MLIBC_ROOT/include" LD="${TOOLCHAIN_PREFIX:-}gcc" LDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group" -j$(nproc)
cp figlet $BASE/usr/bin
cp fonts/standard.flf $BASE/usr/local/share/figlet