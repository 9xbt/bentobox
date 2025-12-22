#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1
[ ! -d "ports/src/gnulib" ] && echo "Please build gnulib before building coreutils!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-g -std=gnu17 -Wno-error"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p ports/src
git clone https://github.com/coreutils/coreutils ports/src/coreutils --depth=1
cd ports/src/coreutils
git apply ../../coreutils.diff

make clean
./bootstrap --gnulib-srcdir=../gnulib
set -e
./configure --host=x86_64-linux-gnu \
    --disable-nls \
    --prefix=/usr \
    gl_cv_func_getcwd_abort_bug=no \
    gl_cv_func_getcwd_null=yes \
    gl_cv_func_mknod_works=yes \
    gl_cv_func_working_mkstemp=yes \
    ac_cv_func_malloc_0_nonnull=yes \
    ac_cv_func_realloc_0_nonnull=yes \
    ac_cv_func_fallocate=no \
    ac_cv_func_posix_fallocate=no
make -j"$(nproc)"
make DESTDIR=$BASE install