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
git clone https://gitlab.freedesktop.org/pixman/pixman.git ports/src/pixman --depth=1
cd ports/src/pixman

rm -rf build
mkdir -p build

set -e
meson setup build \
    --cross-file ../../../build/crossfile-$ARCH.txt \
    --prefix=/usr \
    --default-library=shared \
    -Dgtk=disabled \
    -Dlibpng=disabled \
    -Dopenmp=disabled \
    -Dtests=disabled

ninja -C build
DESTDIR="$BASE" ninja -C build install
