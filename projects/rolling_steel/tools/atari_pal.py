"""The Atari's colours for pictures made on the host (from Spectral Keep's
tools): the emulator's PAL palette (pal_pal.bin), a luma / weighted chroma
space to choose colours in, each pixel mapped to the Atari colour of its hue
and brightness, and a band's best few colours."""
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PAL = np.frombuffer(open(os.path.join(HERE, "pal_pal.bin"), "rb").read(), dtype=np.uint8).reshape(256, 3).astype(np.float32)
PAL_IDX = np.arange(0, 256, 2)     # GTIA: the low bit doesn't count
PALC = PAL[PAL_IDX]
CHROMA = 3.0


def yuv(a, chroma=CHROMA):
    y = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
    w = chroma ** 0.5
    return np.stack([y, (a[..., 2] - y) * 0.49 * w, (a[..., 0] - y) * 0.88 * w], -1)


PALY = yuv(PALC)
SEL_CHROMA = 1.0
PALS = yuv(PALC, SEL_CHROMA)
HUE_ANG = np.array([np.arctan2(*yuv(PAL[h * 16 + 8][None, :])[0][[2, 1]]) for h in range(16)])
CHROMA_MIN = 10.0


def to_atari(im):
    """each pixel -> the Atari colour of its hue and brightness"""
    c = yuv(im)
    ch = np.hypot(c[..., 1], c[..., 2])
    ang = np.arctan2(c[..., 2], c[..., 1])
    dang = np.abs(((ang[..., None] - HUE_ANG[None, 1:]) + np.pi) % (2 * np.pi) - np.pi)
    hue = np.where(ch < CHROMA_MIN, 0, dang.argmin(-1) + 1)
    lumy = PALY[:, 0].reshape(16, 8)
    lum = np.abs(lumy[hue] - c[..., 0][..., None]).argmin(-1)
    return PALC[hue * 8 + lum]


def nearest(rgb):
    """the palette index (even) nearest an RGB colour"""
    return int(PAL_IDX[((PALS - yuv(np.asarray(rgb, np.float32), SEL_CHROMA)) ** 2).sum(-1).argmin()])


def band_palette(px, bk, k=3):
    """k palette entries (PALC indices) for these pixels (N x 3) besides the
    background bk (a PALC index): k-means, snapped, each slot then improved
    against every Atari colour"""
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
    dall = ((yuv(px, SEL_CHROMA)[:, None, :] - PALS[None, :, :]) ** 2).sum(2)
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
