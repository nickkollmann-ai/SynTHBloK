"""Renders BACKDROP's species woods into res/cases/wood_<slug>.png: full-colour, seamless (they tile), grain along x
(BACKDROP turns them upright on the side panels). Unlike the neutral overlays in make_case_textures.py these carry
their own colour, so each species has its own grain AND figure: curly, quilted, birdseye, spalted, ribbon, burl...

Run: python tools/make_wood_textures.py [slug ... | mats]   (also the man-made panel materials, mat_*.png)   (needs numpy, Pillow)
The order of WOODS matches the new SIDE MATERIAL entries in src/SynTHBloKBackdrop.cpp; add new woods at the END.
"""
import os
import sys
import numpy as np
from PIL import Image

N = 512
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'res', 'cases')
os.makedirs(OUT, exist_ok=True)
Y, X = np.mgrid[0:N, 0:N].astype(float)
TAU = 2 * np.pi


def pnoise(seed, scale, aniso=(1.0, 1.0)):
    """Periodic noise: white noise low-passed in the frequency domain. scale ~ feature size; aniso stretches."""
    rng = np.random.default_rng(seed)
    f = np.fft.fft2(rng.normal(size=(N, N)))
    fy = np.fft.fftfreq(N)[:, None] * aniso[1]
    fx = np.fft.fftfreq(N)[None, :] * aniso[0]
    f *= np.exp(-((np.sqrt(fx ** 2 + fy ** 2)) * scale) ** 2)
    o = np.real(np.fft.ifft2(f))
    return (o - o.mean()) / (o.std() + 1e-9)


def hexc(h):
    h = h.lstrip('#')
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], float)


def ramp(t, cols):
    """t in 0..1 -> colour along a list of hex stops."""
    t = np.clip(t, 0, 1)[..., None]
    c = [hexc(x) for x in cols]
    k = len(c) - 1
    i = np.minimum((t[..., 0] * k).astype(int), k - 1)
    f = t * k - i[..., None]
    a = np.stack(c)[i]
    b = np.stack(c)[i + 1]
    return a * (1 - f) + b * f


def rings(seed, count, warp=2.2, sharp=0.5):
    """Growth rings along x: 0..1, with the dark latewood thin."""
    w = pnoise(seed, 30, (6.0, 1.0)) * warp + pnoise(seed + 1, 8, (10.0, 1.0)) * 0.5
    r = (np.sin(Y / N * TAU * count + w) + 1) / 2
    return r ** (1 / sharp) if sharp < 1 else r


def pores(seed, amt=0.12):
    return pnoise(seed, 1.0, (30.0, 1.0)) * amt


def save(slug, rgb):
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB').save(os.path.join(OUT, 'wood_' + slug + '.png'), optimize=True)
    print('wrote wood_' + slug)


def shade(rgb, v):
    """v around 0: lighten (+) / darken (-) multiplicatively."""
    return rgb * (1 + v)[..., None]


# ---- species ----
def black_walnut(s=101):
    t = rings(s, 9) * 0.7 + pnoise(s + 2, 40, (8, 1)) * 0.12 + 0.15
    return shade(ramp(t, ['#2a1a12', '#4a3020', '#6b4a32', '#8a6446']), pores(s + 3, 0.1))


def claro_walnut(s=111):
    t = rings(s, 7, 3.2) * 0.6 + pnoise(s + 2, 25, (5, 1)) * 0.25 + 0.2
    rgb = ramp(t, ['#24160e', '#5a3a22', '#8a5e34', '#b08850'])
    streak = np.clip(pnoise(s + 4, 6, (30, 1)), 0, None) * 0.25
    return shade(rgb, pores(s + 3, 0.1) - streak)


