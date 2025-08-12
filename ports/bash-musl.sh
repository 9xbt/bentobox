#!/bin/bash
export CC="musl-gcc"
export CFLAGS="-static -DHANDLE_MULTIBYTE -DHAVE_STRERROR -DJOB_CONTROL -std=gnu17"

cd ../bash/
./configure --host=x86_64-linux-gnu \
    --disable-nls \
    --without-bash-malloc \
    --disable-readline \
    --enable-static-link \
    bash_cv_getcwd_malloc=yes \
    ac_cv_func_getcwd=yes \
    ac_cv_func_getenv=yes \
    ac_cv_func_putenv=yes \
    ac_cv_func_setenv=yes \
    ac_cv_func_unsetenv=yes \
    ac_cv_func_gettimeofday=yes \
    ac_cv_func_gethostname=yes \
    ac_cv_func_dprintf=yes \
    ac_cv_func_isblank=yes ac_cv_func_strerror=yes \
    CC="$CC" \
    CFLAGS_FOR_BUILD="$CFLAGS"
make -j$(nproc)
mkdir -p ../bentobox/root/usr/bin
cp bash ../bentobox/root/usr/bin/bash
