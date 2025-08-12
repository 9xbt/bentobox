#!/bin/bash
target=$(pwd)/root/usr/local/vim
mkdir -p ports/src
cd ports/src/
git clone https://github.com/vim/vim.git --depth=1
cd vim
git apply ../../vim.diff
export CC=musl-gcc
export CFLAGS="-static"
export CPPFLAGS="-I/usr/local/ncurses-musl/include"
export LDFLAGS="-L/usr/local/ncurses-musl/lib -static"
./configure \
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
    --prefix=$target
make -j$(nproc)
make install