def white_oak_qs(s=121):
    t = rings(s, 22, 0.6, 0.9) * 0.5 + 0.35
    rgb = ramp(t, ['#8c6a42', '#b8925e', '#d2b07a'])
    # ray fleck: short, lighter, shiny flakes across the grain
    fl = pnoise(s + 5, 1.6, (3.0, 1.0))
    fleck = np.clip(fl - 1.3, 0, None) * 0.9
    return shade(rgb, pores(s + 3, 0.16) + fleck * 0.5)


def cherry(s=131):
    t = rings(s, 10, 2.0, 0.8) * 0.45 + pnoise(s + 2, 50, (4, 1)) * 0.1 + 0.4
    rgb = ramp(t, ['#5e2410', '#8c3c1e', '#a8542c', '#c06c3c'])
    gum = np.clip(-pnoise(s + 6, 2, (25, 1)) - 1.6, 0, None) * 0.5
    return shade(rgb, pores(s + 3, 0.06) - gum)


def hard_maple(s=141):
    t = rings(s, 8, 1.6, 0.9) * 0.35 + 0.55
    return shade(ramp(t, ['#c8a678', '#e2c89c', '#f0dcb4']), pores(s + 3, 0.05))


def birch_ply(s=151):
    t = rings(s, 4, 4.0, 1.0) * 0.4 + pnoise(s + 2, 60, (3, 1)) * 0.15 + 0.5
    return shade(ramp(t, ['#c8a674', '#dcc092', '#ecd6ae']), pores(s + 3, 0.05))


def curly(base, s, amp=0.32, n=34):
    """Flame / tiger stripes ACROSS the grain (they shimmer as the light moves)."""
    w = pnoise(s + 7, 20, (1, 8)) * 0.8
    band = np.sin(X / N * TAU * n + w + pnoise(s + 8, 12, (1, 3)) * 0.6)
    return shade(base, band * amp)


def curly_maple(s=161):
    return curly(hard_maple(s), s, 0.28)


def quilted_maple(s=171):
    base = hard_maple(s)
    q = pnoise(s + 9, 22, (1.8, 1.0)) + pnoise(s + 22, 9, (1.4, 1)) * 0.35
    q = np.sin(q * 3.0)   # pillowy 3D blobs
    return shade(base, q * 0.3)


def birdseye_maple(s=181):
    rgb = hard_maple(s)
    rng = np.random.default_rng(s)
    v = np.zeros((N, N))
    for _ in range(140):
        cx, cy, r = rng.uniform(0, N), rng.uniform(0, N), rng.uniform(2.5, 5)
        dx = (X - cx + N / 2) % N - N / 2
        dy = (Y - cy + N / 2) % N - N / 2
        d = np.sqrt(dx ** 2 * 0.6 + dy ** 2)
        v += -np.exp(-(d / r) ** 2) * 0.35 + np.exp(-((d - r * 1.6) / (r * 0.6)) ** 2) * 0.12
    return shade(rgb, v)


def spalted_maple(s=191):
    rgb = hard_maple(s) * 0.97
    # zone lines: thin black wandering contours, plus grey-brown blotches
    f = pnoise(s + 10, 26) * 1.0 + pnoise(s + 11, 12) * 0.12
    lines = np.zeros((N, N))
    for lv in (-1.1, -0.4, 0.3, 1.0):
        lines += np.exp(-((f - lv) / 0.07) ** 2)
    blot = np.clip(pnoise(s + 12, 30) - 0.4, 0, None) * 0.35
    return shade(rgb, -np.clip(lines, 0, 1) * 0.82 - blot)


def waterfall_bubinga(s=201):
    t = rings(s, 12, 1.4, 0.8) * 0.4 + 0.35
    rgb = ramp(t, ['#5a2418', '#8a3a26', '#a85238', '#c06a48'])
    stripe = np.clip(pnoise(s + 4, 4, (40, 1)), 0, None) * 0.25
    ripple = np.sin(X / N * TAU * 26 + np.sin(Y / N * TAU * 3 + pnoise(s + 5, 30) * 1.2) * 4)
    return shade(rgb, ripple * 0.22 - stripe)


