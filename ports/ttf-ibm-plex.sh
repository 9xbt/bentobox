#!/bin/bash
[ -z "$MLIBC_ROOT" ] || [ -z "$BASE" ] && echo "Please run . build/mlibc-root before building ports!" && exit 1

mkdir -p ports/src
cd ports/src
set -e
wget https://github.com/IBM/plex/releases/download/%40ibm%2Fplex-mono%401.1.0/ibm-plex-mono.zip
unzip -o ibm-plex-mono.zip
mkdir -p $BASE/usr/share/fonts/TTF/
cp ibm-plex-mono/fonts/complete/ttf/*.ttf $BASE/usr/share/fonts/TTF/