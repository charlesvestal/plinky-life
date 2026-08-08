#!/bin/sh
# plinky-life - build and run the desktop tests.
#
# The four headers under src/ touch no Plinky API, so they compile natively.
# panel.cpp does NOT compile here - it has no #includes and only type-checks
# after the IDE injects the SDK headers.
set -e
cd "$(dirname "$0")/.."
cc -std=c99 -Wall -Wextra -Werror -O1 -o harness/tests harness/tests.c
./harness/tests
