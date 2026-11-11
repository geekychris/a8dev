# Spectral Keep (Atari 130XE): work in progress, parked

Parked to do rolling_steel and scairy_fun first. The plan below is what to do next.

## What is here

- **Logic:** the Spectrum Next port's 16-bit logic, built with cc65.
  - Files: `src/game.c`, `src/room.c`, `src/world.c`, `src/levels.c`, `src/probe.c`, `src/isin.c`.
  - `src/port.c` has `rle_unpack` and `keep_log`.
- **Measurement build:** `src/main.c` is a temporary harness that times `game_step`. It uses `measure.cfg`:
  - main RAM $2000-$BBFF; BASIC is switched off by an INI chunk (`src/xchunks.s`) that also sets RAMTOP
  - no cc65 system check: it assumes the stack sits below MEMTOP
  - C stack at $1800, `LOWRAM` at $0C00-$17FF
- **asm:**
  - `src/maths.s`: `isqrt32`, `sqr16`, `smul16`; checked against C with `make MATHCHECK=1`
  - `src/gather.s`: world.c's `gather()` and move_axis's candidate scan; checked with `make GATHERCHECK=1` / `SCANCHECK=1`
  - Delete `build/obj` when switching between these check builds: the flags aren't tracked.
- **Pictures:** `tools/gen_gfx.py` builds the room pictures from the original's pieces, using `tools/export.c` (the original's renderer, run on the host) and `tools/rooms_io.py`.
  - 34 distinct pieces in 3 brightness levels, 4K in all, shared by texture class.
  - Each room has its draw list and a colour for each level in each 8-line band.
  - `--preview` writes `build/host/comp_*.png`.
  - `tools/gen_rooms.py` was the first try: whole pictures. They are too big: 24 rooms are ~100K with RLE and ~87K with LZ, against 64K of banks.
- **Profiling:** `tools/profile.py` samples where the time goes. `profile.py trace FUNC` lists every runtime helper call site in one call.

## Measured (logic only, no display DMA yet)

| | frames per logic step |
|---|---|
| as ported (cc65 C) | ~10 |
| + asm maths, gather and scan, no 32-bit ops per step, approximate lengths | **4.2** (gate room), **5.2** (Great Hall) |

The original steps 50 times a second. Earlier numbers in messages (36, 183...) came from an 8-bit frame counter that wrapped.

- **Where the time goes now:** it is spread over cc65's code for the physics. Each body does 3 axis moves and a ground probe, and each of those gathers the ~28 static boxes (walls, doors, floor).
- **Smaller costs:** `tick_actor`, `pose`/`octant`, `box_shrink80`, `body_box`, and `smul16`'s shift-add multiply (quarter squares would be faster).

## The plan (agreed with the lead)

1. **Logic at 12.5 steps a second**, with every per-step constant multiplied by 4. Sprites are interpolated between steps so motion looks smooth.
   - Tune gravity and jump speed so the original's jump arc (about 1.32 tiles high) is kept.
   - Check that the step distance (0.25 tiles) can't tunnel through thin things such as spikes or gates.
2. **Bring a step down to about 2.8 frames.** The budget is 4 frames, minus about 30% for the mode E screen's DMA.
   - Put `w_move`/`move_axis`/`probe_ground` entirely in asm.
   - Put the static boxes into the grid or a per-cell list, so `gather` doesn't scan all ~28 of them on every move.
3. **Memory:** 130XE, OS ROM off (os.s, as in boing_pinball).
   - `game.c`/`probe.c` in a bank; the room pieces and draw lists in banks.
   - A copy of the bare room in a bank, to restore behind software-drawn crates, lifts and pickups.
4. **Display:** mode E, 160 x 208 lines (one original line each; two original pixels across), plus HUD text rows.
   - A DLI every 8 lines sets PF0-PF2 (preloaded operands, as in pinball's disp.s). COLBK is the room's background.
   - Rooms are drawn on entry by an asm blitter: pieces at 2 bits a pixel, transparent where 0, shifted to the pixel.
   - Gates are redrawn when they open.
   - Spikes need their own look: they blend into the floor's colours now.
5. **Sprites:** the explorer is players 0+1 (multicolour). Rooms have at most 2 monsters, so they are players 2 and 3. Crates, lifts and pickups are drawn into the bitmap. Occlusion behind pillars and arches (masking the players) comes later.
6. **The rest:** HUD and title text, POKEY sounds, the test bot (from the Next port's `tools/bot.py` via `keep_probe`), README, arcade.json.
