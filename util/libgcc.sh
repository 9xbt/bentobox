#!/bin/bash
[ -z "$BASE" ] || [ -z "$TARGET" ] && echo "Please run . build/mlibc-root before building GCC!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$CWD/build/$ARCH"
export PATH="$CWD/build/bin:$PATH"

set -e
cd /var/tmp/gcc-$ARCH
make all-target-libgcc -j$(nproc)
make install-target-libgcc

cp -r $PREFIX/$TARGET/lib/* $BASE/usr/lib/