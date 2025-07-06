#!/bin/bash
mkdir -p base/usr/bin
curl -o base/usr/bin/busybox https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox
chmod +x base/usr/bin/busybox
