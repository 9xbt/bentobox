#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export CXX="${TOOLCHAIN_PREFIX:-}g++"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export AR="${TOOLCHAIN_PREFIX:-}ar"
export RANLIB="${TOOLCHAIN_PREFIX:-}ranlib"
export CFLAGS="-g -O2"
export CXXFLAGS="-g -O2"

mkdir -p base/usr
mkdir -p ports/src
git clone https://github.com/madler/zlib.git ports/src/zlib --depth=1
cd ports/src/zlib

make distclean 2>/dev/null || true
make clean 2>/dev/null || true

set -e
./configure --prefix=/usr --shared

make -j"$(nproc)"
make DESTDIR=$BASE install