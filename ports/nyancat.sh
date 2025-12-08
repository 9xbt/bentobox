#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-g -O2"

mkdir -p base/usr/bin
mkdir -p ports/src
git clone https://github.com/klange/nyancat.git ports/src/nyancat --depth=1
cd ports/src/nyancat

make clean
set -e
make -j$(nproc)

cp src/nyancat $BASE/usr/bin