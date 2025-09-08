#!/bin/bash
mkdir -p root/usr/bin
curl -L -o root/usr/bin/neofetch https://raw.githubusercontent.com/dylanaraps/neofetch/refs/heads/master/neofetch
patch root/usr/bin/neofetch < ports/neofetch.patch
chmod +x root/usr/bin/neofetch