def ribbon_sapele(s=211):
    t = rings(s, 14, 0.5, 1.0) * 0.3 + 0.4
    rgb = ramp(t, ['#5a2a16', '#7a3c20', '#96502c'])
    ribbon = np.sin(Y / N * TAU * 9 + pnoise(s + 6, 50, (6, 1)) * 0.8)
    return shade(rgb, np.sign(ribbon) * np.abs(ribbon) ** 0.4 * 0.2 + pores(s + 3, 0.08))


def zebrawood(s=221):
    w = pnoise(s, 30, (6, 1)) * 1.4 + pnoise(s + 1, 6, (14, 1)) * 0.35
    st = np.sin(Y / N * TAU * 24 + w)
    dark = np.clip(st * 1.6 - 0.2, 0, 1)
    rgb = ramp(1 - dark * 0.9, ['#3a2614', '#7a5c34', '#d8bc88', '#e6d0a0'])
    return shade(rgb, pores(s + 3, 0.08))


def padauk(s=231):
    t = rings(s, 11, 1.8, 0.8) * 0.45 + pnoise(s + 2, 40, (5, 1)) * 0.1 + 0.4
    return shade(ramp(t, ['#8a1c0c', '#b42a12', '#d03c18', '#e05424']), pores(s + 3, 0.14))


def purpleheart(s=241):
    t = rings(s, 12, 1.4, 0.9) * 0.4 + 0.4
    return shade(ramp(t, ['#3c1438', '#5a1e54', '#782a6c', '#8e3a80']), pores(s + 3, 0.12))


def cocobolo(s=251):
    t = rings(s, 10, 2.6, 0.7) * 0.5 + pnoise(s + 2, 25, (5, 1)) * 0.2 + 0.3
    rgb = ramp(t, ['#4a1c0c', '#8a3412', '#c25a1c', '#e08a3c'])
    lines = np.exp(-((np.sin(Y / N * TAU * 17 + pnoise(s + 5, 20, (6, 1)) * 2.4)) / 0.12) ** 2)
    return shade(rgb, -lines * 0.75 + pores(s + 3, 0.06))


def wenge(s=261):
    w = pnoise(s, 30, (8, 1)) * 0.9
    st = (np.sin(Y / N * TAU * 40 + w) + 1) / 2
    rgb = ramp(st ** 2, ['#140c08', '#2a1c14', '#5a4430', '#7a6048'])
    return shade(rgb, pores(s + 3, 0.2))


def macassar_ebony(s=271):
    w = pnoise(s, 30, (6, 1)) * 1.8
    st = (np.sin(Y / N * TAU * 16 + w) + 1) / 2
    rgb = ramp(st ** 3 * 0.9 + pnoise(s + 2, 40, (6, 1)) * 0.05, ['#0c0806', '#1e140c', '#5a3c22', '#9a7040'])
    return shade(rgb, pores(s + 3, 0.06))


def ziricote(s=281):
    rgb = ramp(rings(s, 8, 2.2) * 0.5 + 0.35, ['#1a120c', '#3e2e20', '#7a6248'])
    # spider-web: a fine network of dark lines across everything, plus light sapwood-ish streaks
    f1 = pnoise(s + 13, 9); f2 = pnoise(s + 14, 16, (2, 1)); f3 = pnoise(s + 23, 6)
    web = np.exp(-(f1 / 0.16) ** 2) + np.exp(-(f2 / 0.14) ** 2) * 0.9 + np.exp(-(f3 / 0.12) ** 2) * 0.7
    light = np.clip(pnoise(s + 15, 8, (20, 1)) - 1.2, 0, None) * 0.6
    return shade(rgb, -np.clip(web, 0, 1) * 0.85 + light)


