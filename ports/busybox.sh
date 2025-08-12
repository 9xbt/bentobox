#!/bin/bash
mkdir -p root/usr/bin
curl -o root/usr/bin/busybox https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox
chmod +x root/usr/bin/busybox

mkdir -p root/bin
cd root
for applet in $(./usr/bin/busybox --list); do
  if [ "$applet" != "init" ]; then
    ln -sf /usr/bin/busybox bin/"$applet"
  fi
done
