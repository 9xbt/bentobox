#!/bin/bash
WD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MLIBC="$WD/../build/mlibc/x86_64"

mkdir -p ports/src
mkdir -p base/usr/local/share/figlet
cd ports/src/
git clone https://github.com/cmatsuoka/figlet
cd figlet/
git apply ../../figlet.diff
make CC=gcc CFLAGS="-g -O2 -Wall -std=gnu99 -I$MLIBC/include" LD=gcc LDFLAGS="-L$MLIBC/lib -nostdlib -static $MLIBC/lib/crt0.o -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group" -j$(nproc)
cp figlet ../../../base/usr/bin/
cp fonts/standard.flf ../../../base/usr/local/share/figlet/