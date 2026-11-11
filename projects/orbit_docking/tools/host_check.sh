#!/bin/sh
# The port's autopilot against the original's, on the host. The original's
# od_core.c and the amiga3d engine it needs (a3d_math.c, a3d_fix.c, a3d.h,
# a3d_fix.h) are in the amiga_mcp repo (examples/orbit_docking,
# examples/amiga3d): pass the two directories.
#   sh tools/host_check.sh ~/code/claude_world/amiga_mcp/examples/orbit_docking ~/code/claude_world/amiga_mcp/examples/amiga3d
set -e
cd "$(dirname "$0")"
ORIG=$1 ENGINE=$2
T=${TMPDIR:-/tmp}/od_host.$$
mkdir -p "$T"
cc -O2 -DPORT -Ihost -I../src host_check.c ../src/od_core.c -o "$T/port"
echo "the port:"; "$T/port"
if [ -n "$ORIG" ]; then
    cc -O2 -I"$ORIG" -I"$ENGINE" host_check.c "$ORIG/od_core.c" "$ENGINE/a3d_math.c" "$ENGINE/a3d_fix.c" -o "$T/orig"
    echo "the original:"; "$T/orig"
fi
rm -rf "$T"
