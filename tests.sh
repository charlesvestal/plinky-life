#!/bin/sh
# plinky-life - everything that can be verified without hardware.
set -e
cd "$(dirname "$0")"
sh harness/build.sh
sh harness/compile_check.sh
echo
echo "all local checks passed - the rest can only be verified on the device:"
echo "  - four playhead tints legible over a dense world at 5-bit colour"
echo "  - no stuck notes across mute / rate change / transport stop / panel unload"
