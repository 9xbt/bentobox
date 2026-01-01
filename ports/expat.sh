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

export PKG_CONFIG_SYSROOT_DIR="$BASE"
export PKG_CONFIG_LIBDIR="$BASE/usr/lib/pkgconfig:$BASE/usr/share/pkgconfig"
export ACLOCAL_PATH="$BASE/usr/share/aclocal"

mkdir -p ports/src
git clone https://github.com/libexpat/libexpat.git ports/src/expat --depth=1
cd ports/src/expat/expat

make distclean 2>/dev/null || true
make clean 2>/dev/null || true

set -e
autoreconf -fvi
cp ../../../config.sub .
cp ../../../config.sub conftools/
./configure \
    --host=$ARCH-pc-bentobox \
    --prefix=/usr \
    --enable-shared \
    --disable-static \
    --without-xmlwf \
    --without-docbook \
    --without-examples \
    --without-tests

make -j"$(nproc)"
make DESTDIR=$BASE install