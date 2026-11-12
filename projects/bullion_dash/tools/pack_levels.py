"""src/levels.txt -> src/levels_data.c / .h with tools/gridpack.py, the packed
bits in the XEX's LEVELS segment at $A000-$BFFF (bullion.cfg, xexsegs.s), the
grid lengths and the set itself in the main program (the repaired levels'
bits nearly fill the 8 KB there).

    python3 tools/pack_levels.py
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
SRC = HERE / "src"

subprocess.run([sys.executable, str(HERE / "tools" / "gridpack.py"), str(SRC / "levels.txt"),
                "--prefix", "lv", "--no-names", "-o", str(SRC / "levels_data.c"),
                "--header", str(SRC / "levels_data.h")], check=True)
c = (SRC / "levels_data.c").read_text()
glen = re.search(r"static const uint8_t lv_grid_len\[\] = \{.*?\};\n", c, re.S).group(0)
lvset = re.search(r"const gp_set lv_set = .*?;\n", c).group(0)
c = c.replace(glen, "").replace(lvset, "")
head, body = c.split("\n\n", 1)
c = (head + "\n\n" + "/* in the main program */\n" + glen +
     "\n/* bullion.cfg: loaded at $A000 (see xexsegs.s) */\n"
     "#pragma rodata-name (push, \"LEVELS\")\n\n" + body.rstrip("\n") + "\n"
     "#pragma rodata-name (pop)\n\n" + lvset)
(SRC / "levels_data.c").write_text(c)
