#!/usr/bin/env bash
# Check every piece of the environment; print a fix for anything broken.
source "$(dirname "$0")/lib.sh"
set +e
bad=0
fail() { warn "$1"; printf '      fix: %s\n' "$2" >&2; bad=$((bad+1)); }

step "a8dev doctor ($(host_os)/$(host_arch))"
for t in git make cc; do have "$t" && ok "$t" || fail "$t missing" "xcode-select --install (macOS) / apt install build-essential git"; done

if have docker && docker info >/dev/null 2>&1; then
  ok "docker running"
  if docker image inspect "$TOOLCHAIN_IMAGE" >/dev/null 2>&1; then
    v="$(docker run --rm "$TOOLCHAIN_IMAGE" sh -c 'cc65 --version 2>&1; mads 2>&1 | head -1 | cut -d" " -f1-2' | tr '\n' ' ')"
    ok "toolchain $TOOLCHAIN_IMAGE: $v"
  else
    fail "toolchain image $TOOLCHAIN_IMAGE missing" "scripts/build-toolchain-image.sh"
  fi
elif have cl65; then
  warn "docker not running: builds fall back to the cc65 on PATH ($(cc65 --version 2>&1 | head -1))"
else
  fail "docker not installed or not running, and no cc65 on PATH" "start Docker Desktop / Rancher / OrbStack / colima"
fi

have sdl2-config && ok "SDL2 $(sdl2-config --version)" \
  || fail "SDL2 missing (window mode unavailable; headless works)" "brew install sdl2 / sudo apt install libsdl2-dev, then scripts/build-emulator.sh"

if [ -x "$ATARI800_BIN" ]; then
  ok "Atari800 built ($ATARI800_BIN)"
  if [ -n "${ATARI800_COMMIT:-}" ]; then
    have_rev="$(git -C "$ATARI800_DIR" rev-parse HEAD 2>/dev/null)"
    [ "$have_rev" = "$ATARI800_COMMIT" ] && ok "Atari800 at the pinned commit" \
      || fail "Atari800 checkout is $have_rev, expected $ATARI800_COMMIT" "scripts/build-emulator.sh"
  fi
else
  fail "emulator not built" "scripts/build-emulator.sh"
fi

if [ -x "$A8_ROOT/.venv/bin/python" ] && "$A8_ROOT/.venv/bin/python" -c "import a8dev.emu, a8dev.mcp_server, a8dev.devbench" 2>/dev/null; then
  ok "python env (.venv) with a8dev"
else
  fail "python env missing or broken" "scripts/setup-python.sh"
fi

if [ -x "$ATARI800_BIN" ] && [ -x "$A8_ROOT/.venv/bin/python" ]; then
  if "$A8_ROOT/.venv/bin/python" - <<'PY' 2>/dev/null
from a8dev.emu import Emulator
with Emulator() as e:
    r = e.run(50)
    assert r["frames"] == 50, r
PY
  then ok "headless emulator boots and runs 50 frames"
  else fail "headless emulator smoke test failed" "scripts/build-emulator.sh; then ./a8 log hello"
  fi
fi

stale="$(ls "$A8_ROOT"/build/sessions/*.json 2>/dev/null | wc -l | tr -d ' ')"
[ "$stale" = 0 ] || ok "$stale session(s) registered (./a8 sessions)"

[ "$bad" -eq 0 ] && ok "all good" || die "$bad problem(s)"
