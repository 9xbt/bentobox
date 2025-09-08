#!/bin/bash
# IMPORTANT: run from the root of the repo!

./ports/bash-prebuilt.sh
./ports/busybox.sh
./ports/doomgeneric.sh
./ports/figlet.sh
./ports/neofetch.sh
./ports/ncurses.sh
./ports/vim.sh
./ports/tree.sh
./ports/lua.sh

read -p "Clean up? (Y/n) " answer
answer=${answer:-Y}
if [[ "$answer" =~ ^[Yy]$ ]]; then
    echo "Cleaning up..."
    rm -rf ports/src/
fi
