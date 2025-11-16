#!/bin/bash
[ -z "$MLIBC_ROOT" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -g -O2 -Wno-error"
export LDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o"
export LIBS="-Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-O2"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p base/usr/lib
mkdir -p ports/src
cd ports/src

git clone https://github.com/mirror/ncurses --depth=1
cd ncurses
make clean
set -e
./configure --host=x86_64-linux-gnu \
    --prefix=/usr \
    --with-shared=no \
    --with-normal \
    --enable-static \
    --disable-shared \
    --without-debug \
    --without-cxx-binding \
    --without-ada \
    --disable-echo \
    --enable-pc-files \
    --with-pkg-config-libdir=/usr/lib/pkgconfig \
    --with-terminfo-dirs=/usr/share/terminfo:/etc/terminfo \
    --with-default-terminfo-dir=/usr/share/terminfo \
    ac_cv_func_malloc_0_nonnull=yes \
    ac_cv_func_realloc_0_nonnull=yes
make -j"$(nproc)"
make DESTDIR="$MLIBC_ROOT/../../../base" install
"${TOOLCHAIN_PREFIX}strip" $MLIBC_ROOT/../../../base/usr/bin/*