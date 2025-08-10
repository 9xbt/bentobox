#!/bin/bash
mkdir -p base/usr/bin
curl -L -o base/usr/bin/neofetch https://raw.githubusercontent.com/dylanaraps/neofetch/refs/heads/master/neofetch
patch base/usr/bin/neofetch < ports/neofetch.patch