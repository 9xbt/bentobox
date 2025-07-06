#!/bin/bash
mkdir -p base/usr/bin
curl -L -o base/usr/bin/bash -H "User-Agent: curl" https://github.com/robxu9/bash-static/releases/download/5.1-actions-1/bash-ubuntu-latest
chmod +x base/usr/bin/bash
