"""The arcade: every game in the repo on one bootable ATR disk with a menu.

    ./a8 arcade [--no-build] [--image-only] [--list]

A game joins with projects/<p>/arcade.json:

    {"title": "Planet Chomp", "ready": "chomp: ready",
     "description": "about 100 characters", "controls": "joystick, FIRE",
     "picture": "screenshots/play.png"}

"picture" is optional: without it the game is booted headless, run to its
ready line plus "picture_frames" (default 100) frames, and the screen is
captured (after "picture_press" keys, e.g. "START", if given). The menu
shows it at 80x48 in the game's four most common colours.

build() builds the games, writes projects/arcade/build/gen/games.h (titles,
descriptions, controls in menu order), builds the menu (projects/arcade),
assembles the boot loader (sdk/boot/boot.s) and writes
build/arcade/arcade.atr (directory entry 0 = the menu, i + 1 = game i) and
build/arcade/manifest.json. The disk needs no DOS.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path

import collections
import hashlib

from .atr import PIC_BYTES, make_disk, read_directory
from .emu import repo_root

ROOT = repo_root()
OUT = ROOT / "build" / "arcade"
MENU = ROOT / "projects" / "arcade"
MAX_GAMES = 31


class ArcadeError(RuntimeError):
    pass


def _c(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def games(root: Path | None = None) -> list[dict]:
    """Projects with an arcade.json, sorted by title."""
    root = root or ROOT
    out = []
    for f in sorted((root / "projects").glob("*/arcade.json")):
        try:
            g = json.loads(f.read_text())
        except json.JSONDecodeError as ex:
            raise ArcadeError(f"{f}: {ex}") from None
        p = f.parent.name
        for k in ("title", "ready"):
            if not g.get(k):
                raise ArcadeError(f"{f}: needs \"{k}\"")
        out.append({"project": p, "title": g["title"], "ready": g["ready"],
                    "description": g.get("description", ""), "controls": g.get("controls", ""),
                    "picture": str(f.parent / g["picture"]) if g.get("picture") else None,
                    "picture_frames": int(g.get("picture_frames", 100)),
                    "picture_press": g.get("picture_press"),
                    "short": p.replace("_", "")[:8]})
    out.sort(key=lambda g: g["title"].lower())
    for i, g in enumerate(out):
        g["index"] = i
    if len(out) > MAX_GAMES:
        raise ArcadeError(f"{len(out)} games; the disk directory holds {MAX_GAMES}")
    return out


def _readme_blurb(proj: Path) -> tuple[str | None, str]:
    """A project's README: its first heading and first paragraph (or None, "")."""
    f = proj / "README.md"
    if not f.exists():
        return None, ""
    title, para = None, []
    for line in f.read_text(errors="replace").splitlines():
        s = line.strip()
        if title is None and s.startswith("#"):
            title = s.lstrip("#").strip()
            continue
        if s.startswith(("!", "|", "```", "#", "<")) or not s:
            if para:
                break
            continue
        para.append(s)
    return title, " ".join(para)[:300]


def _other_kind(name: str) -> str:
    if name == "arcade":
        return "the arcade's own menu program"
    if "test" in name:
        return "a hardware / emulator test"
    return "a library demo or SDK example"


def _program(proj: Path) -> Path | None:
    x = proj / "build" / f"{proj.name}.xex"
    if x.exists():
        return x
    cands = sorted((proj / "build").glob("*.xex"))
    return cands[0] if cands else None


def catalog(root: Path | None = None) -> dict:
    """Every project, for the DevBench's arcade page: the games (projects with an
    arcade.json), each saying whether it is on the built ATR (playable from the
    menu) or only playable on its own, and the other projects (tests, demos, the
    menu) with why they aren't in the arcade. Never raises for one bad project:
    its "problem" says what is wrong."""
    root = root or ROOT
    out_dir = root / "build" / "arcade"
    image = out_dir / "arcade.atr"
    on_disk = {}
    if (out_dir / "manifest.json").exists():
        try:
            on_disk = {g["project"]: g for g in json.loads((out_dir / "manifest.json").read_text())["games"]}
        except (json.JSONDecodeError, KeyError, TypeError):
            on_disk = {}
    image_time = image.stat().st_mtime if image.exists() else None
    gs, others = [], []
    for proj in sorted(p for p in (root / "projects").iterdir() if (p / "Makefile").exists()):
        prog = _program(proj)
        shots = sorted((proj / "screenshots").glob("*.png")) if (proj / "screenshots").is_dir() else []
        meta = proj / "arcade.json"
        if not meta.exists():
            title, blurb = _readme_blurb(proj)
            others.append({"project": proj.name, "title": title or proj.name, "description": blurb,
                           "machine": "a8", "built": prog is not None, "kind": _other_kind(proj.name),
                           "reason": f"no arcade.json: {_other_kind(proj.name)}",
                           "picture": str(shots[0]) if shots else None})
            continue
        g = {"project": proj.name, "title": proj.name, "description": "", "controls": "", "machine": "a8",
             "built": prog is not None, "picture": None, "problem": None}
        try:
            j = json.loads(meta.read_text())
            missing = [k for k in ("title", "ready", "description", "controls") if not j.get(k)]
            g.update(title=j.get("title") or proj.name, description=j.get("description", ""),
                     controls=j.get("controls", ""), ready=j.get("ready"))
            if j.get("picture"):
                if (proj / j["picture"]).exists():
                    g["picture"] = str(proj / j["picture"])
                else:
                    missing.append(f"picture file {j['picture']}")
            elif shots:
                g["picture"] = str(shots[0])
            if missing:
                g["problem"] = "arcade.json is missing " + ", ".join(missing)
        except json.JSONDecodeError as ex:
            g["problem"] = f"arcade.json: {ex}"
        b = on_disk.get(proj.name)
        on_image = bool(b) and image.exists()
        g["on_image"] = on_image
        g["index"] = b.get("index") if on_image else None
        g["stale"] = bool(on_image and image_time and prog and prog.stat().st_mtime > image_time)
        if not on_image:
            g["note"] = ("not on the arcade disk yet: it plays on its own; Rebuild adds it to the menu"
                         if image.exists() else "the arcade disk isn't built: it plays on its own")
        elif g["stale"]:
            g["note"] = "built since the arcade disk: the menu has the older build until Rebuild"
        gs.append(g)
    gs.sort(key=lambda g: g["title"].lower())
    return {"image": str(image) if image.exists() else None, "built": image.exists(),
            "games": gs, "others": others,
            "missing_from_image": [g["project"] for g in gs if not g["on_image"]]}


