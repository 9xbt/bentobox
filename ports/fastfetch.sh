#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export CXX="${TOOLCHAIN_PREFIX:-}g++"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-I$MLIBC_ROOT/include -g -std=gnu17"
export CXXFLAGS="-I$MLIBC_ROOT/include -g -std=gnu++17"
export LDFLAGS="-L$MLIBC_ROOT/lib -nostdlib -static $MLIBC_ROOT/lib/crt0.o"

mkdir -p $BASE/usr/bin
mkdir -p ports/src
git clone https://github.com/fastfetch-cli/fastfetch ports/src/fastfetch -b master --depth=1
cd ports/src/fastfetch
git apply ../../fastfetch.diff
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
    -DCMAKE_C_STANDARD_LIBRARIES="-Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group" \
    -DCMAKE_CXX_STANDARD_LIBRARIES="-Wl,--allow-multiple-definition -Wl,--start-group -lc -lgcc -lgcc_eh -Wl,--end-group" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_RPM=OFF \
    -DENABLE_ZLIB=OFF \
    -DENABLE_SYSTEM_YYJSON=OFF \
    -DINSTALL_LICENSE=OFF \
    -DBUILD_FLASHFETCH=OFF \
    -DENABLE_CHAFA=OFF \
    -DENABLE_DBUS=OFF \
    -DENABLE_DCONF=OFF \
    -DENABLE_DDCUTIL=OFF \
    -DENABLE_DRM=OFF \
    -DENABLE_ELF=OFF \
    -DENABLE_EGL=OFF \
    -DENABLE_GIO=OFF \
    -DENABLE_GLX=OFF \
    -DENABLE_IMAGEMAGICK6=OFF \
    -DENABLE_IMAGEMAGICK7=OFF \
    -DENABLE_OPENCL=OFF \
    -DENABLE_OSMESA=OFF \
    -DENABLE_PULSE=OFF \
    -DENABLE_SQLITE3=OFF \
    -DENABLE_VULKAN=OFF \
    -DENABLE_WAYLAND=OFF \
    -DENABLE_XCB_RANDR=OFF \
    -DENABLE_XFCONF=OFF \
    -DENABLE_XRANDR=OFF \
    -DENABLE_DRM_AMDGPU=OFF \
    -DBUILD_TESTS=OFF
make -j$(nproc)
cp fastfetch $BASE/usr/bin