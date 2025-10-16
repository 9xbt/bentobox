#!/bin/bash
mkdir -p ports/src
cd ports/src

git clone https://github.com/coreutils/gnulib --depth=1
cd gnulib
git apply ../../gnulib.diff