def _make(project_dir: Path, log=print):
    r = subprocess.run([str(ROOT / "bin" / "a8-make"), "-C", str(project_dir)],
                       capture_output=True, text=True)
    if r.returncode:
        raise ArcadeError(f"build of {project_dir.name} failed:\n{(r.stdout + r.stderr)[-3000:]}")


def games_h(gs: list[dict]) -> str:
    def arr(name, key):
        vals = ", ".join(_c(g[key][:200]) for g in gs) or '""'
        return f"static const char *const {name}[{max(len(gs), 1)}] = {{ {vals} }};"
    return "\n".join([
        "/* generated by a8dev/arcade.py from the projects' arcade.json */",
        f"#define GAME_COUNT {len(gs)}",
        arr("game_title", "title"), arr("game_desc", "description"),
        arr("game_controls", "controls"), ""])


def boot_loader() -> bytes:
    """Assemble sdk/boot/boot.s (in the toolchain) -> build/arcade/boot.bin."""
    OUT.mkdir(parents=True, exist_ok=True)
    out = OUT / "boot.bin"
    r = subprocess.run([str(ROOT / "bin" / "a8-make"), "-C", str(ROOT / "sdk" / "boot")],
                       capture_output=True, text=True)
    if r.returncode:
        raise ArcadeError(f"boot loader: {(r.stdout + r.stderr)[-2000:]}")
    built = ROOT / "sdk" / "boot" / "build" / "boot.bin"
    out.write_bytes(built.read_bytes())
    return out.read_bytes()


def _palette_rgb(pal: bytes) -> list[tuple[int, int, int]]:
    return [tuple(pal[i * 3:i * 3 + 3]) for i in range(256)]


def picture_bytes(img, pal: bytes) -> bytes:
    """A playfield image (any size, RGB) -> the menu's 1024-byte picture:
    80x48 pixels, 4 Atari colours (the most common = background)."""
    from PIL import Image
    rgb = _palette_rgb(pal)
    lut: dict = {}

    def index(c):
        if c not in lut:
            lut[c] = min(range(0, 256, 2), key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb[i], c)))
        return lut[c]
    src = img.convert("RGB").resize((320, 192), Image.NEAREST)
    px = src.load()
    blocks = []                          # 80x48 cells of 4x4: their most common colour
    for y in range(48):
        row = []
        for x in range(80):
            cnt = collections.Counter(index(px[x * 4 + dx, y * 4 + dy]) for dy in range(4) for dx in range(4))
            row.append(cnt.most_common(1)[0][0])
        blocks.append(row)
    freq = collections.Counter(c for r in blocks for c in r)
    cols = [c for c, _ in freq.most_common(4)]
    while len(cols) < 4:
        cols.append(cols[0])

    def nearest(c):
        return min(range(4), key=lambda k: sum((a - b) ** 2 for a, b in zip(rgb[cols[k]], rgb[c])))
    out = bytearray(PIC_BYTES)
    out[0:4] = bytes(cols)
    for y, r in enumerate(blocks):
        for bx in range(20):
            v = 0
            for k in range(4):
                v = (v << 2) | nearest(r[bx * 4 + k])
            out[4 + y * 20 + bx] = v
    out[-1] = 0xA8
    return bytes(out)


