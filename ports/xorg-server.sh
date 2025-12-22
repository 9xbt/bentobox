#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

export CC="${TOOLCHAIN_PREFIX:-}gcc"
export CXX="${TOOLCHAIN_PREFIX:-}g++"
export LD="${TOOLCHAIN_PREFIX:-}ld"
export CFLAGS="-g -O0"
export CXXFLAGS="-g -O0"

export CC_FOR_BUILD="gcc"
export CXX_FOR_BUILD="g++"
export CFLAGS_FOR_BUILD="-O2"
export LDFLAGS_FOR_BUILD=""

export PKG_CONFIG_SYSROOT_DIR="$BASE"
export PKG_CONFIG_LIBDIR="$BASE/usr/lib/pkgconfig:$BASE/usr/share/pkgconfig"
export ACLOCAL_PATH="$BASE/usr/share/aclocal"

mkdir -p base/usr
mkdir -p ports/src
git clone https://gitlab.freedesktop.org/xorg/xserver.git ports/src/xorg-server --depth=1
cd ports/src/xorg-server
git apply ../../xorg-server.diff

rm -rf build
mkdir -p build

set -e
meson setup build \
    --cross-file ../../../build/crossfile-$ARCH.txt \
    --prefix=/usr \
    --default-library=shared \
    -Ddefault_font_path=/usr/share/fonts/X11 \
    -Dxorg=true \
    -Dxv=true \
    -Dxvfb=true \
    -Dxephyr=false \
    -Dxnest=false \
    -Dxwayland=false \
    -Dsuid_wrapper=false \
    -Dpciaccess=false \
    -Ddpms=false \
    -Dscreensaver=true \
    -Dxres=false \
    -Dxvmc=false \
    -Dsystemd_logind=false \
    -Dudev=false \
    -Dudev_kms=false \
    -Ddri1=false \
    -Ddri2=false \
    -Ddri3=false \
    -Dint10=false \
    -Dvgahw=false \
    -Ddrm=false \
    -Dglamor=false \
    -Dglx=false \
    -Dhal=false \
    -Dipv6=false \
    -Dxdmcp=false \
    -Dxdm-auth-1=false \
    -Ddocs=false \
    -Ddevel-docs=false \
    -Dsystemd_logind=false \
    -Dsystemd_notify=false \
    -Dxf86-input-inputtest=false

ninja -C build
DESTDIR="$BASE" ninja -C build install
mkdir -p $BASE/var/log