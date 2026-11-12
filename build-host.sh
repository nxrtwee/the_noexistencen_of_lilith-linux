#!/bin/sh
set -eu
cd "$(dirname "$0")"
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build/cmake --parallel "${JOBS:-2}"
