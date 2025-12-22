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

mkdir -p base/usr
mkdir -p ports/src
cd ports/src
if [ ! -d nettle-3.9.1 ]; then
    wget https://ftp.gnu.org/gnu/nettle/nettle-3.9.1.tar.gz
    tar xf nettle-3.9.1.tar.gz
fi
cd nettle-3.9.1

make distclean 2>/dev/null || true
make clean 2>/dev/null || true

set -e
cp ../../config.sub .
./configure --host=$ARCH-pc-bentobox \
    --prefix=/usr \
    --enable-shared \
    --disable-static \
    --disable-documentation \
    --disable-openssl \
    --enable-mini-gmp

make -j"$(nproc)" libnettle.a libhogweed.a
make DESTDIR=$BASE install-here install-headers
mkdir -p $BASE/usr/lib/pkgconfig
cp nettle.pc hogweed.pc $BASE/usr/lib/pkgconfig/