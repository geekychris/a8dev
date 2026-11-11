#!/usr/bin/env bash
# Build Atari800 natively with the a8h harness (deterministic frame stepping,
# control socket, guest log, breakpoints, input recording). See
# emulator/patches/README.md.
#
# Output: third_party/atari800/build/atari800-a8h      headless (libatari800, no SDL)
#         third_party/atari800/build/atari800-a8h-sdl  window (SDL2), if SDL2 is found
#
# Needs: a C compiler, make, autoconf + automake, zlib; SDL2 for the window build.
#   macOS:  xcode-select --install; brew install autoconf automake sdl2
#   Debian: sudo apt install build-essential autoconf automake zlib1g-dev libsdl2-dev
#   Windows: MSYS2 (pacman -S base-devel autoconf automake mingw-w64-x86_64-{gcc,SDL2,zlib})
#            or WSL with the Debian packages; see docs/emulator-harness.md.
source "$(dirname "$0")/lib.sh"

step "Atari800 @ ${ATARI800_COMMIT:0:10} (7.2.1) + a8h harness"
have git  || die "git is required"
have make || die "make is required (macOS: xcode-select --install; Linux: build-essential)"
have cc   || die "a C compiler is required (macOS: xcode-select --install; Linux: build-essential)"
have autoreconf || die "autoconf/automake are required (macOS: brew install autoconf automake; Debian: apt install autoconf automake)"
want_sdl=1
have sdl2-config || { warn "sdl2-config not found: building the headless emulator only (macOS: brew install sdl2; Debian: apt install libsdl2-dev)"; want_sdl=0; }

# The source tree (third_party/atari800) is fetched once and never built in;
# builds happen in a private clone, and the binaries are swapped in with a
# rename, so emulators that are running keep their old file.
git_checkout_pinned "$ATARI800_REPO" "$ATARI800_DIR" "$ATARI800_COMMIT"
SRC="$THIRD_PARTY/atari800-build/src-tree"
mkdir -p "$THIRD_PARTY/atari800-build"
[ -d "$SRC/.git" ] || git clone -q "$ATARI800_DIR" "$SRC"
git -C "$SRC" fetch -q "$ATARI800_DIR" "$ATARI800_COMMIT" 2>/dev/null || true
git -C "$SRC" -c advice.detachedHead=false checkout -q -f "$ATARI800_COMMIT"
git -C "$SRC" reset -q --hard
git -C "$SRC" clean -q -fdx
cp "$A8_ROOT"/emulator/ext/a8h.[ch] "$SRC/src/"
for p in "$A8_ROOT"/emulator/patches/*.patch; do
  git -C "$SRC" apply --whitespace=nowarn "$p" || die "patch failed: $(basename "$p")"
  ok "applied $(basename "$p")"
done
log="$THIRD_PARTY/atari800-build/build.log"
: >"$log"
( cd "$SRC" && autoreconf -fi ) >>"$log" 2>&1 || { tail -30 "$log"; die "autoreconf failed (full log: $log)"; }

# --disable-download: Atari800 7.x can fetch ROMs from the net; the harness
# only ever uses the built-in AltirraOS / Altirra BASIC (a8h.h refuses to build otherwise).
common=(--disable-download --without-mp3 --without-readline --disable-netsio --disable-riodevice
        --disable-pbi_xld --disable-voicebox --disable-pbi_bb --disable-pbi_mio --disable-onscreenkeyboard
        --disable-crashmenu --disable-monitorbreakpoints)
export CPPFLAGS="${CPPFLAGS:-} -DA8H"

# headless: the libatari800 target (no SDL, no window) + the a8h main loop
B="$THIRD_PARTY/atari800-build/headless"
rm -rf "$B"; mkdir -p "$B"
( cd "$B" && LIBS="-lz" CFLAGS="-O2 -g -Wno-macro-redefined -Wno-unknown-warning-option" "$SRC/configure" --target=libatari800 "${common[@]}" ) >>"$log" 2>&1 \
  || { tail -30 "$log"; die "configure (headless) failed (full log: $log)"; }
make -C "$B" -j"$(ncpu)" >>"$log" 2>&1 || { tail -30 "$log"; die "build (headless) failed (full log: $log)"; }
cc -O2 -I"$B/src" -I"$SRC/src" -DA8H -DHAVE_CONFIG_H -o "$B/atari800-a8h" \
  "$A8_ROOT/emulator/ext/a8h_headless_main.c" "$B/src/libatari800.a" -lm -lz >>"$log" 2>&1 \
  || { tail -30 "$log"; die "link (headless) failed (full log: $log)"; }
ok "headless emulator built"

# window: the SDL2 target, same harness
if [ "$want_sdl" = 1 ]; then
  W="$THIRD_PARTY/atari800-build/sdl"
  rm -rf "$W"; mkdir -p "$W"
  ( cd "$W" && CFLAGS="-O2 -g" "$SRC/configure" --target=default --with-video=sdl2 --with-sound=sdl2 --without-opengl "${common[@]}" ) >>"$log" 2>&1 \
    || { tail -30 "$log"; die "configure (SDL2) failed (full log: $log)"; }
  make -C "$W" -j"$(ncpu)" >>"$log" 2>&1 || { tail -30 "$log"; die "build (SDL2) failed (full log: $log)"; }
  ok "window emulator built"
fi

install_bin() {   # copy next to the live binary, then rename over it (same filesystem)
  local from="$1" to="$2" tmp="$2.new.$$"
  mkdir -p "$(dirname "$to")"
  cp "$from" "$tmp" && chmod +x "$tmp" && mv -f "$tmp" "$to" || { rm -f "$tmp"; die "could not install $to"; }
  ok "installed $to"
}
install_bin "$B/atari800-a8h" "$ATARI800_BIN"
[ "$want_sdl" = 1 ] && install_bin "$W/src/atari800" "$ATARI800_SDL_BIN"
cp "$log" "$ATARI800_DIR/build/build.log"
"$ATARI800_BIN" -help >/dev/null 2>&1 || true
ok "done (running emulators keep the old binaries)"
