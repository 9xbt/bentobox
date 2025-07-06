#!/bin/bash

# The cwd has to be ports/.. for these to work
./ports/bash-prebuilt.sh
./ports/busybox.sh
./ports/doomgeneric.sh
./ports/figlet.sh

echo "Cleaning up..."
rm -rf ports/src/
cd ..
