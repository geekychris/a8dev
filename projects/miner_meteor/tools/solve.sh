#!/bin/sh
# Builds tools/solve.c with the game's own src/game.c for the host and writes
# tools/routes.json (the test bot's routes) and src/demo.h (the attract mode),
# then replays every route with tools/route_check.c (all six must clear).
cd "$(dirname "$0")/.." || exit 1
SDK=../../sdk
mkdir -p build
cc -O2 -std=c99 -Wall -I src -I $SDK/include -o build/solve tools/solve.c src/game.c src/levels.c \
   src/levels_data.c src/tmap.c src/plat.c && \
build/solve tools/routes.json src/demo.h && \
cc -O2 -std=c99 -Wall -I src -I $SDK/include -o build/route_check tools/route_check.c src/game.c src/levels.c \
   src/levels_data.c src/tmap.c src/plat.c && \
build/route_check tools/routes.json
