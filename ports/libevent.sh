#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export CXX="${TOOLCHAIN_PREFIX:-}g++"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -g -std=gnu17"
export CXXFLAGS="-I$MLIBC_ROOT/include -g -std=gnu++17"

mkdir -p $BASE/usr/bin
mkdir -p ports/src
git clone https://github.com/libevent/libevent ports/src/libevent --depth=1
cd ports/src/libevent

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
    -DEVENT__DISABLE_OPENSSL=ON \
    -DEVENT__DISABLE_REGRESS=ON \
    -DEVENT__DISABLE_MBEDTLS=ON \
    -DEVENT__DISABLE_TESTS=ON \
    -DEVENT__DISABLE_BENCHMARK=ON \
    -DEVENT__DISABLE_SAMPLES=ON \
    -DEVENT__LIBRARY_TYPE=SHARED \
    -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
make DESTDIR=$BASE install