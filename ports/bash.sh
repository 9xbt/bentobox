#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-g -std=gnu17 -O2 -DHAVE_POSIX_SIGNALS -DHANDLE_MULTIBYTE -fpermissive -Wno-error"
export LIBS="-Wl,--allow-multiple-definition"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-std=gnu17"
export LDFLAGS_FOR_BUILD=""

mkdir -p $BASE/usr/bin
mkdir -p ports/src
git clone https://github.com/bminor/bash ports/src/bash
cd ports/src/bash
git checkout a8a1c2fac029404d3f42cd39f5a20f24b6e4fe4b

git apply ../../bash.diff

make clean
set -e
./configure --host=x86_64-linux-gnu \
    --disable-nls \
    --without-bash-malloc \
    bash_cv_getcwd_malloc=yes \
    bash_cv_func_strcoll_broken=no \
    bash_cv_func_sigsetjmp=present \
    ac_cv_func_getcwd=yes \
    ac_cv_func_getenv=yes \
    ac_cv_func_putenv=yes \
    ac_cv_func_setenv=yes \
    ac_cv_func_unsetenv=yes \
    ac_cv_func_strchrnul=yes \
    ac_cv_func_dprintf=yes \
    ac_cv_func_strerror=yes \
    ac_cv_func_isblank=yes \
    ac_cv_func_bcopy=yes \
    ac_cv_func_mkfifo=yes \
    ac_cv_func_strpbrk=yes \
    ac_cv_func_gethostname=yes \
    ac_cv_func_getrusage=yes \
    ac_cv_func_gettimeofday=yes \
    ac_cv_func_tcgetattr=yes \
    ac_cv_func_siginterrupt=yes \
    bash_cv_termios_ldisc=yes
sed -i 's/-rdynamic//g' Makefile
make -j$(nproc)
cp bash $BASE/usr/bin