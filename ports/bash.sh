#!/bin/bash
WD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MLIBC="$WD/../build/mlibc/x86_64"

export CC="gcc"
export CFLAGS="-I$MLIBC/include -g -std=gnu17"
export LDFLAGS="-L$MLIBC/lib -nostdlib -static $MLIBC/lib/crt0.o"
export LIBS="-Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p ports/src
cd ports/src
if [ ! -d "bash" ]; then
    git clone https://github.com/bminor/bash
fi
cd bash

make clean
#make distclean
set -e
./configure --host=x86_64-linux-gnu \
    --disable-nls \
    --without-bash-malloc \
    --without-job-control \
    --disable-readline \
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