#!/bin/sh
# Builds tools/solve.c with the game's own src/jq.c for the host and writes
# tools/routes.json (the test bot's routes) and src/demo.h (the demo).
# tools/solve.sh -q -DJUMP_FORCE=-120: only report which levels clear.
cd "$(dirname "$0")/.." || exit 1
SDK=../../sdk   # (tmap, zxrle: in src/ here)
mkdir -p build
ARGS="tools/routes.json src/demo.h"
[ "$1" = "-q" ] && { ARGS=-q; shift; }
cc -O2 -std=c99 -Wall "$@" -I src -I $SDK/include -o build/solve tools/solve.c src/jq.c src/tmap.c src/zxrle.c src/levels_data.c && \
build/solve $ARGS
