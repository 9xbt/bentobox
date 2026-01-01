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
git clone https://gitlab.freedesktop.org/xorg/app/xclock.git ports/src/xclock --depth=1
cd ports/src/xclock

make distclean 2>/dev/null || true
make clean 2>/dev/null || true

set -e
sed -i '/AM_ICONV/d' configure.ac
autoreconf -fvi
cp ../../config.sub .
./configure --host=$ARCH-pc-bentobox \
    --prefix=/usr \
    --disable-static

make -j"$(nproc)"
make DESTDIR=$BASE install