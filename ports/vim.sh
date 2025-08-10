#!/bin/bash
mkdir -p ports/src
cd ports/src/
git clone https://github.com/vim/vim.git --depth=1
cd vim
export CC=musl-gcc
export CFLAGS="-static"
export CPPFLAGS="-I/usr/local/ncurses-musl/include"
export LDFLAGS="-L/usr/local/ncurses-musl/lib -static"
./configure \
    --with-features=normal \
    --enable-multibyte \
    --disable-gui \
    --disable-gtk3-check \
    --disable-gnome-check \
    --disable-xsmp \
    --disable-x11 \
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
    --without-wlr \
    --with-tlib=ncurses \
    --prefix=/usr/local/vim-musl
make -j$(nproc)
sudo make install
