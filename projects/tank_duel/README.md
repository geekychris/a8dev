# Tank Duel (Atari 8-bit, "Atari edition")

Port of `amiga_mcp/examples/tank_duel`: a tank fight in a walled arena,
seen from the turret. Destroy every enemy tank. **Scouts** circle and
snipe, **mortars** lob shells where you will be, **bulwarks** carry a
shield across their front that shots glance off. Shells bounce once off the
walls, so a shot can be banked round cover, and crates break. There are
eight rounds, and you get +30 armour between them. The Spectrum Next port
(`spectrum_dev/projects/tank_duel`) was the reference.

![title](screenshots/title.png) ![play](screenshots/play.png) ![a fight](screenshots/fight.png)

![Tank Duel: the title, then the autopilot fights round 1](screenshots/tank_duel.gif)

```sh
./a8 build tank_duel                # build/tank_duel.xex (XL/XE, 64K)
./a8 run tank_duel
./a8 test tank_duel
./a8 build tank_duel RELEASE=1      # no harness log
python3 projects/tank_duel/tools/make_gif.py
```

## Controls

| | |
|---|---|
| forward / back | joystick up / down |
| turn | joystick left / right |
| fire | FIRE |
| pause | OPTION |
| autopilot on / off | SELECT (it also fights behind the title) |
| back to the title | ESC |
| title: the round to start at | up / down; FIRE starts |

## The Atari edition: what's kept, what changed

**Kept:** the original's rules (`src/td_core.c`, `src/td_ai.c`, from its
`td_core.c` via the Next port):

- driving and collisions;
- shells banked once off the walls, crates that break, the bulwark's
  shield with glances, mortar lobs and their splash;
- the three enemies' minds, the eight rounds and their line-ups;
- the scoring (100, 200 or 300 a tank, 20 a crate, 500 × the round for
  clearing it, 5 a point of armour left);
- the autopilot.

**Changed (`port:` in the code):**

- **16 bits.** The original works in 16.16 fixed point, which is 32-bit
  arithmetic and library calls in cc65.
  - Positions here are 1/16 of a unit in int16.
  - Sines are Q8, lengths are alpha-max-beta-min (within 4%), and angles
    come from a table (no division).
- **12.5 ticks a second.** Each tick is two of the original's: speeds,
  turns and shell steps are doubled, and the timers count half as many
  ticks.
- **Enemies think every other tick** (half of them each tick), and every
  fourth tick with more than 3 enemies. In between they drive on their
  last decision. So they decide 6.25 times a second (3.1 in big rounds)
  and fire on those ticks.
- `tools/run_orig.c` / `tools/run_port.c` play the autopilot over 20
  seeds on the host, original against port. The fights end up alike but
  not bit for bit: the port reaches round 3.1 on average (2.6 in the
  original) with 6.1 kills (5.2).
- **High scores:** five, with initials. The original keeps the best score
  only.

## What the look gives up

- **Colours.** Three colours and their dithers for everything, plus the
  sky and floor as background colours (a DLI at the horizon). The
  original has 32 pens: the enemy types differ there by colour, here only
  by shape and shade.
- **No floor grid.** Pillars are square, not 8-sided.
- **Simplified shapes:**
  - A scout is a hull and a turret, a mortar a box and a tube, a bulwark
    a hull, a turret and its shield.
  - There are no barrels, and turrets are dropped beyond 450 units.
  - Objects beyond 350 units are one polygon: the outline between their
    outer edges, in one shade.
- **Draw limits:** nothing beyond 1100 units, and at most 10 objects drawn
  (the nearest). The radar still shows every tank.
- **Mortar rings** are blinking marks on the radar, not rings on the
  ground.
- **Small effects:** explosions are flashing squares, and shells and
  mortar rounds are dots.
- **No help pages.**

## How it uses the Atari

| | |
|---|---|
| **a8_3d** (projects/a8_3d) | ANTIC D, 160 × 80, double buffered, flat and dithered polygons. Everything standing is a vertical prism or a pyramid: footprints are turned in 2D (asm), a3d projects them, faces are culled by comparing the camera's place with the box (no products), and faces go straight to `a3d_poly` when nothing is behind the near plane. Walls are clipped to the view in camera space first, so they reach a3d as on-screen trapezoids. |
| **DLI** | the floor's colour from the horizon, the HUD's colours |
| **players / missiles** | the radar (players 2 and 3, double width; missile 3 the mortar marks), the sight (missile 0) |
| **ANTIC 2** | the HUD: round, enemies, score, the armour and gun bars |
| **POKEY** | effects: shots, explosions, lobs, glances, hits, a round won, defeat |
| **RAM under the OS ROM** | the OS ROM is switched off (`os.s`), so a **64K 800XL** runs it |

## Memory (64K XL/XE; no 130XE needed)

