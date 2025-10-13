#!/bin/bash
export CC="gcc"
export CFLAGS="-I$MLIBC_ROOT/include -g -std=gnu17"
export LDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o"
export LIBS="-Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p ports/src
cd ports/src
if [ ! -d "bash" ]; then
    git clone https://github.com/bminor/bash --depth=1
fi
cd bash

make clean
#make distclean
set -e
./configure --host=x86_64-linux-gnu \
    --disable-nls \
    --without-bash-malloc \
    --enable-static-link \
    bash_cv_getcwd_malloc=yes \
    bash_cv_func_strcoll_broken=no \
    bash_cv_func_sigsetjmp=present \
    ac_cv_func_getcwd=yes \
    ac_cv_func_getenv=yes \
    ac_cv_func_putenv=yes \
    ac_cv_func_setenv=yes \
    ac_cv_func_unsetenv=yes \
    ac_cv_func_strchrnul=yes
make -j$(nproc)
cp bash ../../../base/usr/bin/