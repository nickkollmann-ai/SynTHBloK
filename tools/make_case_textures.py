"""Renders BACKDROP's case textures into res/cases/*.png: seamless (they tile), neutral overlays drawn over each
case's own colour, so one wood grain serves walnut, oak, cherry, ebony and ash alike.

Each texture is RGBA: white where the surface is lighter than its colour, black where darker, with the alpha saying
how much. Rack draws each as one textured rectangle, so they cost next to nothing to draw.
Run: python tools/make_case_textures.py  (needs numpy, Pillow)
"""
import os
import numpy as np
from PIL import Image

N = 512
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'res', 'cases')
os.makedirs(OUT, exist_ok=True)


def pnoise(seed, scale, aniso=(1.0, 1.0), n=N):
    """Periodic (seamless) noise: white noise filtered in the frequency domain. scale ~ feature size in pixels."""
    rng = np.random.default_rng(seed)
    f = np.fft.fft2(rng.normal(size=(n, n)))
    fy = np.fft.fftfreq(n)[:, None] * aniso[1]
    fx = np.fft.fftfreq(n)[None, :] * aniso[0]
    r = np.sqrt(fx ** 2 + fy ** 2)
    f *= np.exp(-(r * scale) ** 2)
    out = np.real(np.fft.ifft2(f))
    return (out - out.mean()) / (out.std() + 1e-9)


def overlay(name, v, strength):
    """v: detail around 0 (negative darker, positive lighter) -> RGBA overlay."""
    v = np.clip(v, -1, 1)
    rgb = np.where(v[..., None] > 0, 255, 0).astype(np.uint8).repeat(3, axis=2)
    a = (np.abs(v) * strength * 255).clip(0, 255).astype(np.uint8)
    Image.fromarray(np.dstack([rgb, a]), 'RGBA').save(os.path.join(OUT, name + '.png'), optimize=True)
    print('wrote', name)


def wood():
    # grain runs along x: rings from a stretched noise field, warped; plus fine pores and a few darker streaks
    y, x = np.mgrid[0:N, 0:N].astype(float)
    warp = pnoise(1, 30, (6.0, 1.0)) * 2.2 + pnoise(2, 8, (10.0, 1.0)) * 0.5
    rings = np.sin((y / N * 2 * np.pi * 11) + warp)
    rings = np.sign(rings) * np.abs(rings) ** 0.5
    pores = pnoise(3, 1.0, (30.0, 1.0))
    streak = pnoise(4, 3, (40.0, 1.0))
    v = rings * 0.35 + pores * 0.18 + streak * 0.22
    overlay('wood', v / 1.1, 0.55)


def ply_edge():
    # the cut edge of birch ply: thin layers across, light and dark, with a little grain
    y, x = np.mgrid[0:N, 0:N].astype(float)
    layers = np.sin(y / N * 2 * np.pi * 26 + pnoise(5, 30, (8.0, 1.0)) * 0.5)
    v = np.sign(layers) * 0.55 + pnoise(6, 1.5, (20.0, 1.0)) * 0.15
    overlay('ply_edge', v, 0.5)


def brushed():
    v = pnoise(7, 0.7, (120.0, 1.0)) * 0.6 + pnoise(8, 3, (60.0, 1.0)) * 0.3
    overlay('brushed', v / 1.2, 0.35)


def powder():
    # orange-peel: soft small bumps
    v = pnoise(9, 2.5) * 0.7 + pnoise(10, 0.8) * 0.3
    overlay('powder', v / 1.4, 0.22)


def tolex():
    # pebbled leather: cells with dark creases
    v = pnoise(11, 3.2)
    crease = -np.exp(-(v * 3) ** 2) * 0.9
    v = v * 0.25 + crease + pnoise(12, 0.8) * 0.15
    overlay('tolex', v, 0.45)


def carbon():
    # 2x2 twill: tows crossing over two, under two, stepping one each row (the diagonal you see in real carbon).
    # Each tow shows its fibres along its own direction and a rounded sheen across it, so the weave catches light.
    y, x = np.mgrid[0:N, 0:N].astype(float)
    cell = 32
    cx, cy = (x // cell).astype(int), (y // cell).astype(int)
    warp_on_top = ((cx + cy) % 4) < 2
    u = (x % cell) / cell
    v = (y % cell) / cell
    across = np.where(warp_on_top, u, v)                    # across the tow: the rounded sheen
    along = np.where(warp_on_top, y, x)                     # along the tow: the fibre lines
    sheen = np.sin(across * np.pi) ** 1.5
    fibres = np.sin(along * 2.2 + pnoise(14, 6) * 1.5) * 0.04 + pnoise(13, 0.8) * 0.03
    edge = -np.exp(-((across - 0.0) / 0.06) ** 2) * 0.5 - np.exp(-((across - 1.0) / 0.06) ** 2) * 0.5   # dark gaps between tows
    tone = np.where(warp_on_top, 0.15, -0.15)              # warp and weft lit differently
    v = sheen * 0.75 - 0.35 + tone + fibres + edge
    overlay('carbon', v, 0.6)


if __name__ == '__main__':
    for fn in (wood, ply_edge, brushed, powder, tolex, carbon):
        fn()
