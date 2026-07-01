#!/bin/bash
echo "NOTE: only run this if using the toolchain wrappers!"
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export PATH="$DIR/cc-wrappers:$PATH"