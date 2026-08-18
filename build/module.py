#!/usr/bin/env python3
import os
import sys

def parse(f, package):
    is_packages = False
    while True:
        line = f.readline().rstrip('\n')
        if line.startswith('/*'):
            is_packages = True
        if is_packages and ' * @package ' + package in line:
            print(f.name)
            break
        if is_packages and line.startswith(' */'):
            break        

if __name__ == '__main__':
    dir     = sys.argv[1]
    package = sys.argv[2]

    with os.scandir(dir) as files:
        for file in files:
            with open(file.path) as f:
                parse(f, package)
