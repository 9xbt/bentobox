#!/bin/bash
[ -z "$MLIBC_ROOT" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -g -O2"
export LDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o -Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group"

mkdir -p base/usr/bin
mkdir -p ports/src
cd ports/src

git clone https://github.com/klange/nyancat.git --depth=1
cd nyancat

make clean
set -e
sed -i 's|\$(LDFLAGS) \$(OBJECTS)|\$(LDFLAGS) $(LIBS) $(OBJECTS) -Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group|' src/Makefile
make -j$(nproc)

cp src/nyancat ../../../base/usr/bin/