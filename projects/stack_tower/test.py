"""stack_tower: the rules against the browser, then a game played on the joystick.

1. The game runs the browser's four scripted games (tests/vectors.h) on
   the Atari first: pieces, score, board, queue and hold must match.
2. The title: mode and level chosen with the joystick, FIRE starts.
3. A bot plays through the joystick only (UP rotates, LEFT/RIGHT turn the
   tower, FIRE hard-drops): it reads the board and the piece from memory,
   picks a placement (holes, height, bumpiness, rings, the classic weights)
   and steers the piece there. It checks OPTION (rotate back), SELECT
   (hold), START (pause), then plays until rings clear and the level goes
   up, then drops pieces where they are until the tower tops out: game
   over, back to the title.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "a8dev" / "src"))
from a8dev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = HERE / "build" / "stack_tower.xex"
COLS, TOTAL, ROWS = 16, 24, 20
CELLS = [
    [(-1, 0), (0, 0), (1, 0)], [(0, 0), (1, 0), (0, 1)],
    [(-1, 1), (-1, 0), (0, 0), (1, 0), (1, 1)], [(0, -1), (0, 0), (1, 0), (0, 1), (1, 1)],
    [(0, -1), (-1, 0), (0, 0), (1, 0), (0, 1)], [(-1, 1), (0, 1), (1, 1), (0, 0), (0, -1)],
    [(-1, 1), (-1, 0), (0, 0), (0, -1), (1, -1)], [(-1, 1), (0, 1), (0, 0), (0, -1), (1, -1)],
]
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def cells(t, rot, col, row):
    out = []
    for x, y in CELLS[t]:
        for _ in range(rot & 3):
            x, y = y, -x
        out.append(((col + x) % COLS, row + y))
    return out


def evaluate(board, t, rot, col):
    """Dellacherie's features for a well that wraps round (no walls)"""
    h = [max([r + 1 for r in range(TOTAL) if board[r * COLS + c]] or [0]) for c in range(COLS)]
    rel = cells(t, rot, col, 0)
    land = max(h[c] - y for c, y in rel)
    placed = [(c, land + y) for c, y in rel]
    if max(y for _, y in placed) >= ROWS - 2:
        return None
    b = list(board)
    for c, y in placed:
        b[y * COLS + c] = 1
    full = [r for r in range(TOTAL) if all(b[r * COLS + c] for c in range(COLS))]
    eroded = len(full) * sum(1 for _, y in placed if y in full)
    rows = [b[r * COLS:(r + 1) * COLS] for r in range(TOTAL) if r not in full]
    rows += [[0] * COLS] * (TOTAL - len(rows))
    top = max([r + 1 for r in range(TOTAL) if any(rows[r])] or [0])
    row_tr = sum(1 for r in range(top) for c in range(COLS) if bool(rows[r][c]) != bool(rows[r][(c + 1) % COLS]))
    col_tr = sum(1 for c in range(COLS) for r in range(TOTAL - 1) if bool(rows[r][c]) != bool(rows[r + 1][c]))
    col_tr += sum(1 for c in range(COLS) if not rows[0][c])
    hh = [max([r + 1 for r in range(TOTAL) if rows[r][c]] or [0]) for c in range(COLS)]
    holes = sum(1 for c in range(COLS) for r in range(hh[c]) if not rows[r][c])
    wells = 0
    for c in range(COLS):
        d = min(hh[(c - 1) % COLS], hh[(c + 1) % COLS]) - hh[c]
        if d > 0:
            wells += d * (d + 1) // 2
    landing = land + sum(y for _, y in rel) / len(rel)
    return -landing + eroded - row_tr - col_tr - 4 * holes - wells


