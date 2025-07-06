#!/bin/bash
cloc . --match-d='^(?!.*(mlibc|kernel/misc/flanterm|bin|base/usr/include/kernel/3rdparty)).*'
