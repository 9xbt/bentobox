#!/bin/bash
[ -z "$MLIBC_ROOT" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -I$MLIBC_ROOT/../../../base/usr/include -g -O2 -Wno-error"
export LDFLAGS="-L$MLIBC_ROOT/lib -L$MLIBC_ROOT/../../../base/usr/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o"
export LIBS="-Wl,--allow-multiple-definition -Wl,--start-group -lncurses -lc -lgcc -lgcc_eh -Wl,--end-group"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-O2"
export LDFLAGS_FOR_BUILD=""

mkdir -p base/usr/bin
mkdir -p ports/src
cd ports/src

git clone https://github.com/vim/vim.git --depth=1
cd vim/src

make clean
set -e
./configure --host=x86_64-linux-gnu \
    --prefix=/usr \
    --with-features=huge \
    --enable-multibyte \
    --disable-gui \
    --disable-gtk3-check \
    --disable-gnome-check \
    --disable-xsmp \
    --disable-xim \
    --disable-selinux \
    --disable-cscope \
    --disable-netbeans \
    --disable-python3interp \
    --disable-pythoninterp \
    --disable-luainterp \
    --disable-perlinterp \
    --disable-rubyinterp \
    --without-x \
    --without-wayland \
    --with-tlib=ncurses \
    ac_cv_small_wchar_t=no \
    vim_cv_toupper_broken=no \
    vim_cv_terminfo=yes \
    vim_cv_tgetent=non-zero \
    vim_cv_tty_group=world \
    vim_cv_tty_mode=0620 \
    vim_cv_getcwd_broken=no \
    vim_cv_stat_ignores_slash=no \
    vim_cv_memmove_handles_overlap=yes
make -j"$(nproc)"
make DESTDIR="$MLIBC_ROOT/../../../base" install
strip "$MLIBC_ROOT/../../../base/usr/bin/vim"