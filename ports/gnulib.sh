#!/bin/bash
mkdir -p ports/src
git clone https://github.com/coreutils/gnulib ports/src/gnulib --depth=1
cd ports/src/gnulib
git apply ../../gnulib.diff