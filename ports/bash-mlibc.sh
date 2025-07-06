#!/bin/bash
export CC="clang"
export CFLAGS="-I/opt/mlibc/include -isystem /opt/mlibc/include -ffreestanding -nostdlib -nostartfiles -fno-pic -fno-stack-protector -mcmodel=large -g -DHANDLE_MULTIBYTE -DHAVE_STRERROR -DJOB_CONTROL -std=gnu17"
export LDFLAGS="-L/opt/mlibc/lib -nostdlib -nostartfiles -static /opt/mlibc/lib/crt0.o /opt/mlibc/lib/crti.o /opt/mlibc/lib/crtn.o /opt/mlibc/lib/libc.a"
export LIBS="/opt/mlibc/lib/libc.a"

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
    CFLAGS_FOR_BUILD="$CFLAGS" \
    LDFLAGS="$LDFLAGS"
make -j$(nproc)
mkdir -p ../bentobox/base/usr/bin
cp bash ../bentobox/base/usr/bin/bash
