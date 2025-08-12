#!/bin/bash
cloc . --match-d='^(?!.*(root|bin|lib|ports/src)).*'
