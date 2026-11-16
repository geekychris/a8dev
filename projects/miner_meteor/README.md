# Miner Meteor (Atari 8-bit)

Port of `amiga_mcp/examples/miner_meteor` (by way of the ZX Spectrum port),
a single-screen platformer: in each of six caverns, collect every key to
open the exit, then reach it before the air runs out. Spikes, conveyor
belts, floors that crumble under you, and guardians patrolling the ledges.
100 points a key, 1000 a cavern plus half the air left. Three lives.

![title](screenshots/title.png) ![play](screenshots/play.png)

![Miner Meteor's demo](screenshots/miner_meteor.gif)

```sh
./a8 build miner_meteor            # RELEASE=1: no harness log
./a8 run miner_meteor
./a8 test miner_meteor             # the bot, the demo (test.py)
```

## Controls

| | |
|---|---|
| walk | joystick left / right (or A / D, arrow keys) |
| jump | FIRE, SPACE or joystick up |
| start | FIRE or START on the title screen |
| OPTION | music on/off |
| give up | ESC during a game |
| initials | type three letters, or UP/DOWN + FIRE, after a high score |

Left alone, the title starts a demo that plays all six caverns.

## What was kept, what changed

`src/game.c` is the original's `game.cpp` as the ZX Spectrum port has it
(the `MinerGame` methods over one `GameState`), `src/levels.txt` its six
caverns row for row (with the original's three fixed rows) and
`src/levels.c` its guardians; `src/plat.c`/`src/tmap.c` are the Spectrum
SDK's helpers holding the original's `update_player` collision code. The
guardians, scoring, air, collapse timers, conveyors, jump edge-trigger and
collision snaps are unchanged, and the logic runs at a fixed 50 ticks a
second, as on the Amiga. The proof: the demo (`src/demo.h`, the fastest
route through each cavern found by the Spectrum port's solver over the same
`game_tick()`) plays all six caverns here without losing a life.
The original's later fixes (amiga_games #53) are in too: Hazard Bay's row 5
meets the right wall, Guardian Alley's first guardian patrols on the top
ledge (y 32, not inside it at 48), and the walk cycle shows all eight of
the sheet's frames. With the guardian on the ledge the solver's fastest
route through Guardian Alley is 259 ticks (211 before), the same count as
the original's and the ST port's checks.

Port changes (marked `port:` in the Spectrum port's source and here):

- the caverns are stored as plain bytes (`tools/mklevels.py`), not zxrle
- the death flash: the original restarts the cavern on the same tick; the
  port flashes it for half a second first
- ESC gives up a game; a demo, a title tune and sound effects (the Amiga's
  sound is a stub), a high-score table with initials

## The Atari version

- **Full-size caverns in ANTIC 5.** The cavern's rows 0-13 (row 14 is a
  second wall row under the floor) are 14 ANTIC 5 character rows: a tile is
  two characters, 8 colour clocks x 16 scan lines, the original's 16 x 16
  pixels at one scan line a pixel. Walls in COLPF0, coloured by the
  cavern's `palette_variant` (which the original defines but doesn't use),
  belts and crumbling floors COLPF1, spikes and the closed exit COLPF2,
  keys and the open exit COLPF3, whose colour cycles.
- **Belts move** by rewriting their two characters (8 frames), and a
  crumbling floor sinks a line at a time (8 character pairs) as its timer
  runs down.
- **The miner is player 0, the guardians players 1-3**, at the full
  resolution (one scan line an original pixel); frames come from the
  original's tile sheet (`tools/mkart.py`).
- **Status**: an ANTIC 6 line: the cavern's name for two seconds, then
  score, the air as a bar, lives.
- **POKEY**: jump, key, death, cavern cleared, game over; the tune on the
  title and during the game (OPTION turns it off).

## Memory and speed

- Program, data and variables `$2000-$701C` (20K; the XEX is 18.4K in a
  release build). Character set `$A000`, cavern `$A400`, display lists and
  title from `$A700`; players at `$9800`.
- No 130XE banks: it runs on a 64K 800XL (the test boots it there) and on a
  48K machine with BASIC off.
- 50.0 fps in the emulator (PAL) over the demo in a release build; 49 fps
  with the harness log. When a frame is late the logic runs twice, so the
  game plays at the original's speed.

## Test

`./a8 test miner_meteor`: a bot follows the solver's route
(`tools/routes.json`) through cavern 1, setting the keys for each logic
tick at a breakpoint on `game_tick()`; in cavern 2 it walks into the spikes
until the game is over, enters BOT in the high scores and is back on the
title (recorded, replayed twice with every frame hash checked). Then the
demo plays all six caverns to the win screen at 45+ fps, and the game boots
on a 64K 800XL. `.venv/bin/python projects/miner_meteor/test.py --shots`
rewrites the screenshots and the GIF. (`tools/solve.c`/`solve.sh` are the
Spectrum port's route search, kept with the routes they made.)
