#!/bin/bash
WD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MLIBC="$WD/../build/mlibc/x86_64"

export CC="gcc"
export CFLAGS="-I$MLIBC/include -g -std=gnu17 -D__bentobox__ -Wno-error -Wno-error=format-overflow -DHOST_OPERATING_SYSTEM="\"bentobox\"""
export LDFLAGS="-L$MLIBC/lib -nostdlib -static $MLIBC/lib/crt0.o"
export LIBS="-Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/bin
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
make DESTDIR="$WD/../base" install
strip $WD/../base/usr/bin/*