| | |
|---|---|
| `$0C00-$1B72` | a8_3d's tables and work arrays, the game state, the view's arrays |
| `$1C00-$1FBA` | asm: the tank's drive, the shells' flight (loaded there: no DOS) |
| `$2000-$94FF` | the program: a8_3d (with its convex-object path), the view, the front end, the maths, the SDK |
| `$9600-$97FF` | C stack |
| `$9800-$99FF` | the font's capitals and digits (copied from the ROM) |
| `$9B00-$9FFF` | players and missiles |
| `$A000-$BFFF` | a8_3d's two buffers, display lists, text rows |
| `$C000-$CE78` | under the ROM: an enemy's mind (`think.s`), the autopilot (`td_ai.c`), the radar |
| `$D800-$FF2B` | under the ROM: the rules (`td_core.c`; in asm: `apart.s`, `pushout.s`), the sine table (`td_fix.c`) |

The XEX loads the code for under the ROM at `$9800`, a part at a time. A
routine at `$0600`, run by INITAD, switches the ROM off and copies each
part up (`xload.s`).

## Speed

Measured in the emulator (PAL) with the `td: fps10=` lines, turning on the
spot at the start of a round (`inview10` is ten times the objects in view):

| | frame rate | a tick of the rules |
|---|---|---|
| **round 1** (2 enemies, 2–5 objects in view) | **3.8–6.5 fps** | 18–19K cycles |
| **round 4** (4 enemies) | 3.0–5.0 fps; 3.0 with ~8 objects in view | 23–30K |
| **round 8** (7 enemies) | 3.5–3.6 fps | 39–45K |
| over the test's autopilot game | 3.2–4.4 fps, 3.5 on average | |

A picture costs 168–350K cycles: objects ~65%, walls ~25%, clearing ~7%.
The rules run 12.5 ticks a second. These are wall-clock cycles,
including ANTIC's DMA and the interrupts. In CPU cycles, a round-8 tick is
~20K and an enemy's think 5.9K.

So 5 fps holds only with few objects in view; the 5+ fps target with ~10
objects in view isn't met. What's left is mostly the picture's fill,
inside a8_3d.

**The rules**, by profiling (`projects/a8_3d/tools/profile.py`) and timing
each part of a tick. A round-8 tick went from 72–80K (with the minds
already every fourth tick) to 34–44K; a round-1 tick from 60–150K at
the start to 19–21K.

In asm:
- an obstacle query over an 8 × 8 grid of the arena (`obs.s`);
- atan2 and length (`atan.s`, `fast.s`);
- the shells' flight, which stops only at a step where something may
  happen (`shell.s`);
- the tanks' drive and the walls (`drive.s`);
- pushing a tank out of obstacles (`pushout.s`);
- pushing tanks apart, and the loop over the enemies (`apart.s`);
- an enemy's mind (`think.s`).

Each asm routine was checked call by call against the C's rules in the
emulator: a breakpoint on entry and on return, and the expected result
from a Python copy of the C. There were 0 mismatches over several hundred
calls each, in rounds 1, 3/4 and 8. The C stays for the host build
(`tools/run_port.c`).

Changes to the rules themselves:
- byte counters instead of modulos;
- the minds every other tick (every fourth with more than 3 enemies);
- after the tanks are pushed apart, only the tanks that moved are pushed
  out of the obstacles again (drive already pushed each one out).

**The picture** went from 450–750K to 175–370K, by:

- a8_3d's convex-object path (`a3d_prism`, `a3d_pyramid`,
  `a3d_wallquad`): one asm call an object; the C face path stays for an
  object with a corner nearer than the near plane;
- clipping the walls in 2D, with a 16-bit division;
- level of detail and the draw caps.

- a8_3d's fast fill for polygons wholly on the screen (`A3D_FFILL`, on in
  the Makefile): ~5% off a picture.

To make room for that, the sine table and its maths (`td_fix.c`) moved
under the ROM.

## Test

`./a8 test tank_duel`:

- the title shows the arena, and UP and DOWN choose the round;
- FIRE starts round 1;
- UP drives the tank ahead and LEFT turns it, read from the game state;
- the arena has content;
- SELECT hands over to the autopilot: it kills (100 a scout), takes hits
  and clears round 1, and round 2 begins;
- then the armour is set to 1, and the score to at least 1000 so it makes
  the table (its lowest entry is 800), so the next hit ends the game: game
  over, ACE goes in the high scores, and it's back to the title;
- the frame rate is logged;
- then the same XEX boots on a 64K 800XL and its demo fights.

## Not done

- 5+ fps with ~10 objects in view (see Speed).
- The high scores are not saved.
- PAL only measured. On NTSC the rules' 12.5 ticks a second come from 4
  frames of 60, so they run 20% fast.
