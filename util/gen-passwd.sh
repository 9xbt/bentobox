#!/bin/bash
echo root:$(echo -n $(openssl passwd -6)):0:0::/root:/usr/bin/bash > base/etc/passwd