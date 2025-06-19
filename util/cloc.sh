#!/bin/bash
cloc . --match-d='^(?!.*(mlibc|kernel/misc/flanterm|bin)).*'
