#!/usr/bin/env bash
# Check every piece of the environment; print a fix for anything broken and,
# at the end, one command that installs all missing system packages.
# Works on macOS (Homebrew), Debian/Ubuntu (apt), Fedora (dnf), Arch (pacman),
# openSUSE (zypper) and Windows through WSL2 (one of those Linuxes).
source "$(dirname "$0")/lib.sh"
set +e
bad=0
pkgs=()          # system packages to install (names for this package manager)
fail() { warn "$1"; printf '      fix: %s\n' "$2" >&2; bad=$((bad+1)); }

# --- platform + package manager
os="$(host_os)"; wsl=0
if [ "$os" = linux ] && grep -qi microsoft /proc/version 2>/dev/null; then wsl=1; fi
pm=""
if [ "$os" = darwin ]; then have brew && pm=brew
else for c in apt-get dnf pacman zypper; do have "$c" && { pm="$c"; break; }; done; fi
# package names: pkg <generic> -> name for $pm
pkg() {
  case "$pm:$1" in
    brew:cc|brew:make|brew:git) echo "";;            # xcode-select --install
    brew:sdl2) echo sdl2;; brew:python) echo python@3.12;; brew:*) echo "$1";;
    apt-get:cc) echo build-essential;; apt-get:make) echo build-essential;;
    apt-get:sdl2) echo libsdl2-dev;; apt-get:zlib) echo zlib1g-dev;; apt-get:python) echo "python3 python3-venv";;
    dnf:cc) echo gcc;; dnf:sdl2) echo SDL2-devel;; dnf:zlib) echo zlib-devel;; dnf:python) echo python3;;
    pacman:cc) echo base-devel;; pacman:make) echo base-devel;; pacman:sdl2) echo sdl2;; pacman:python) echo python;;
    zypper:cc) echo gcc;; zypper:sdl2) echo libSDL2-devel;; zypper:zlib) echo zlib-devel;; zypper:python) echo python3;;
    *:*) echo "$1";;
  esac
}
need_pkg() { local n; n="$(pkg "$1")"; [ -n "$n" ] && pkgs+=($n); }
install_cmd() {
  case "$pm" in
    brew) echo "brew install";; apt-get) echo "sudo apt-get install -y";; dnf) echo "sudo dnf install -y";;
    pacman) echo "sudo pacman -S --needed";; zypper) echo "sudo zypper install -y";; *) echo "install:";;
  esac
}

step "a8dev doctor ($os/$(host_arch)$([ $wsl = 1 ] && echo ', WSL2')${pm:+, $pm})"
if [ "$os" != darwin ] && [ "$os" != linux ]; then
  fail "unsupported host $(uname -s)" "on Windows use WSL2 (wsl --install -d Ubuntu), then run ./a8 setup inside it"
fi
[ -z "$pm" ] && warn "no known package manager (brew/apt/dnf/pacman/zypper): install the packages named below by hand"

for t in git make; do have "$t" && ok "$t" || { fail "$t missing" "$( [ $os = darwin ] && echo 'xcode-select --install' || echo "$(install_cmd) $(pkg $t)")"; need_pkg "$t"; }; done
if have cc; then ok "C compiler ($(cc --version 2>&1 | head -1))"
else fail "C compiler missing (needed to build Atari800)" "$( [ $os = darwin ] && echo 'xcode-select --install' || echo "$(install_cmd) $(pkg cc)")"; need_pkg cc; fi
for t in autoconf automake; do
  have "$t" && ok "$t" || { fail "$t missing (needed to build Atari800)" "$(install_cmd) $t"; need_pkg "$t"; }
done
if [ "$os" = linux ]; then
  if printf '#include <zlib.h>\nint main(void){return 0;}\n' | cc -x c - -o /dev/null -lz 2>/dev/null; then ok "zlib headers"
  else fail "zlib headers missing (Atari800 needs them)" "$(install_cmd) $(pkg zlib)"; need_pkg zlib; fi
fi

# --- python
py=""
for c in python3.13 python3.12 python3.11 python3.10 python3; do
  if have "$c" && "$c" -c 'import sys; sys.exit(sys.version_info < (3,10))' 2>/dev/null; then py="$c"; break; fi
