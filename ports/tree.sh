#!/bin/bash
mkdir -p ports/src
cd ports/src
git clone https://github.com/Old-Man-Programmer/tree
cd tree
git apply ../../tree.patch
make CC=musl-gcc -j$(nproc)
make install PREFIX=../../../root/usr/local