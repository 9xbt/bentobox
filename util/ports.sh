#!/bin/bash

# The cwd has to be ports/.. for these to work
./ports/bash-prebuilt.sh
./ports/busybox.sh
./ports/doomgeneric.sh
./ports/figlet.sh
./ports/neofetch.sh
./ports/ncurses.sh
./ports/vim.sh

echo "Cleaning up..."
rm -rf ports/src/
cd ..
