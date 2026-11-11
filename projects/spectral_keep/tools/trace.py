"""Play a fixed pseudo-random input sequence on the host sim and print each
step's probe (or a hash of them): to check that a change to the physics
keeps the game the same.

    python3 tools/trace.py [sim] [steps] [seed]
"""
import hashlib
import random
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sim = sys.argv[1] if len(sys.argv) > 1 else str(HERE / "build" / "host" / "sim")
steps = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
p = subprocess.Popen([sim], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
h = hashlib.sha1()
log = []
held = 0
for t in range(steps):
    while True:
        line = p.stdout.readline()
        if line.startswith("L "):
            log.append(f"{t} {line[2:].strip()}")
        elif line.startswith("P "):
            h.update(line.encode())
            break
    if t % 12 == 0:
        held = rng.choice([1, 2, 4, 8, 5, 6, 9, 10, 0])
    pressed = 16 if rng.random() < 0.05 else 0
    if t == 20 or (t % 400) == 399:
        pressed |= 16
    p.stdin.write(f"{held} {pressed}\n")
p.stdin.write("q\n")
p.wait()
print("\n".join(log[-40:]))
print("rooms", len([l for l in log if " room " in l]), "hash", h.hexdigest())