def capture_picture(g: dict, xex: Path, log=print) -> bytes | None:
    """The game's picture: its arcade.json PNG, or a headless capture.
    Cached in build/arcade/pics by the XEX's (and picture's) hash."""
    from .emu import Emulator
    h = hashlib.md5(xex.read_bytes() + json.dumps([g.get("picture"), g["picture_frames"],
                                                     g.get("picture_press")]).encode())
    if g.get("picture") and Path(g["picture"]).exists():
        h.update(Path(g["picture"]).read_bytes())
    cache = OUT / "pics" / f"{g['project']}-{h.hexdigest()[:12]}.bin"
    if cache.exists():
        return cache.read_bytes()
    from PIL import Image
    try:
        with Emulator() as emu:
            pal = emu.palette()
            if g.get("picture") and Path(g["picture"]).exists():
                img = Image.open(g["picture"])
            else:
                emu.load(xex)
                if not emu.run_until(g["ready"], 3000):
                    log(f"  {g['project']}: ready line {g['ready']!r} not seen: no picture")
                    return None
                if g.get("picture_press"):
                    emu.press(g["picture_press"])
                emu.run(g["picture_frames"])
                img = emu.screenshot(None, border=False)
    except Exception as ex:                       # no emulator: the menu shows no picture
        log(f"  {g['project']}: no picture ({ex})")
        return None
    data = picture_bytes(img, pal)
    for old in (OUT / "pics").glob(f"{g['project']}-*.bin"):
        old.unlink()
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_bytes(data)
    return data


def build(do_build: bool = True, log=print, out_dir: Path = OUT, pictures: bool = True) -> dict:
    gs = games()
    if do_build:
        for g in gs:
            log(f"  build {g['project']}")
            _make(ROOT / "projects" / g["project"], log)
    files = [("MENU", None)]
    for g in gs:
        x = ROOT / "projects" / g["project"] / "build" / f"{g['project']}.xex"
        if not x.exists():
            cands = sorted((ROOT / "projects" / g["project"] / "build").glob("*.xex"))
            if not cands:
                raise ArcadeError(f"{g['project']} isn't built (./a8 build {g['project']})")
            x = cands[0]
        files.append((g["title"], x.read_bytes()))
    gen = MENU / "build" / "gen"
    gen.mkdir(parents=True, exist_ok=True)
    h = games_h(gs)
    if not (gen / "games.h").exists() or (gen / "games.h").read_text() != h:
        (gen / "games.h").write_text(h)
        (MENU / "build" / "obj" / "main.c.o").unlink(missing_ok=True)
    log("  build the menu")
    _make(MENU, log)
    files[0] = ("MENU", (MENU / "build" / "menu.xex").read_bytes())
    boot = boot_loader()
    out_dir.mkdir(parents=True, exist_ok=True)
    image = out_dir / "arcade.atr"
    pics = []
    for g, (_, x) in zip(gs, files[1:]):
        xex = ROOT / "projects" / g["project"] / "build" / f"{g['project']}.xex"
        pics.append(capture_picture(g, xex if xex.exists() else Path(g["project"]), log) if pictures else None)
    entries = make_disk(image, boot, files, pics)
    m = {"image": str(image), "games": [{**g, "sector": entries[g["index"] + 1]["sector"]} for g in gs]}
    (out_dir / "manifest.json").write_text(json.dumps(m, indent=1))
    log(f"  {image} ({len(gs)} games, {image.stat().st_size // 1024} KB)")
    return m


def manifest() -> dict:
    f = OUT / "manifest.json"
    if f.exists():
        return json.loads(f.read_text())
    return {"image": None, "games": games()}


def find_game(gs: list[dict], name: str) -> dict:
    n = (name or "").lower()
    for g in gs:
        if n in (g["project"].lower(), g["title"].lower(), g["short"]):
            return g
    raise ArcadeError(f"no game {name!r}; one of {', '.join(g['project'] for g in gs)}")


def picture(src):
    from PIL import Image
    img = Image.open(src).convert("RGB")
    return img.tobytes(), img


def menu_ready(emu, max_frames: int = 3000) -> int | None:
    """Wait for the menu; the selected index, or None."""
    if not emu.run_until("arcade: menu ready", max_frames):
        return None
    line = next((l for l in reversed(emu.debug_log) if "arcade: menu ready" in l), "")
    try:
        return int(line.split("sel=")[1].split()[0])
    except (IndexError, ValueError):
        return 0


def menu_choose(emu, game: dict, count: int, max_frames: int = 3000, sel: int | None = None) -> bool:
    """Pick `game` in the menu and play it: True once its ready line shows."""
    if sel is None:
        sel = menu_ready(emu, max_frames)
    if sel is None:
        return False
    while sel != game["index"]:
        key = "DOWN" if game["index"] > sel else "UP"
        emu.press(key, hold_frames=2, release_frames=2)
        sel += 1 if key == "DOWN" else -1
        if not emu.run_until(f"arcade: sel={sel}", 30):
            return False
    emu.press("FIRE")
    return emu.run_until(game["ready"], max_frames)


__all__ = ["ArcadeError", "OUT", "build", "catalog", "games", "manifest", "find_game", "menu_choose",
           "menu_ready", "picture", "read_directory"]