def bocote(s=291):
    w = pnoise(s, 20, (5, 1)) * 2.8 + pnoise(s + 1, 6, (8, 1)) * 0.6
    st = (np.sin(Y / N * TAU * 13 + w) + 1) / 2
    eyes = np.clip(np.sin(X / N * TAU * 5 + pnoise(s + 2, 30) * 3) * np.sin(Y / N * TAU * 13 + w), 0, None)
    rgb = ramp(np.clip(st ** 2.2 + eyes * 0.3, 0, 1), ['#d4a85a', '#a87a3a', '#4a3018', '#1a120a'])
    return shade(rgb, pores(s + 3, 0.06))


def koa(s=301):
    t = rings(s, 9, 1.8, 0.8) * 0.5 + 0.3
    base = shade(ramp(t, ['#5a3214', '#8a5424', '#b07a38', '#c8964c']), pores(s + 3, 0.06))
    return curly(base, s, 0.25, 28)


def leopardwood(s=311):
    rgb = ramp(np.full((N, N), 0.5) + pnoise(s + 2, 40, (4, 1)) * 0.08, ['#8a4a2a', '#b0683c', '#c8804c'])
    # leopard spots: short dark flecks with light rims (the big medullary rays)
    f = pnoise(s + 16, 5.5, (3.0, 1))
    spot = np.clip(f - 1.0, 0, None) * 1.4
    rim = np.exp(-((f - 0.8) / 0.15) ** 2)
    return shade(rgb, -np.clip(spot * 1.2, 0, 0.6) + rim * 0.15 + pores(s + 3, 0.06))


def amboyna_burl(s=321):
    f = pnoise(s + 17, 45) * 1.2 + pnoise(s + 18, 12) * 0.45
    swirl = (np.sin(f * 5) + 1) / 2
    rgb = ramp(swirl * 0.8 + 0.15, ['#5a1e0c', '#9a4418', '#c8702c', '#e0a050'])
    rng = np.random.default_rng(s)
    v = np.zeros((N, N))
    for _ in range(90):   # the burl's little dark eyes
        cx, cy, r = rng.uniform(0, N), rng.uniform(0, N), rng.uniform(1.5, 3.5)
        dx = (X - cx + N / 2) % N - N / 2; dy = (Y - cy + N / 2) % N - N / 2
        v -= np.exp(-(np.sqrt(dx ** 2 + dy ** 2) / r) ** 2) * 0.6
    return shade(rgb, v)


def shou_sugi_ban(s=331):
    # charred: black with a cracked "alligator" scale and soft silver-brown grain catching light
    g = rings(s, 7, 2.4, 0.7)
    c1 = pnoise(s + 19, 12, (1.8, 1)); c2 = pnoise(s + 20, 12, (1.8, 1))
    crack = np.exp(-(c1 / 0.12) ** 2) + np.exp(-(c2 / 0.12) ** 2)
    scale_hi = np.clip(np.minimum(np.abs(c1), np.abs(c2)) - 0.3, 0, None) * 0.5
    rgb = ramp(g * 0.55 + 0.15, ['#080808', '#1c1814', '#3a322a', '#6a5e50'])
    return shade(rgb, -np.clip(crack, 0, 1) * 0.8 + scale_hi * 1.6 + pnoise(s + 21, 1.2) * 0.12)


WOODS = [
    ('black_walnut', black_walnut), ('claro_walnut', claro_walnut), ('white_oak_qs', white_oak_qs), ('cherry', cherry),
    ('hard_maple', hard_maple), ('birch_ply', birch_ply),
    ('curly_maple', curly_maple), ('quilted_maple', quilted_maple), ('birdseye_maple', birdseye_maple),
    ('spalted_maple', spalted_maple), ('waterfall_bubinga', waterfall_bubinga), ('ribbon_sapele', ribbon_sapele),
    ('zebrawood', zebrawood), ('padauk', padauk), ('purpleheart', purpleheart), ('cocobolo', cocobolo), ('wenge', wenge),
    ('macassar_ebony', macassar_ebony), ('ziricote', ziricote), ('bocote', bocote), ('koa', koa), ('leopardwood', leopardwood),
    ('amboyna_burl', amboyna_burl), ('shou_sugi_ban', shou_sugi_ban),
]


