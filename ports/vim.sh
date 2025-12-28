#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -I$BASE/usr/include -g -O2 -Wno-error"
export LIBS="-lncurses"

export CC_FOR_BUILD="gcc"
export CFLAGS_FOR_BUILD="-O2"
export LDFLAGS_FOR_BUILD=""

mkdir -p $BASE/usr/bin
mkdir -p ports/src
git clone https://github.com/vim/vim.git ports/src/vim --depth=1
cd ports/src/vim/src

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
    ac_cv_func_strptime=no \
    vim_cv_toupper_broken=no \
    vim_cv_terminfo=yes \
    vim_cv_tgetent=non-zero \
    vim_cv_tty_group=world \
    vim_cv_tty_mode=0620 \
    vim_cv_getcwd_broken=no \
    vim_cv_stat_ignores_slash=no \
    vim_cv_memmove_handles_overlap=yes
make -j"$(nproc)"
make DESTDIR=$BASE install