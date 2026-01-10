#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export CXX="${TOOLCHAIN_PREFIX:-}g++"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -g -std=gnu17"
export CXXFLAGS="-I$MLIBC_ROOT/include -g -std=gnu++17"

mkdir -p $BASE/usr/bin
mkdir -p ports/src
git clone https://github.com/9xbt/nes ports/src/nes_emu --depth=1
cd ports/src/nes_emu
rm -rf build
mkdir -p build
cd build

set -e
cmake .. \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_COMPILER_WORKS=1 \
    -DCMAKE_CXX_COMPILER_WORKS=1 \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$BASE/usr
make -j$(nproc)
cp nes_emu $BASE/usr/bin/