# ---- man-made panel / case materials (res/cases/mat_*.png), full colour, seamless; x runs UP the panel ----
def mat_diamond(s=401):
    # tread plate: raised lozenges alternating direction on a 32 px grid (tiles), lit from the top left
    cx = np.mod(X, 32) - 16; cy = np.mod(Y, 32) - 16
    flip = ((np.floor(X / 32) + np.floor(Y / 32)) % 2) * 2 - 1
    u = (cx + flip * cy) / np.sqrt(2); v = (cx - flip * cy) / np.sqrt(2)
    d = np.abs(u) / 13 + np.abs(v) / 3.2                     # a long thin diamond
    loz = np.clip((1 - d) * 4, 0, 1)
    gy, gx = np.gradient(ndimage_blur(loz, 0.8))
    lit = np.clip(-(gx + gy) * 3, -1, 1)
    base = 150 + pnoise(s, 2) * 4 + pnoise(s + 1, 0.6, (1, 30)) * 3
    v_ = base + 30 * loz + 60 * lit
    return v_[..., None] * np.array([1, 1.01, 1.04])


def ndimage_blur(a, sg):
    from scipy import ndimage
    return ndimage.gaussian_filter(a, sg, mode='wrap')


def mat_carbon(s=411):
    # tight 2x2 twill: 4 px tows, over two / under two, stepping one per row; a soft sheen across each tow
    t = 4
    cx = np.floor(X / t); cy = np.floor(Y / t)
    over = np.mod(cx + cy, 4) < 2
    fx = np.mod(X, t) / t; fy = np.mod(Y, t) / t
    sheen = np.where(over, np.sin(fy * np.pi), np.sin(fx * np.pi)) ** 1.2
    v_ = 16 + 30 * sheen + pnoise(s, 0.5) * 1.5
    return v_[..., None] * np.array([1, 1.02, 1.07])


def mat_pla(s=421):
    # printed layers across the panel (x runs up the panel, so the lines vary with x), 4 px per layer
    lines = 0.82 + 0.18 * np.sin(X / 4 * TAU) ** 2
    return np.array([236, 104, 26])[None, None, :] * (lines * (1 + pnoise(s, 6) * 0.02))[..., None]


def mat_alu(s=431):
    b = 172 + pnoise(s, 0.6, (1, 120)) * 10 + pnoise(s + 1, 4, (1, 40)) * 6
    return b[..., None] * np.array([1, 1.012, 1.04])


def mat_acrylic(s=441):
    return np.array([236, 40, 160])[None, None, :] * (0.99 + 0.01 * pnoise(s, 60))[..., None]


def mat_tolex(s=451):
    v_ = pnoise(s, 3.2); crease = -np.exp(-(v_ * 3) ** 2) * 0.5
    b = 42 * (1 + v_ * 0.08 + crease) + pnoise(s + 1, 0.8) * 2
    return b[..., None] * np.array([1, 1, 1.05])


MATERIALS = [('diamond_plate', mat_diamond), ('carbon_fiber', mat_carbon), ('printed_pla', mat_pla),
             ('brushed_alu', mat_alu), ('neon_acrylic', mat_acrylic), ('tolex', mat_tolex)]


def save_mat(slug, rgb):
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), 'RGB').save(os.path.join(OUT, 'mat_' + slug + '.png'), optimize=True)
    print('wrote mat_' + slug)


if __name__ == '__main__':
    want = set(sys.argv[1:])
    for slug, fn in WOODS:
        if not want or slug in want:
            save(slug, fn())
    for slug, fn in MATERIALS:
        if not want or slug in want or 'mats' in want:
            save_mat(slug, fn())
