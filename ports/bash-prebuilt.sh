#!/bin/bash
mkdir -p root/usr/bin
curl -L -o root/usr/bin/bash -H "User-Agent: curl" https://github.com/robxu9/bash-static/releases/download/5.1-actions-1/bash-ubuntu-latest
chmod +x root/usr/bin/bash
mkdir -p root/bin
ln -s /usr/bin/bash root/bin/bash
