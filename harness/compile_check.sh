#!/bin/sh
# plinky-life - type-check the generated panel against stubbed Plinky headers.
#
# This does not produce a runnable binary and it is not a substitute for
# flashing. It catches the errors a single-file panel with no local toolchain
# otherwise only surfaces on the device: wrong argument counts, misspelled
# members, bad types, unused state.
#
# harness/plinky_stubs.h is transcribed from the published API dump. If it and
# the real firmware ever disagree, the firmware is right.
set -e
cd "$(dirname "$0")/.."

sh build/amalgamate.sh

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

{
    echo '#include "plinky_stubs.h"'
    cat plinky_life.cpp
} > "$TMP/check.cpp"

# -Wno-unused-function: life.h exposes helpers the panel does not use but the
# tests do, and the amalgamated file is one translation unit.
c++ -std=c++17 -fsyntax-only -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-function \
    -I harness "$TMP/check.cpp"

echo "compile check passed"
