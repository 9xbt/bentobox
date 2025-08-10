#!/bin/bash
mkdir -p ports/src
cd ports/src/
curl -LO https://invisible-island.net/datafiles/release/ncurses.tar.gz
tar xf ncurses.tar.gz
cd ncurses-*/
export CC=musl-gcc
export CFLAGS="-static"
export LDFLAGS="-static"
./configure --prefix=/usr/local/ncurses-musl --with-shared=no --with-normal --enable-static --disable-shared --without-cxx-binding
make -j$(nproc)
sudo make install
