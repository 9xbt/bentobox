#!/bin/bash
cloc . --match-d='^(?!.*(kernel/misc/flanterm|bin|lib)).*'
