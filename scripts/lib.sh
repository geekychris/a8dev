# Shared helpers for a8dev scripts. Source this; don't run it.
# shellcheck shell=bash

set -euo pipefail

A8_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export A8_ROOT
# This checkout's a8dev, even when .venv is a symlink to another checkout's venv
# (worktrees share one): its editable install would otherwise import that
# checkout's code.
export PYTHONPATH="$A8_ROOT/tools/a8dev/src${PYTHONPATH:+:$PYTHONPATH}"

# ---- pinned upstream versions (bump deliberately, then re-run setup) ----
# Toolchain image: cc65 (pinned commit, after V2.19) + MADS (pinned tag).
# Bump TOOLCHAIN_IMAGE's number whenever docker/Dockerfile or these change.
CC65_COMMIT="eebf54af545f4ae52f8489a2f6d4d5f93e7942d2"
MADS_REF="2.1.9"
TOOLCHAIN_IMAGE="a8-dev-toolchain:1"

# Atari800 (GPL-2.0), patched with the a8h harness (emulator/patches, emulator/ext)
ATARI800_REPO="https://github.com/atari800/atari800.git"
ATARI800_COMMIT="63a381884175e3cbfc745ff00dfb3d70d928eb50"   # tag ATARI800_7_2_1

THIRD_PARTY="$A8_ROOT/third_party"
ATARI800_DIR="$THIRD_PARTY/atari800"
# headless (libatari800, no SDL) and window (SDL2) builds of the patched emulator
ATARI800_BIN="${ATARI800_BIN:-$ATARI800_DIR/build/atari800-a8h}"
ATARI800_SDL_BIN="${ATARI800_SDL_BIN:-$ATARI800_DIR/build/atari800-a8h-sdl}"

if [ -t 1 ]; then
  _c_b=$'\033[1m'; _c_g=$'\033[32m'; _c_y=$'\033[33m'; _c_r=$'\033[31m'; _c_0=$'\033[0m'
else
  _c_b=""; _c_g=""; _c_y=""; _c_r=""; _c_0=""
fi

step() { printf '%s==> %s%s\n' "$_c_b" "$*" "$_c_0"; }
ok()   { printf '%s  ✓ %s%s\n' "$_c_g" "$*" "$_c_0"; }
warn() { printf '%s  ! %s%s\n' "$_c_y" "$*" "$_c_0" >&2; }
die()  { printf '%s  ✗ %s%s\n' "$_c_r" "$*" "$_c_0" >&2; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

host_os()   { uname -s | tr '[:upper:]' '[:lower:]'; }   # darwin | linux
host_arch() { uname -m; }                                  # arm64 | x86_64 | aarch64

ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4; }

# Clone `repo` into `dir` and check out exactly `commit` (shallow).
git_checkout_pinned() {
  local repo="$1" dir="$2" commit="$3"
  if [ ! -d "$dir/.git" ]; then
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$repo"
  fi
  if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" != "$commit" ]; then
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" checkout -q -f FETCH_HEAD
  fi
}
