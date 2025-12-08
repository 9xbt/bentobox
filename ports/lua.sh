#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

mkdir -p ports/src
git clone https://github.com/lua/lua ports/src/lua --depth=1
cd ports/src/lua
git apply ../../lua.diff
make clean
make CC="${TOOLCHAIN_PREFIX:-}gcc" LD="${TOOLCHAIN_PREFIX:-}gcc" -j$(nproc)
cp lua $BASE/usr/bin