#!/bin/sh
# The port's autopilot against the original's, on the host: pass the
# original's directory and the amiga3d engine's (amiga_mcp/examples).
#   sh tools/host_check.sh ~/code/claude_world/amiga_mcp/examples/tank_duel ~/code/claude_world/amiga_mcp/examples/amiga3d
set -e
cd "$(dirname "$0")"
ORIG=$1 ENGINE=$2
T=${TMPDIR:-/tmp}/td_host.$$
mkdir -p "$T"
cc -O2 -DPORT -Ihost -I../src host_check.c ../src/td_core.c host/obs.c host/mathb.c -o "$T/port"
echo "the port:"; "$T/port"
if [ -n "$ORIG" ]; then
    cc -O2 -I"$ORIG" -I"$ENGINE" host_check.c "$ORIG/td_core.c" "$ENGINE/a3d_math.c" "$ENGINE/a3d_fix.c" -o "$T/orig"
    echo "the original:"; "$T/orig"
fi
rm -rf "$T"
