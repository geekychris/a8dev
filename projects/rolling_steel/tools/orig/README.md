The original's files, unchanged, from amiga_mcp/examples/rolling_steel
(the 3DO version's C, MIT, see LICENSE.original): game.c, game.h, phys.c,
course.c, rs.h, render.c, and data/course1-6.bin (built by the 3DO
version's tools/courses.py from the Unity game's course files).

The Atari edition's src/main.c and src/phys.c follow game.c and phys.c;
tools/gen_course.py draws the courses and samples their physics grid
from data/ (rscourse.py is course.c's loader in Python).
