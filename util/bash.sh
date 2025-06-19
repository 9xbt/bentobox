#!/bin/bash
mkdir -p base/usr/bin
cd ../bash/
make clean; make -j12; cp bash ../bentobox/base/usr/bin/bash
