#!/bin/bash
mkdir -p ports/src
cd ports/src/
curl -LO http://ftp.figlet.org/pub/figlet/program/unix/figlet-2.2.5.tar.gz
tar xf figlet-2.2.5.tar.gz
cd figlet-2.2.5/
patch -p0 < ../../figlet.diff
make -j$nproc
cp figlet ../../../base/usr/bin/
mkdir -p ../../../base/usr/local/share/figlet
cp fonts/standard.flf ../../../base/usr/local/share/figlet/
cd ../../../
