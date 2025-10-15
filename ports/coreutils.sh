#!/bin/bash
[ -z "$MLIBC_ROOT" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="gcc"
export CFLAGS="-I$MLIBC_ROOT/include -g -std=gnu17 -D__bentobox__ -Wno-error -Wno-error=format-overflow"
export LDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o"
export LIBS="-Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p ports/src
cd ports/src

if [ ! -d "coreutils" ]; then
    git clone https://github.com/coreutils/coreutils --depth=1
fi
cd coreutils
git apply ../../coreutils.diff

make clean
#make distclean
./bootstrap
cd gnulib
git apply ../../../gnulib.diff
cd ..
set -e
./configure --host=x86_64-linux-gnu \
    --disable-nls \
    --enable-static-link \
    --prefix=/usr \
    gl_cv_func_getcwd_abort_bug=no \
    gl_cv_func_getcwd_null=yes \
    gl_cv_func_mknod_works=yes \
    gl_cv_func_working_mkstemp=yes \
    ac_cv_func_malloc_0_nonnull=yes \
    ac_cv_func_realloc_0_nonnull=yes
make -j"$(nproc)"
make DESTDIR="$MLIBC_ROOT/../../../base" install
strip $MLIBC_ROOT/../../../base/usr/bin/*