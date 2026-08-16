#!/bin/bash
# configure.sh — minimal version, literally just runs cmake -S ..

mkdir -p build   # create build dir if missing
cd build
cmake -S .. "$@"  # "$@" passes any extra args you give (e.g. -DCMAKE_BUILD_TYPE=Debug -G Ninja)
