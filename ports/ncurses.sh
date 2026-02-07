#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -g -O2 -Wno-error -D_GNU_SOURCE"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-O2"
export LDFLAGS_FOR_BUILD=""

mkdir -p $BASE/usr/bin
mkdir -p $BASE/usr/lib
mkdir -p ports/src

git clone https://github.com/mirror/ncurses ports/src/ncurses --depth=1
cd ports/src/ncurses
make clean
set -e
./configure --host=x86_64-linux-gnu \
    --prefix=/usr \
    --with-shared \
    --with-normal \
    --without-static \
    --without-debug \
    --without-cxx-binding \
    --without-ada \
    --disable-echo \
    --enable-widec \
    --enable-pc-files \
    --with-pkg-config-libdir=/usr/lib/pkgconfig \
    --with-terminfo-dirs=/usr/share/terminfo:/etc/terminfo \
    --with-default-terminfo-dir=/usr/share/terminfo \
    ac_cv_func_malloc_0_nonnull=yes \
    ac_cv_func_realloc_0_nonnull=yes
make -j"$(nproc)"
make DESTDIR=$BASE INSTALL_PROG="/usr/bin/install -c" install