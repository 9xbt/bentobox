#!/bin/bash
sudo chroot root /usr/bin/bash -c "export PATH=\$PATH:/bin; exec /usr/bin/bash -i"