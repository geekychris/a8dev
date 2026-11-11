"""Lunar Rider's bot (test.py, make_gif.py): rides at full speed and jumps
when it must, looking ahead with the buggy's own rules (game.c)."""
import struct

CRATER_SM, CRATER_LG, ROCK, MINE = 1, 2, 3, 5
BUGGY_CX = 80 + 12          # BUGGY_X + BUGGY_W / 2


def probe(emu):
    st, lives, alive, ground, scroll = struct.unpack("<BBBBI", emu.read_mem("lunar_probe", 8))
    return st, lives, alive, ground, scroll


# the buggy's rules (game.c update_buggy), to look ahead: vy = -600 + 40k,
# y += vy / 100 (truncated), the ground test at the centre, rocks at the front
VDIV = [int((-600 + 40 * k) / 100) for k in range(128)]


def height(t):
    return 220 if t == CRATER_SM else 230 if t == CRATER_LG else 200 - (t >> 4)


def step(ter, scroll, y, k, ground, jump, speed=3):
    """One frame at full speed; returns (scroll, y, k, ground) or None (dead)."""
    scroll += speed
    if jump and ground:
        k, ground = 0, False
    elif not ground:
        k = min(k + 1, 127)
    y += VDIV[k]
    t = ter[(scroll + BUGGY_CX) & 1023]
    gh = height(t) - 12
    if y >= gh:
        if t in (CRATER_SM, CRATER_LG):
            if (ground or k > 15) and y >= 193:
                return None
        else:
            y, k, ground = gh, 15, True
    else:
        ground = False
    if ground and ter[(scroll + BUGGY_CX + 12) & 1023] == ROCK:
        return None
    if ground and t == MINE:
        return None
    return scroll, y, k, ground


def survives(ter, state, frames, jump):
    """Can the buggy live `frames` more frames if it jumps (or not) now?"""
    s = step(ter, *state, jump)
    if s is None:
        return False
    if frames <= 1:
        return True
    if not s[3]:                                     # in the air: no choice
        return survives(ter, s, frames - 1, False)
    return survives(ter, s, frames - 1, False) or survives(ter, s, frames - 1, True)


def want_jump(emu, scroll):
    """Jump now only when not jumping can't be survived and jumping can."""
    ter = emu.read_mem("terrain", 1024)
    gs = emu.read_mem("gs", 17)
    y = struct.unpack("<h", gs[9:11])[0]
    ground, k = gs[13], gs[16]
    state = (scroll, y, k, bool(ground))
    if survives(ter, state, 34, False):
        return False
    return survives(ter, state, 34, True)


def ride(emu, state):
    """One frame of the bot: jump (hold UP 3 frames) when it must; `state`
    is a dict it keeps between calls."""
    st, lives, alive, ground, scroll = probe(emu)
    if alive and ground and want_jump(emu, scroll):
        emu.hold("UP")
        state["jumping"] = 3
    elif state.get("jumping"):
        state["jumping"] -= 1
        if not state["jumping"]:
            emu.release("UP")
    emu.run(1)
