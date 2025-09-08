#!/bin/bash
mkdir -p ports/src
cd ports/src
git clone https://github.com/lua/lua
cd lua
git apply ../../lua.diff
make CC=musl-gcc MYCFLAGS="-std=c99 -DLUA_USE_LINUX" MYLDFLAGS=-static -j$(nproc)
cp lua ../../../root/usr/bin
