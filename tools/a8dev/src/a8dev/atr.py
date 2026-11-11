"""ATR disk images that boot into a menu of XEX programs, with no DOS.

    from a8dev.atr import make_disk
    make_disk("build/arcade/arcade.atr", boot_bin, [("MENU", menu_xex), ("GAME", game_xex)])

Layout (single density, 128-byte sectors; sdk/boot/boot.s is the loader):

    sectors 1..8     the boot loader, loaded at $0700
    sectors 9..16    directory: 32 entries of 32 bytes
                       +0 start sector (word)  +2 length (3 bytes)  +5 flags
                       +6 title (26 bytes, ATASCII, 0-padded)
                     entry 0 is the program the disk boots into (the menu)
    sectors 17..     the files, each in whole consecutive sectors

The ATR header's size field counts 16-byte paragraphs; sectors above 720
make an "enhanced" image every emulator reads (Atari800, Altirra).
"""
from __future__ import annotations

import struct
from pathlib import Path

SECTOR = 128
BOOT_SECTORS = 8
DIR_SECTOR = 9
DIR_SECTORS = 8
FIRST_FILE = DIR_SECTOR + DIR_SECTORS
MAX_ENTRIES = DIR_SECTORS * SECTOR // 32
TITLE_LEN = 26


class AtrError(ValueError):
    pass


def check_xex(data: bytes, name: str = "file") -> None:
    """Raise unless data is a well-formed Atari executable (segments to the end)."""
    i, n, segs = 0, len(data), 0
    while i < n:
        if data[i:i + 2] == b"\xff\xff":
            i += 2
        if i + 4 > n:
            raise AtrError(f"{name}: truncated segment header at {i}")
        start, end = struct.unpack_from("<HH", data, i)
        i += 4
        if end < start:
            raise AtrError(f"{name}: segment {start:#06x}-{end:#06x} ends before it starts")
        i += end - start + 1
        if i > n:
            raise AtrError(f"{name}: segment {start:#06x}-{end:#06x} runs past the end of the file")
        segs += 1
    if not segs:
        raise AtrError(f"{name}: no segments")


def atr_header(sectors: int) -> bytes:
    paras = sectors * SECTOR // 16
    return struct.pack("<HHHB9x", 0x0296, paras & 0xFFFF, SECTOR, paras >> 16)


def make_disk(path, boot: bytes, files: list[tuple[str, bytes]]) -> list[dict]:
    """Write an ATR: the loader, the directory and the files. Returns the
    directory entries (index, title, sector, length)."""
    if len(boot) > BOOT_SECTORS * SECTOR:
        raise AtrError(f"boot loader is {len(boot)} bytes, more than {BOOT_SECTORS} sectors")
    if len(files) > MAX_ENTRIES:
        raise AtrError(f"{len(files)} files, the directory holds {MAX_ENTRIES}")
    body = bytearray(boot.ljust(BOOT_SECTORS * SECTOR, b"\0"))
    directory = bytearray(DIR_SECTORS * SECTOR)
    data = bytearray()
    entries = []
    sector = FIRST_FILE
    for i, (title, xex) in enumerate(files):
        check_xex(xex, title)
        t = title.upper().encode("ascii", "replace")[:TITLE_LEN].ljust(TITLE_LEN, b"\0")
        directory[i * 32:(i + 1) * 32] = struct.pack("<HHBB", sector, len(xex) & 0xFFFF, len(xex) >> 16, 0) + t
        entries.append({"index": i, "title": title, "sector": sector, "length": len(xex)})
        padded = bytes(xex) + b"\0" * (-len(xex) % SECTOR)
        data += padded
        sector += len(padded) // SECTOR
    body += directory + data
    total = len(body) // SECTOR
    total = max(total, 720)                       # at least a standard 90K disk
    body = body.ljust(total * SECTOR, b"\0")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(atr_header(total) + bytes(body))
    return entries


def read_directory(path) -> list[dict]:
    """The directory of a disk made by make_disk."""
    raw = Path(path).read_bytes()
    if struct.unpack_from("<H", raw, 0)[0] != 0x0296:
        raise AtrError(f"{path}: not an ATR image")
    img = raw[16:]
    out = []
    for i in range(MAX_ENTRIES):
        e = img[(DIR_SECTOR - 1) * SECTOR + i * 32:][:32]
        sector, lo, hi, _flags = struct.unpack_from("<HHBB", e)
        length = lo | hi << 16
        if not length:
            continue
        out.append({"index": i, "title": e[6:].rstrip(b"\0").decode("ascii", "replace"),
                    "sector": sector, "length": length,
                    "data": img[(sector - 1) * SECTOR:][:length]})
    return out
