#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export CXX="${TOOLCHAIN_PREFIX:-}g++"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-g -O2"
export CXXFLAGS="-g -O2"

export CC_FOR_BUILD="gcc"
export CXX_FOR_BUILD="g++"
export CFLAGS_FOR_BUILD="-O2"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr
mkdir -p ports/src
git clone https://gitlab.freedesktop.org/xorg/util/macros.git ports/src/util-macros --depth=1
cd ports/src/util-macros

make distclean 2>/dev/null || true
make clean 2>/dev/null || true

set -e
autoreconf -fvi
cp ../../config.sub .
./configure --host=$ARCH-pc-bentobox --prefix=/usr

make -j"$(nproc)"
make DESTDIR=$BASE install