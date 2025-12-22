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

find $BASE/usr/lib -name "*.la" -delete

mkdir -p base/usr
mkdir -p ports/src
git clone https://gitlab.freedesktop.org/xorg/lib/libX11.git ports/src/libX11 --depth=1
cd ports/src/libX11

make distclean 2>/dev/null || true
make clean 2>/dev/null || true

set -e
autoreconf -fvi
cp ../../config.sub .
./configure --host=$ARCH-pc-bentobox \
    --prefix=/usr \
    --enable-shared \
    --disable-static \
    --disable-specs \
    --disable-xf86bigfont \
    --without-xmlto \
    --without-fop \
    --without-xsltproc \
    --disable-malloc0returnsnull

make -j"$(nproc)"
make DESTDIR=$BASE install
sed -i 's/^Libs\.private: *$/Libs.private: -lxcb -lXau -lXdmcp/' $BASE/usr/lib/pkgconfig/x11.pc