#!/usr/bin/env python3
"""Spectral Keep's room pictures for the Atari, from the original's own
picture of each room (tools/export.c's ref_<l>_<r>.bin, 320 x 240, nothing
moving in it).

ANTIC mode E (160 pixels, 4 colours a line): a pixel is two of the
original's across and one line down (the same shape on screen). A DLI
every BAND lines sets the band's four colours (COLBK, PF0-PF2), chosen per
band from the Atari's palette (tools/pal_pal.bin, the emulator's).

    python3 tools/gen_rooms.py [--preview]     (after tools/export.c)
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
HOST = os.path.join(PROJ, "build", "host")
W, H = 160, 208          # the picture: mode E pixels, lines (y 0..207 of the original's)
BAND = 8
PAL = np.frombuffer(open(os.path.join(HERE, "pal_pal.bin"), "rb").read(), dtype=np.uint8).reshape(256, 3).astype(np.float32)
PAL_IDX = np.arange(0, 256, 2)     # GTIA: the low bit doesn't count
PALC = PAL[PAL_IDX]
CHROMA = 3.0             # chroma errors count this much more than luma's (keeps the rooms' hues)


def yuv(a, chroma=CHROMA):
    """RGB (... x 3) -> a luma / weighted chroma space for the colour choice"""
    y = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
    w = chroma ** 0.5
    return np.stack([y, (a[..., 2] - y) * 0.49 * w, (a[..., 0] - y) * 0.88 * w], -1)


PALY = yuv(PALC)
SEL_CHROMA = 0.5         # in a band's choice of colours: shading (luma) first
PALS = yuv(PALC, SEL_CHROMA)


def load(path):
    d = np.fromfile(path, dtype="<u2").reshape(240, 320)
    r, g, b = ((d >> 10) & 31) * 255 / 31, ((d >> 5) & 31) * 255 / 31, (d & 31) * 255 / 31
    return np.dstack([r, g, b]).astype(np.float32)


def half(im):
    """320 -> 160 across: the mean of each pair"""
    return (im[:, 0::2] + im[:, 1::2]) / 2


HUE_ANG = []
for h in range(16):
    c = yuv(PAL[h * 16 + 8][None, :])[0]
    HUE_ANG.append(np.arctan2(c[2], c[1]))
HUE_ANG = np.array(HUE_ANG)
CHROMA_MIN = 10.0        # below this (weighted) the pixel is a grey


CONTRAST = 1.7


def stretch(im, bg):
    """the room's luma spread out about its median (the background left as
    it is): the faces' light / mid / dark stay apart in 3 colours a band"""
    y = im[..., 0] * 0.299 + im[..., 1] * 0.587 + im[..., 2] * 0.114
    fg = np.abs(im - bg).sum(-1) > 6
    m = np.median(y[fg])
    k = np.clip(m + (y - m) * CONTRAST, 0, 255) / np.maximum(y, 1)
    out = np.clip(im * k[..., None], 0, 255)
    return np.where(fg[..., None], out, im)


def to_atari(im):
    """each pixel -> the Atari colour of its hue and brightness (the
    original's colours are muted; the Atari's are not: matching hue and
    luma, not saturation, keeps a room green or pink rather than grey)"""
    c = yuv(im)
    ch = np.hypot(c[..., 1], c[..., 2])
    ang = np.arctan2(c[..., 2], c[..., 1])
    dang = np.abs(((ang[..., None] - HUE_ANG[None, 1:]) + np.pi) % (2 * np.pi) - np.pi)
    hue = np.where(ch < CHROMA_MIN, 0, dang.argmin(-1) + 1)
    lumy = PALY[:, 0].reshape(16, 8)                       # [hue][lum/2] luma
    lum = np.abs(lumy[hue] - c[..., 0][..., None]).argmin(-1)
    return PALC[hue * 8 + lum]


def band_palette(px, bk):
    """three palette entries (PF0-PF2) for these pixels (N x 3), besides the
    background bk: k-means, snapped, then each slot improved against every
    Atari colour"""
    k = 3
    uniq = np.unique(px.reshape(-1, 3), axis=0)
    cent = uniq[np.linspace(0, len(uniq) - 1, min(k, len(uniq))).astype(int)].copy()
    for _ in range(8):
        d = ((px[:, None, :] - cent[None, :, :]) ** 2).sum(2)
        a = d.argmin(1)
        for j in range(len(cent)):
            if (a == j).any():
                cent[j] = px[a == j].mean(0)
    sel = [int(((PALS - yuv(c, SEL_CHROMA)) ** 2).sum(1).argmin()) for c in cent]
    while len(sel) < k:
        sel.append(sel[0])
    dall = ((yuv(px, SEL_CHROMA)[:, None, :] - PALS[None, :, :]) ** 2).sum(2)      # N x 128
    dbk = dall[:, bk]
    def err(s):
        return np.minimum(dall[:, s].min(1), dbk).sum()
    best = err(sel)
    for _ in range(2):
        for j in range(k):
            for c in range(len(PALC)):
                t = sel.copy()
                t[j] = c
                e = err(t)
                if e < best:
                    best, sel = e, t
    return sel


def convert(im, bk):
    """the picture (H x 160 x 3) -> pixel values (H x 160; 0 the background
    colour bk), each band's colours [bk, pf0, pf1, pf2]"""
    pix = np.zeros((H, W), np.uint8)
    cols = []
    for b0 in range(0, H, BAND):
        px = im[b0:b0 + BAND].reshape(-1, 3)
        sel = sorted(band_palette(px, bk), key=lambda c: PALS[c, 0])
        sel = [bk] + sel
        pc = PALS[sel]
        d = ((yuv(im[b0:b0 + BAND], SEL_CHROMA)[:, :, None, :] - pc[None, None, :, :]) ** 2).sum(3)
        pix[b0:b0 + BAND] = d.argmin(2)
        cols.append([int(PAL_IDX[c]) for c in sel])
    return pix, cols


def render(pix, cols):
    out = np.zeros((H, W * 2, 3), np.uint8)
    for y in range(H):
        c = cols[y // BAND]
        row = PAL[[c[v] for v in pix[y]]].astype(np.uint8)
        out[y] = np.repeat(row, 2, axis=0)
    return out


def main():
    rooms = sorted(glob.glob(os.path.join(HOST, "ref_*.bin")))
    for f in rooms:
        lr = os.path.basename(f)[4:-4]
        src = half(load(f))[:H]
        im = to_atari(stretch(src, src[0, 0]))
        bk = int(((PALS - yuv(src[0, 0], SEL_CHROMA)) ** 2).sum(1).argmin())
        pix, cols = convert(im, bk)
        if "--preview" in sys.argv:
            a = render(pix, cols)
            ref = np.repeat(half(load(f))[:H], 2, axis=1).astype(np.uint8)
            Image.fromarray(np.hstack([ref, a])).resize((W * 4 * 2 // 2 * 2, H * 2), Image.NEAREST).save(
                os.path.join(HOST, f"atari_{lr}.png"))
        print(lr, end=" ", flush=True)
    print()


if __name__ == "__main__":
    main()
