#!/bin/bash
git clone https://github.com/ozkl/doomgeneric ports/src/doomgeneric
cd ports/src/doomgeneric/
git apply ../../doomgeneric.diff
cd doomgeneric/
make -f Makefile.fblinux -j$nproc
mkdir -p ../../../../base/usr/bin
cp doomgeneric ../../../../base/usr/bin/
cd ../../../../