class Game:
    def __init__(self, emu):
        self.emu = emu
        self.game = emu.addr("_game")
        self.probe = emu.addr("_st_probe")

    def state(self):
        """the piece from the game itself (always current); the shell's state,
        pause and hold from the probe (written at the end of each frame)"""
        p = self.emu.read_mem(self.probe, 8)
        g = self.emu.read_mem(self.game + 418, 6)          # hold, can_hold, piece type/rot/col/row
        ov = self.emu.read_mem(self.game + 449, 2)         # over, won
        row = g[5] - 256 if g[5] > 127 else g[5]
        return {"state": p[0], "type": g[2], "rot": g[3], "col": g[4], "row": row,
                "over": ov[0] | ov[1], "paused": p[6], "hold": g[0]}

    def board(self):
        return list(self.emu.read_mem(self.game + 9, COLS * TOTAL))

    def act(self, key, frames=3):
        self.emu.press(key, hold_frames=frames, release_frames=3)

    def wait(self, cond, frames=30):
        """run until cond(state) (the main loop may take a few frames)"""
        for _ in range(frames // 2):
            s = self.state()
            if cond(s):
                return s
            self.emu.run(2)
        return self.state()

    def place(self):
        """steer the piece to the best placement and drop it; False if over"""
        s = self.state()
        if s["over"] or s["state"] != 1:
            return False
        board = self.board()
        best = None
        for rot in range(4):
            for col in range(COLS):
                v = evaluate(board, s["type"], rot, col)
                if v is not None and (best is None or v > best[0]):
                    best = (v, rot, col)
        if best is None:
            best = (0, s["rot"], s["col"])
        _, rot, col = best
        for _ in range(6):                       # rotate (kicks may move it)
            s = self.state()
            if (s["rot"] - rot) % 4 == 0 or s["over"]:
                break
            r0 = s["rot"]
            self.act("UP")
            self.wait(lambda t: t["rot"] != r0 or t["over"])
        for _ in range(20):                      # turn the tower the short way
            s = self.state()
            d = (col - s["col"]) % COLS
            if d == 0 or s["over"]:
                break
            c0 = s["col"]
            self.act("RIGHT" if d <= 8 else "LEFT")
            self.wait(lambda t: t["col"] != c0 or t["over"])
        pieces = self.emu.read_mem(self.game + 432, 2)
        self.act("FIRE")
        for _ in range(30):                      # until the next piece
            if self.emu.read_mem(self.game + 432, 2) != pieces or self.state()["over"]:
                break
            self.emu.run(2)
        return True


with Emulator() as emu:
    emu.load(PROG)
    check("title", emu.run_until("st: ready", 4000))
    log = emu.log(0)
    for v in range(4):
        check(f"browser vector {v}", f"st: vector {v} ok=1" in log,
              re.search(rf"st: vector {v}.*", log).group(0) if f"vector {v}" in log else "")
    emu.run(200)                                  # the demo plays behind the title
    emu.screenshot(HERE / "build" / "t_title.png")
    g = Game(emu)
    check("the demo plays on the title", sum(1 for x in g.board() if x) > 0)

    # title: right = rising, left back = marathon, up/down level
    emu.log()
    for k in ("RIGHT", "LEFT", "UP", "DOWN"):
        g.act(k, 4)
        emu.run(4)
    g.act("FIRE", 4)
    check("FIRE starts marathon level 1, seed 1",
          emu.run_until("st: start mode=marathon level=1 seed=1", 100))
    g.wait(lambda s: s["state"] == 1)
    emu.run(10)

    # the console keys
    s0 = g.state()
    g.act("UP", 4)
    s1 = g.wait(lambda s: s["rot"] != s0["rot"])
    check("UP rotates clockwise", (s1["rot"] - s0["rot"]) % 4 == 1, f"{s0} {s1}")
    g.act("OPTION", 4)
    check("OPTION rotates back", g.wait(lambda s: s["rot"] == s0["rot"])["rot"] == s0["rot"])
    g.act("START", 4)
    check("START pauses", g.wait(lambda s: s["paused"] == 1)["paused"] == 1)
    row = g.state()["row"]
    emu.run(100)
    check("paused: the piece stays", g.state()["row"] == row)
    g.act("START", 4)
    check("START resumes", g.wait(lambda s: s["paused"] == 0)["paused"] == 0)
    held = g.state()["type"]
    g.act("SELECT", 4)
    check("SELECT holds the piece", g.wait(lambda s: s["hold"] == held)["hold"] == held)

    # the frame rate while the tower turns all the time (held RIGHT repeats):
    # count the display-list switches (a3d_show) frame by frame
    emu.hold("RIGHT")
    emu.run(30)
    flips, last = 0, emu.read_mem(0x231, 1)[0]
    for _ in range(150):
        emu.run(1)
        v = emu.read_mem(0x231, 1)[0]
        flips += v != last
        last = v
    emu.release()
    turn_fps = flips / 3.0
    print(f"       turning: {turn_fps:.1f} frames a second")
    check("turning the tower: >= 10 fps", turn_fps >= 10, f"{turn_fps:.1f}")

    # play
    clears = 0
    for n in range(160):
        if not g.place():
            break
        if n == 20:
            emu.screenshot(HERE / "build" / "t_play.png")
        if "st: level=2" in emu.log_text(0):
            break
    log = emu.log(0)
    clears = len(re.findall(r"st: clear rings=", log))
    print(f"       {n + 1} pieces, {clears} clears")
    check("pieces lock and score", "st: lock" in log and re.search(r"score=[1-9]", log) is not None)
    check("rings clear", clears >= 1, f"{clears}")
    check("level up after 10 rings", "st: level=2" in log)
    emu.screenshot(HERE / "build" / "t_level.png")
    fps = [int(x) for x in re.findall(r"fps10=(\d+)", log)]
    print(f"       redrawn frames a second x10: {fps}")

    # top out: drop pieces where they are
    for _ in range(80):
        if g.state()["over"]:
            break
        g.act("FIRE")
        emu.run(4)
    check("game over", "st: gameover" in emu.log_text(0) or emu.run_until("st: gameover", 200))
    emu.run(100)
    emu.screenshot(HERE / "build" / "t_over.png")
    g.act("FIRE", 4)
    check("FIRE: back to the title", emu.run_until("st: title", 100))
    if failures:
        print("\n".join(emu.debug_log[-20:]))

print(f"{'FAIL' if failures else 'PASS'}: stack_tower")
sys.exit(1 if failures else 0)
