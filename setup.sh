#!/usr/bin/env bash
# One-shot setup for the Atari 8-bit dev environment. Safe to re-run.
#
#   ./setup.sh            everything, then build + test all projects
#   ./setup.sh --quick    skip the final build/test pass
#
# Requires: git, make, a C compiler, SDL2 (for the window; headless works
# without), Docker (any engine), Python >= 3.10 (or uv).
# macOS (Apple Silicon / Intel) and Linux (x86_64 / arm64). Windows: WSL2.
source "$(dirname "$0")/scripts/lib.sh"

QUICK=0
for a in "$@"; do
  case "$a" in
    --quick) QUICK=1 ;;
    -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "unknown option $a" ;;
  esac
done

step "prerequisites ($(host_os)/$(host_arch))"
missing=()
for t in git make cc; do have "$t" || missing+=("$t"); done
if [ ${#missing[@]} -gt 0 ]; then
  if [ "$(host_os)" = darwin ]; then
    die "missing: ${missing[*]} – run: xcode-select --install"
  else
    die "missing: ${missing[*]} – e.g. sudo apt install git build-essential"
  fi
fi
if have docker; then ok "git, make, cc, docker"
elif have cl65; then warn "no Docker: builds use the cc65 on PATH ($(cc65 --version 2>&1 | head -1))"
else die "Docker is required for the cc65 toolchain (or a cc65 on PATH). Install Docker Desktop, Rancher Desktop, OrbStack or colima (macOS) / docker-ce (Linux)."
fi
if ! have sdl2-config; then
  if [ "$(host_os)" = darwin ]; then warn "SDL2 missing: no emulator window (brew install sdl2), headless still works"
  else warn "SDL2 missing: no emulator window (sudo apt install libsdl2-dev), headless still works"; fi
fi

have docker && "$A8_ROOT/scripts/build-toolchain-image.sh"
"$A8_ROOT/scripts/build-emulator.sh"
"$A8_ROOT/scripts/setup-python.sh"

if [ "$QUICK" = 0 ]; then
  step "build + test all projects"
  "$A8_ROOT/a8" selftest
fi

cat <<EOT

${_c_g}Atari 8-bit dev environment ready.${_c_0}

  ./a8 run hello           hello in a window
  ./a8 new mygame          start a project from the template
  ./a8 test mygame         headless tests
  ./a8 devbench --open     web UI + REST + MCP over HTTP (:3680)
  ./a8 doctor              diagnose problems

Claude Code: open this directory with \`claude\`; the "a8" MCP server in .mcp.json
gives Claude build/boot/screenshot/input/memory/debugger tools (see CLAUDE.md).
EOT
