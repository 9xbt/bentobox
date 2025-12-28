#!/bin/bash
[ -z "$BASE" ] || [ -z "$TARGET" ] && echo "Please run . build/mlibc-root before building GCC!" && exit 1

CWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET=x86_64-pc-bentobox
PREFIX="$CWD/build"
export PATH="$CWD/build/bin:$PATH"

set -e
cd /var/tmp/gcc
make all-target-libgcc -j$(nproc)
make install-target-libgcc

cp -r $PREFIX/$TARGET/lib/* $BASE/usr/lib/