done
if have uv; then ok "uv $(uv --version 2>/dev/null | cut -d' ' -f2) (makes .venv)"
elif [ -n "$py" ]; then
  ok "python $($py -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
  "$py" -c 'import venv, ensurepip' 2>/dev/null || { fail "python venv/ensurepip missing" "$(install_cmd) $(pkg python)"; need_pkg python; }
else fail "Python >= 3.10 missing" "$(install_cmd) $(pkg python)   (or install uv: https://docs.astral.sh/uv/)"; need_pkg python; fi

# --- toolchain
if have docker && docker info >/dev/null 2>&1; then
  ok "docker running"
  if docker image inspect "$TOOLCHAIN_IMAGE" >/dev/null 2>&1; then
    v="$(docker run --rm "$TOOLCHAIN_IMAGE" sh -c 'cc65 --version 2>&1; mads 2>&1 | head -1 | cut -d" " -f1-2' | tr '\n' ' ')"
    ok "toolchain $TOOLCHAIN_IMAGE: $v"
  else
    fail "toolchain image $TOOLCHAIN_IMAGE missing" "scripts/build-toolchain-image.sh"
  fi
elif have docker; then
  if [ "$os" = linux ] && docker info 2>&1 | grep -qi "permission denied"; then
    fail "docker: permission denied on the socket" "sudo usermod -aG docker \$USER, then log out and in"
  elif [ $wsl = 1 ]; then
    fail "docker not reachable from WSL" "Docker Desktop -> Settings -> Resources -> WSL integration: enable this distro (or install docker-ce inside WSL)"
  else
    fail "docker installed but not running" "start Docker Desktop / Rancher / OrbStack / colima, or: sudo systemctl start docker"
  fi
elif have cl65; then
  warn "no Docker: builds use the cc65 on PATH ($(cc65 --version 2>&1 | head -1); A8_NATIVE=1)"
else
  fail "no Docker and no cc65" "install Docker (docs.docker.com/engine/install; on Windows: Docker Desktop with WSL integration), or $(install_cmd) cc65 and use A8_NATIVE=1"
fi

# --- emulator
if have sdl2-config; then ok "SDL2 $(sdl2-config --version) (emulator window)"
else fail "SDL2 missing (no emulator window; headless works)" "$(install_cmd) $(pkg sdl2), then scripts/build-emulator.sh"; need_pkg sdl2; fi
if [ $wsl = 1 ] && [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
  warn "WSL without a display: ./a8 run needs WSLg (Windows 11) or an X server; headless tests work"
fi
if [ -x "$ATARI800_BIN" ]; then
  ok "Atari800 (headless) $ATARI800_BIN"
  [ -x "${ATARI800_SDL_BIN:-/nonexistent}" ] && ok "Atari800 (window) $ATARI800_SDL_BIN" \
    || warn "no window build of Atari800 (needs SDL2; scripts/build-emulator.sh)"
  if [ -n "${ATARI800_COMMIT:-}" ] && [ -d "$ATARI800_DIR/src/.git" -o -d "$ATARI800_DIR/.git" ]; then
    have_rev="$(git -C "$ATARI800_DIR/src" rev-parse HEAD 2>/dev/null || git -C "$ATARI800_DIR" rev-parse HEAD 2>/dev/null)"
    [ "$have_rev" = "$ATARI800_COMMIT" ] && ok "Atari800 at the pinned commit" \
      || fail "Atari800 checkout is ${have_rev:-unknown}, expected $ATARI800_COMMIT" "scripts/build-emulator.sh"
  fi
else
  fail "emulator not built" "scripts/build-emulator.sh"
fi

if [ -x "$A8_ROOT/.venv/bin/python" ] && "$A8_ROOT/.venv/bin/python" -c "import a8dev.emu, a8dev.mcp_server, a8dev.devbench, PIL" 2>/dev/null; then
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

if [ ${#pkgs[@]} -gt 0 ] && [ -n "$pm" ]; then
  uniq="$(printf '%s\n' "${pkgs[@]}" | awk '!s[$0]++' | tr '\n' ' ')"
  printf '\n  all missing system packages in one go:\n    %s %s\n  then: ./a8 setup\n' "$(install_cmd)" "$uniq" >&2
fi
[ "$bad" -eq 0 ] && ok "all good" || die "$bad problem(s)"
