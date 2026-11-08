"""Renders BACKDROP's ART backgrounds into res/backdrops/*.jpg (19: the first 10, plus the PSYCHEDELIC set) (1920 x 1200, dark, so modules stay the star).

They are made once, here, and shipped as pictures: Rack draws each as ONE textured rectangle, so the most detailed
art costs no more CPU or GPU than a plain colour. Run: python tools/make_backdrop_art.py  (needs numpy, scipy, Pillow)
Each piece is seeded, so a re-run draws the same pictures.
"""
import os
import numpy as np
from scipy import ndimage
from PIL import Image, ImageDraw, ImageFilter

W, H = 1920, 1200
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'res', 'backdrops')
os.makedirs(OUT, exist_ok=True)


def noise(seed, octaves=6, base=4, persistence=0.55, w=W, h=H):
    """Fractal value noise in 0..1: random grids, smoothly zoomed up and summed."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        gh, gw = base * 2 ** o + 1, int(base * 2 ** o * w / h) + 1
        g = rng.random((gh, gw))
        z = ndimage.zoom(g, (h / gh, w / gw), order=3)[:h, :w]
        out[:z.shape[0], :z.shape[1]] += z * amp
        total += amp
        amp *= persistence
    out /= total
    return (out - out.min()) / (out.max() - out.min())


def ramp(t, stops):
    """Colour ramp: t (0..1 array) through [(pos, (r, g, b)), ...]."""
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,))
    for (p0, c0), (p1, c1) in zip(stops[:-1], stops[1:]):
        m = (t >= p0) & (t <= p1)
        k = ((t - p0) / max(p1 - p0, 1e-6))[m][:, None]
        out[m] = np.array(c0) * (1 - k) + np.array(c1) * k
    return out


def vignette(img, strength=0.55):
    y, x = np.mgrid[0:H, 0:W]
    d = np.sqrt(((x - W / 2) / (W / 2)) ** 2 + ((y - H / 2) / (H / 2)) ** 2)
    return img * (1 - strength * np.clip(d - 0.35, 0, 1) ** 1.4)[..., None]


def stars(img, seed, n=1600):
    rng = np.random.default_rng(seed)
    ys, xs = rng.integers(0, H, n), rng.integers(0, W, n)
    b = rng.random(n) ** 3
    layer = np.zeros((H, W))
    layer[ys, xs] = b
    glow = ndimage.gaussian_filter(layer, 1.2) * 6 + layer
    return img + glow[..., None] * np.array([1.0, 0.95, 0.9])


def save(name, arr):
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    im.save(os.path.join(OUT, name + '.jpg'), quality=88, optimize=True, progressive=True)
    print('wrote', name)


def glow_layer(draw_fn, blur=(3, 12), seed=0):
    """Lines drawn sharp, plus soft blurred copies for a neon glow. Returns float RGB."""
    im = Image.new('RGB', (W, H), (0, 0, 0))
    draw_fn(ImageDraw.Draw(im), np.random.default_rng(seed))
    a = np.asarray(im).astype(float)
    out = a.copy()
    for b in blur:
        out += np.asarray(im.filter(ImageFilter.GaussianBlur(b))).astype(float) * 1.4
    return out


# 1 NEBULA: layered cloud in violet, teal and amber with stars
def nebula():
    n1, n2, n3 = noise(11, 8, 2, 0.62), noise(12, 8, 3, 0.6), noise(13, 6, 2)
    ridged = 1 - np.abs(noise(15, 8, 3, 0.6) * 2 - 1)   # filaments
    dens = np.clip((n1 * 0.6 + n2 * 0.4) ** 3.2 * 2.6 + ridged ** 6 * 0.5 * n1, 0, 1)
    col = ramp(n3, [(0, (40, 10, 70)), (0.45, (10, 80, 110)), (0.7, (150, 60, 120)), (1, (255, 150, 60))])
    img = col * dens[..., None] * 0.95 + np.array([3, 3, 9])
    img = stars(img * 255 / 255, 14) if False else stars(img, 14)
    save('nebula', vignette(img))


# 2 AURORA: ribbons of light over deep navy
def aurora():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    n = noise(21, 6, 3)
    img = np.zeros((H, W, 3)) + ramp(y / H, [(0, (4, 10, 26)), (1, (1, 2, 6))])
    for k, (c, yy, amp) in enumerate([((60, 255, 170), 0.33, 90), ((40, 200, 255), 0.45, 70), ((180, 90, 255), 0.27, 60)]):
        centre = H * yy + amp * np.sin(x / 260 + k * 1.7) + 120 * (n - 0.5)
        d = (y - centre)
        band = np.exp(-(d / 22) ** 2) + 0.55 * np.exp(-((d + 60) / 110) ** 2) * (d < 0)
        streak = 0.6 + 0.4 * np.sin(x / 9 + n * 30)
        img += np.array(c) * (band * streak * 0.55)[..., None]
    img = stars(img, 22, 900)
    save('aurora', vignette(img))


# 3 TOPOGRAPHIC: contour lines of a fractal landscape, amber on black
def topo():
    h = ndimage.gaussian_filter(noise(31, 5, 2, 0.5), 10)
    h = (h - h.min()) / (h.max() - h.min())
    levels = h * 26
    frac = np.abs(levels - np.round(levels))
    line = np.clip(1 - frac / 0.045, 0, 1)
    major = (np.round(levels) % 5 == 0)
    bright = line * np.where(major, 1.0, 0.45)
    glow = ndimage.gaussian_filter(bright, 2.2)
    img = np.array([255, 130, 30]) * (bright * 0.8 + glow * 0.9)[..., None] + np.array([6, 5, 4])
    img += ramp(h, [(0, (0, 0, 0)), (1, (26, 16, 6))])
    save('topographic', vignette(img, 0.45))


# 4 SYNTHWAVE: a striped sun over a perspective grid and a mountain line (dark, so modules read)
def synthwave():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    hz = H * 0.58
    img = ramp(y / hz, [(0, (8, 2, 24)), (0.7, (50, 8, 60)), (1, (120, 30, 70))]) * (y < hz)[..., None]
    # the sun
    r = np.sqrt((x - W / 2) ** 2 + (y - hz + 40) ** 2)
    sun = (r < 260) & (y < hz)
    stripes = ~((y > hz - 170) & (np.mod(y - (hz - 170), 34) < 8 + (y - (hz - 170)) / 16))
    sc = ramp((y - (hz - 300)) / 300, [(0, (255, 220, 90)), (1, (255, 40, 120))])
    img = np.where((sun & stripes)[..., None], sc * 0.85, img)
    img += np.array([255, 60, 140]) * (np.exp(-np.clip(r - 260, 0, None) / 90) * 0.25 * (y < hz))[..., None]
    # mountains
    n = noise(41, 5, 6, 0.5, W, 1)[0]
    ridge = hz - 40 - 110 * n - 60 * np.sin(np.arange(W) / 210)
    img = np.where(((y > ridge[None, :]) & (y < hz))[..., None], np.array([10, 4, 22]), img)
    # the floor grid
    fl = y >= hz
    depth = (y - hz + 1) / (H - hz)
    zz = 1 / np.maximum(depth, 1e-3)
    hline = np.abs(np.mod(zz * 6, 1) - 0.5) > 0.5 - 0.06 * depth * 3
    xv = (x - W / 2) / np.maximum(depth, 1e-3)
    vline = np.abs(np.mod(xv / 160, 1) - 0.5) > 0.5 - 0.012 / np.maximum(depth, 0.02) * 1.2
    grid = (hline | vline) & fl
    gimg = np.zeros((H, W)) + grid * np.clip(depth * 1.6, 0, 1)
    gl = ndimage.gaussian_filter(gimg, 2.5)
    img = np.where(fl[..., None], np.array([6, 2, 16]) + np.array([255, 40, 200]) * (gimg * 0.7 + gl * 1.2)[..., None], img)
    save('synthwave', vignette(img * 0.9, 0.5))


# 5 FLOW FIELD: thousands of trails following a noise field, orange and cyan
def flow():
    f = noise(51, 4, 2, 0.5, W // 4, H // 4)
    ang = f * np.pi * 4

    def draw(d, rng):
        for i in range(5200):
            px, py = rng.random() * W, rng.random() * H
            c = (255, 120, 20) if rng.random() < 0.55 else (40, 210, 255)
            k = 0.18 + 0.5 * rng.random() ** 2
            col = tuple(int(v * k) for v in c)
            pts = [(px, py)]
            for s in range(90):
                a = ang[min(int(py / 4), H // 4 - 1), min(int(px / 4), W // 4 - 1)]
                px += np.cos(a) * 3.2
                py += np.sin(a) * 3.2
                if not (0 <= px < W and 0 <= py < H):
                    break
                pts.append((px, py))
            if len(pts) > 2:
                d.line(pts, fill=col, width=1)
    img = glow_layer(draw, (2, 8), 52) * 0.75 + np.array([4, 4, 7])
    save('flowfield', vignette(img))


# 6 CRYSTAL: shaded Voronoi facets with glowing seams in steel blue
def crystal():
    rng = np.random.default_rng(61)
    pts = np.c_[rng.random(420) * W, rng.random(420) * H]
    from scipy.spatial import cKDTree
    t = cKDTree(pts)
    y, x = np.mgrid[0:H, 0:W]
    q = np.c_[x.ravel(), y.ravel()]
    d, idx = t.query(q, k=2)
    edge = (d[:, 1] - d[:, 0]).reshape(H, W)
    cell = idx[:, 0].reshape(H, W)
    shade = rng.random(len(pts))[cell]
    tilt = ((x - pts[cell, 0]) * rng.normal(0, 1, len(pts))[cell] + (y - pts[cell, 1]) * rng.normal(0, 1, len(pts))[cell]) / 300
    base = np.clip(0.12 + 0.18 * shade + 0.08 * tilt, 0, 1)
    img = ramp(base, [(0, (4, 8, 14)), (0.5, (20, 40, 64)), (1, (60, 110, 150))])
    seam = np.exp(-edge / 1.6)
    img += np.array([90, 200, 255]) * (seam * 0.55 + ndimage.gaussian_filter(seam, 3) * 0.6)[..., None]
    save('crystal', vignette(img * 0.85))


# 7 OSCILLOSCOPE: glowing Lissajous traces over a faint scope graticule
def scope():
    def draw(d, rng):
        for gx in range(0, W, 120):
            d.line([(gx, 0), (gx, H)], fill=(10, 34, 22), width=1)
        for gy in range(0, H, 120):
            d.line([(0, gy), (W, gy)], fill=(10, 34, 22), width=1)
        t = np.linspace(0, 2 * np.pi * 6, 9000)
        for k, (a, b, ph, sc, col) in enumerate([(3, 4, 0.3, 430, (90, 255, 140)), (5, 4, 1.1, 360, (60, 220, 255)),
                                                 (2, 3, 0.7, 500, (255, 170, 60)), (7, 6, 0.2, 280, (120, 255, 200))]):
            xs = W / 2 + sc * 1.25 * np.sin(a * t + ph)
            ys = H / 2 + sc * np.sin(b * t)
            d.line(list(zip(xs, ys)), fill=tuple(int(c * 0.55) for c in col), width=2)
    img = glow_layer(draw, (2, 7, 20), 71) * 0.7 + np.array([2, 6, 4])
    save('oscilloscope', vignette(img, 0.6))


# 8 DARK MARBLE: black stone with gold veins
def marble():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    n = ndimage.gaussian_filter(noise(81, 6, 2, 0.55), 3)
    v = np.abs(np.sin((x * 0.6 + y * 0.9) / 260 + n * 9))
    vein = np.clip(1 - v / 0.06, 0, 1) ** 1.3
    fine = np.clip(1 - np.abs(np.sin((x - y * 0.4) / 110 + ndimage.gaussian_filter(noise(82, 6, 3), 2) * 14)) / 0.025, 0, 1) * 0.3
    stone = ramp(noise(83, 6, 4), [(0, (6, 6, 8)), (1, (26, 24, 28))])
    img = stone + np.array([230, 170, 70]) * (vein * 0.9 + ndimage.gaussian_filter(vein, 2) * 0.5 + fine * 0.6)[..., None]
    save('marble', vignette(img, 0.4))


# 9 CIRCUIT MACRO: a routed board of glowing copper traces and vias on deep green
def circuit():
    def draw(d, rng):
        g = 24
        for i in range(900):
            x, y = rng.integers(0, W // g) * g, rng.integers(0, H // g) * g
            pts = [(x, y)]
            dirn = rng.integers(0, 4)
            for s in range(rng.integers(4, 18)):
                if rng.random() < 0.3:
                    dirn = (dirn + rng.choice([-1, 1, 0.5])) % 4
                dx, dy = [(1, 0), (0, 1), (-1, 0), (0, -1), (1, 1), (-1, 1), (-1, -1), (1, -1)][int(dirn * 2) % 8]
                x, y = x + dx * g, y + dy * g
                pts.append((x, y))
            w = int(rng.choice([1, 2, 2, 3]))
            col = (210, 130, 40) if rng.random() < 0.8 else (80, 220, 160)
            k = 0.35 + 0.4 * rng.random()
            d.line(pts, fill=tuple(int(c * k) for c in col), width=w)
            ex, ey = pts[-1]
            d.ellipse([ex - 4, ey - 4, ex + 4, ey + 4], outline=tuple(int(c * k * 1.3) for c in col), width=2)
    img = glow_layer(draw, (2, 9), 91) * 0.65 + np.array([3, 14, 9])
    save('circuit', vignette(img))


# 10 SPECTRUM: a blurred spectrogram, warm on black
def spectrum():
    rng = np.random.default_rng(101)
    cols = 240
    t = np.arange(cols)
    spec = np.zeros((120, cols))
    for f in range(120):
        spec[f] = np.exp(-f / 40) * (0.4 + 0.6 * np.abs(np.sin(t / (8 + f * 0.3) + rng.random() * 6))) * (0.6 + 0.4 * rng.random(cols))
    for k in range(10):   # a few melodic lines
        spec[np.clip((30 + 20 * np.sin(t / 15 + k) + k * 8).astype(int), 0, 119), t] += 1.3
    img2 = ndimage.zoom(spec[::-1], (H / 120, W / cols), order=1)[:H, :W]
    img2 = ndimage.gaussian_filter(img2, (1.5, 3))
    img2 = np.clip(img2 / img2.max(), 0, 1) ** 1.4
    img = ramp(img2, [(0, (2, 2, 5)), (0.35, (60, 10, 60)), (0.65, (220, 70, 30)), (1, (255, 220, 120))])
    save('spectrum', vignette(img * 0.8, 0.5))



# ---------------------------------------------------------------------------------------------------------
# PSYCHEDELIC SET (Nov 2026): drawn from Nick's own abstract Midjourney work (tools/backdrop_influences.json):
# neon on a near-black ground, wildstyle swirls, liquid illusion, sacred geometry, bioluminescent networks.
# Kept dark in the middle (where the modules sit) and loudest toward the edges.
# ---------------------------------------------------------------------------------------------------------
NEON = [(255, 47, 168), (25, 227, 255), (182, 255, 42), (255, 122, 26), (138, 60, 255), (255, 210, 58)]


def calm_centre(img, k=0.55):
    """Darken an oval in the middle so the rack stays readable; the edges keep their colour."""
    y, x = np.mgrid[0:H, 0:W]
    d = np.sqrt(((x - W / 2) / (W * 0.46)) ** 2 + ((y - H / 2) / (H * 0.44)) ** 2)
    return img * (1 - k * np.clip(1.15 - d, 0, 1) ** 1.2)[..., None]


# 11 NEON WILDSTYLE: fat interlocking 3D graffiti tubes with arrow heads, neon over black, a calmer core
def wildstyle():
    rng = np.random.default_rng(111)
    f = noise(112, 3, 2, 0.5, W // 4, H // 4)
    ang = f * np.pi * 4.5
    base = ramp(noise(113, 4, 2), [(0, (8, 4, 16)), (1, (22, 8, 34))])
    for i in range(130):
        col = np.array(NEON[int(rng.integers(0, len(NEON)))], float)
        # start mostly toward the edges
        px, py = rng.random() * W, rng.random() * H
        if rng.random() < 0.6 and ((px - W / 2) / (W * 0.3)) ** 2 + ((py - H / 2) / (H * 0.3)) ** 2 < 1:
            px = (px + W / 2) % W
        pts = []
        for s_ in range(int(rng.integers(30, 90))):
            a = ang[min(int(py / 4), H // 4 - 1), min(int(px / 4), W // 4 - 1)] + 0.6 * np.sin(s_ / 6)
            px += np.cos(a) * 10; py += np.sin(a) * 10
            if not (0 <= px < W and 0 <= py < H): break
            pts.append((px, py))
        if len(pts) < 6: continue
        wdt = int(rng.integers(30, 74))
        tube = Image.new('L', (W, H), 0); dr = ImageDraw.Draw(tube)
        dr.line(pts, fill=255, width=wdt, joint='curve')
        for (qx, qy) in (pts[0], pts[-1]):
            dr.ellipse([qx - wdt / 2, qy - wdt / 2, qx + wdt / 2, qy + wdt / 2], fill=255)
        (x1, y1), (x0, y0) = pts[-1], pts[-4]
        dx, dy = x1 - x0, y1 - y0; n = max(np.hypot(dx, dy), 1e-3); dx, dy = dx / n, dy / n
        dr.polygon([(x1 + dx * wdt * 1.5, y1 + dy * wdt * 1.5), (x1 - dy * wdt * 1.1, y1 + dx * wdt * 1.1), (x1 + dy * wdt * 1.1, y1 - dx * wdt * 1.1)], fill=255)
        m = np.asarray(tube) > 127
        if not m.any(): continue
        dist = ndimage.distance_transform_edt(m)
        shade = np.clip(dist / (wdt * 0.5), 0, 1)
        hi = np.clip(1 - np.abs(shade - 0.66) / 0.1, 0, 1) ** 2
        body = col * (0.18 + 0.82 * shade[..., None] ** 0.8) + 255 * hi[..., None] * 0.5
        rim = ndimage.binary_dilation(m, iterations=5) & ~m
        drop = ndimage.shift(m.astype(float), (10, 10), order=0) > 0.5   # drop shadow, down-right
        base = np.where((drop & ~m)[..., None], base * 0.35, base)
        base = np.where(m[..., None], body, base)
        base = np.where(rim[..., None], np.array([3, 1, 6]), base)
    img = base * 0.82 + ndimage.gaussian_filter(base, (18, 18, 0)) * 0.35
    save('wildstyle', vignette(calm_centre(img, 0.6), 0.45))


# 12 LIQUID ILLUSION: marbled ribbons, warm orange against teal and rose, a few glossy orbs (dark middle)
def liquid():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    n1 = ndimage.gaussian_filter(noise(121, 5, 2, 0.55), 6)
    n2 = ndimage.gaussian_filter(noise(122, 5, 3, 0.5), 6)
    wx = x / W * 5 + n1 * 6; wy = y / H * 3.4 + n2 * 6
    t = (np.sin(wx + np.sin(wy * 1.3) * 2.2) + np.sin(wy * 1.7 + np.cos(wx * 0.9) * 2.0)) * 0.25 + 0.5
    band = np.abs(np.sin(t * np.pi * 7)) ** 1.6
    col = ramp(t, [(0, (10, 6, 12)), (0.22, (16, 130, 150)), (0.45, (220, 40, 110)), (0.62, (250, 110, 30)), (0.8, (240, 196, 110)), (1, (12, 8, 14))])
    img = col * (0.12 + 0.6 * band[..., None])
    img += np.array([255, 236, 210]) * (np.clip(1 - band / 0.03, 0, 1) * 0.18)[..., None]
    rng = np.random.default_rng(123)
    for i in range(12):
        cx, cy, r = rng.random() * W, rng.random() * H, rng.integers(22, 70)
        if ((cx - W / 2) / (W * 0.35)) ** 2 + ((cy - H / 2) / (H * 0.33)) ** 2 < 1: continue
        d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / r
        m = d < 1
        sh = np.sqrt(np.clip(1 - d ** 2, 0, 1))
        c = np.array(NEON[i % 5], float)
        orb = c * (0.2 + 0.55 * sh[..., None]) + 255 * (np.exp(-(((x - cx + r * 0.35) ** 2 + (y - cy + r * 0.35) ** 2) / (r * 0.16) ** 2)))[..., None] * 0.75
        img = np.where(m[..., None], orb, img)
    save('liquid', vignette(calm_centre(img, 0.7), 0.5))


# 13 SACRED KALEIDOSCOPE: a 12-fold mandala of glowing geometry, prismatic edges, black centre field
def kaleido():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    cx, cy = W / 2, H / 2
    r = np.sqrt((x - cx) ** 2 + (y - cy) ** 2); a = np.arctan2(y - cy, x - cx)
    seg = np.pi / 6
    am = np.abs(np.mod(a, 2 * seg) - seg)                      # mirror into one wedge
    u, v = r * np.cos(am), r * np.sin(am)
    n = ndimage.gaussian_filter(noise(131, 4, 2, 0.5), 2)
    pat = (np.sin(u / 38 + n * 3) * np.cos(v / 22) + np.sin((u + v) / 61) + np.cos(r / 47 - am * 4)) / 3
    lines = np.clip(1 - np.abs(np.sin(pat * np.pi * 3)) / 0.09, 0, 1)
    rings = np.clip(1 - np.abs(np.sin(r / 52)) / 0.04, 0, 1) * 0.6
    petals = np.clip(1 - np.abs(np.sin(am * 6 + r / 90)) / 0.05, 0, 1) * 0.5
    hue = np.mod(r / 900 + am / seg * 0.15 + n * 0.3, 1)
    col = ramp(hue, [(0, NEON[4]), (0.2, NEON[1]), (0.4, NEON[2]), (0.6, NEON[5]), (0.8, NEON[0]), (1, NEON[4])])
    k = lines + rings + petals
    img = col * (k * 0.75 + ndimage.gaussian_filter(k, 4) * 0.9)[..., None]
    img *= np.clip((r - 140) / 420, 0.08, 1)[..., None]       # quiet middle
    img += np.array([6, 4, 12])
    save('kaleidoscope', vignette(img, 0.35))


# 14 BIOLUMINESCENT NETWORK: branching fibre-optic mycelium glowing cyan on deep blue-black
def biolum():
    def draw(d, rng):
        for tree in range(70):
            stack = [(rng.random() * W, rng.random() * H, rng.random() * 2 * np.pi, int(rng.integers(5, 9)))]
            while stack:
                x, y, a, life = stack.pop()
                pts = [(x, y)]
                for s in range(int(rng.integers(8, 22))):
                    a += rng.normal(0, 0.22); x += np.cos(a) * 7; y += np.sin(a) * 7
                    pts.append((x, y))
                c = (25, 227, 255) if rng.random() < 0.75 else (120, 255, 200)
                k = 0.25 + 0.12 * life
                d.line(pts, fill=tuple(int(v * k) for v in c), width=max(1, life // 3))
                d.ellipse([x - 2.5, y - 2.5, x + 2.5, y + 2.5], fill=tuple(int(v * min(1, k * 2)) for v in c))
                if life > 1:
                    for b in range(int(rng.integers(1, 3))):
                        stack.append((x, y, a + rng.normal(0, 0.8), life - 1))
    img = glow_layer(draw, (2, 7, 22), 141) * 0.7
    img += ramp(noise(142, 5, 2), [(0, (2, 8, 14)), (1, (6, 22, 34))])
    save('biolum', vignette(calm_centre(img, 0.45), 0.55))


# 15 MACHINE ELF FORGE: molten red-orange biomechanical lattice, lit from inside
def forge():
    rng = np.random.default_rng(151)
    from scipy.spatial import cKDTree
    pts = np.c_[rng.random(260) * W, rng.random(260) * H]
    y, x = np.mgrid[0:H, 0:W]
    d, idx = cKDTree(pts).query(np.c_[x.ravel(), y.ravel()], k=2)
    edge = (d[:, 1] - d[:, 0]).reshape(H, W)
    rib = np.exp(-edge / 9)                                    # thick organic ribs
    core = np.exp(-d[:, 0].reshape(H, W) / 70)                 # hot cores in each cell
    n = noise(152, 6, 3, 0.55)
    heat = np.clip(rib * 0.9 + core * 0.6 * n, 0, 1.4)
    img = ramp(np.clip(heat / 1.4, 0, 1), [(0, (12, 4, 4)), (0.35, (80, 16, 10)), (0.6, (170, 50, 14)), (0.8, (255, 122, 26)), (1, (255, 220, 120))])
    circuit = np.clip(1 - np.abs(np.sin(x / 7.0 + n * 20)) / 0.05, 0, 1) * (rib > 0.5) * 0.25
    img += np.array([255, 160, 60]) * circuit[..., None]
    save('forge', vignette(calm_centre(img * 0.85, 0.5), 0.5))


# 16 ESCHER DREAMSCAPE: impossible tumbling blocks (three-face cubes that read both ways), warm stone, violet haze,
# the lower part melting
def escher():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    s3 = np.sqrt(3)
    R = 64.0   # hex size
    q = (s3 / 3 * x - y / 3) / R; r = (2 / 3 * y) / R          # axial coords (pointy-top hexes)
    cx_, cz_ = q, r; cy_ = -cx_ - cz_
    rx, ry, rz = np.round(cx_), np.round(cy_), np.round(cz_)
    dx, dy, dz = np.abs(rx - cx_), np.abs(ry - cy_), np.abs(rz - cz_)
    fx = (dx > dy) & (dx > dz); fy = ~fx & (dy > dz)
    rx = np.where(fx, -ry - rz, rx); ry = np.where(fy, -rx - rz, ry); rz = np.where(~fx & ~fy, -rx - ry, rz)
    hx = R * s3 * (rx + rz / 2); hy = R * 1.5 * rz              # the hex centre
    a = np.arctan2(y - hy, x - hx)                              # which third of the hex: top / left / right face
    face = np.mod(np.floor((a + np.pi / 2 + np.pi / 6) / (2 * np.pi / 3)), 3).astype(int)
    shade = np.choose(face, [0.92, 0.58, 0.32])
    n = ndimage.gaussian_filter(noise(161, 5, 2), 8)
    shade = shade * (0.8 + 0.25 * np.sin((rx * 1.7 + rz * 0.9)))
    # seams between faces and cells
    ang_edge = np.abs(np.mod((a + np.pi / 2 + np.pi / 6), 2 * np.pi / 3) - np.pi / 3)
    rr = np.hypot(x - hx, y - hy) / R
    seam = np.clip(1 - (np.pi / 3 - ang_edge) * rr / 0.05, 0, 1) * (rr < 1.02)
    yy = np.clip(y + 90 * (n - 0.5) * np.clip((y / H - 0.45) / 0.55, 0, 1) ** 1.5, 0, H - 1)   # melting lower half
    shade = ndimage.map_coordinates(shade, [yy, x], order=1)
    seam = ndimage.map_coordinates(seam.astype(float), [yy, x], order=1)
    img = ramp(shade, [(0, (14, 9, 8)), (0.5, (84, 54, 38)), (1, (190, 156, 120))]) * 0.62
    img *= (1 - 0.55 * seam)[..., None]
    img += np.array([138, 60, 255]) * (np.clip(1 - y / H * 1.5, 0, 1) * 0.22 * n)[..., None]
    save('escher', vignette(calm_centre(img, 0.5), 0.55))


# 17 GREY BIOMORPH: monochrome organic tendrils and pods with real relief (the calm psychedelic option)
def biomorph():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    n1 = ndimage.gaussian_filter(noise(171, 6, 2, 0.55), 3)
    n2 = ndimage.gaussian_filter(noise(172, 5, 2, 0.5), 5)
    field = np.sin((n1 * 10 + n2 * 4) * np.pi)
    tend = np.clip(1 - np.abs(field) / 0.35, 0, 1)                 # rope-like tendrils
    height = ndimage.gaussian_filter(tend, 2.5)
    gy, gx = np.gradient(height * 60)
    light = np.clip(0.5 - gx * 0.5 - gy * 0.5, 0, 1)
    v = tend * (0.25 + 0.75 * light) + 0.05 * n2
    rng = np.random.default_rng(173)
    for i in range(55):
        cx, cy, r = rng.random() * W, rng.random() * H, rng.integers(12, 40)
        d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / r
        pod = np.clip(1 - d, 0, 1) ** 0.6
        lit = np.clip(0.55 - ((x - cx) + (y - cy)) / (r * 2.2), 0, 1)
        v = np.maximum(v, pod * (0.3 + 0.6 * lit) * (d < 1))
        v += 0.35 * np.exp(-((d - 1) / 0.08) ** 2)
    img = np.repeat((10 + 165 * np.clip(v, 0, 1.2))[..., None], 3, axis=2) * np.array([0.98, 1.0, 1.06])
    save('biomorph', vignette(calm_centre(img, 0.5), 0.5))


# 18 STICKER BOMB: cartoon suns, moons, rainbows and mushrooms with black outlines, edges only
def sticker():
    im = Image.new('RGB', (W, H), (10, 10, 10)); d = ImageDraw.Draw(im)
    rng = np.random.default_rng(181)
    cols = [(255, 210, 58), (232, 48, 42), (26, 122, 208), (42, 176, 74), (255, 47, 168), (255, 122, 26)]
    for i in range(190):
        x, y = rng.random() * W, rng.random() * H
        if ((x - W / 2) / (W * 0.36)) ** 2 + ((y - H / 2) / (H * 0.34)) ** 2 < 1: continue   # keep the middle clear
        s = rng.integers(36, 110); kind = rng.integers(0, 4); c = cols[rng.integers(0, len(cols))]
        if kind == 0:   # sun (some get a face)
            for k in range(12):
                a = k * np.pi / 6
                d.line([(x, y), (x + np.cos(a) * s * 1.1, y + np.sin(a) * s * 1.1)], fill=(0, 0, 0), width=9)
                d.line([(x, y), (x + np.cos(a) * s * 1.1, y + np.sin(a) * s * 1.1)], fill=(255, 150, 30) if k % 2 else (255, 214, 40), width=5)
            d.ellipse([x - s * 0.6, y - s * 0.6, x + s * 0.6, y + s * 0.6], fill=(255, 214, 40), outline=(0, 0, 0), width=5)
            if rng.random() < 0.5:
                d.ellipse([x - s * 0.25, y - s * 0.15, x - s * 0.1, y], fill=(0, 0, 0)); d.ellipse([x + s * 0.1, y - s * 0.15, x + s * 0.25, y], fill=(0, 0, 0))
                d.arc([x - s * 0.3, y - s * 0.1, x + s * 0.3, y + s * 0.35], 20, 160, fill=(0, 0, 0), width=4)
        elif kind == 1:   # moon
            d.ellipse([x - s * 0.6, y - s * 0.6, x + s * 0.6, y + s * 0.6], fill=(240, 230, 140), outline=(0, 0, 0), width=5)
            d.ellipse([x - s * 0.3, y - s * 0.7, x + s * 0.8, y + s * 0.5], fill=(10, 10, 10))
        elif kind == 2:   # rainbow
            for k, rc in enumerate(cols[:5]):
                rr = s - k * s * 0.16
                d.arc([x - rr, y - rr, x + rr, y + rr], 180, 360, fill=(0, 0, 0), width=int(s * 0.18))
                d.arc([x - rr, y - rr, x + rr, y + rr], 180, 360, fill=rc, width=int(s * 0.12))
        else:   # mushroom
            d.rectangle([x - s * 0.16, y, x + s * 0.16, y + s * 0.6], fill=(236, 226, 206), outline=(0, 0, 0), width=4)
            d.pieslice([x - s * 0.6, y - s * 0.5, x + s * 0.6, y + s * 0.5], 180, 360, fill=c, outline=(0, 0, 0), width=5)
            for k in range(3):
                dx = (k - 1) * s * 0.3
                d.ellipse([x + dx - 6, y - s * 0.28 - 6, x + dx + 6, y - s * 0.28 + 6], fill=(250, 250, 240))
    img = np.asarray(im).astype(float) * 0.86
    save('stickerbomb', vignette(img, 0.35))


# 19 GLASS SPHERES: floating glass orbs refracting neon, soft depth blur, black space
def spheres():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = ramp(noise(191, 5, 2), [(0, (4, 4, 10)), (1, (14, 8, 26))])
    rng = np.random.default_rng(192)
    orbs = sorted([(rng.random() * W, rng.random() * H, rng.integers(30, 170)) for i in range(34)], key=lambda o: o[2])
    for i, (cx, cy, r) in enumerate(orbs):
        if ((cx - W / 2) / (W * 0.33)) ** 2 + ((cy - H / 2) / (H * 0.3)) ** 2 < 1 and r > 60: r = int(r * 0.45)
        d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / r
        m = d < 1
        if not m.any(): continue
        c1, c2 = np.array(NEON[i % 6], float), np.array(NEON[(i + 2) % 6], float)
        fres = np.clip(d, 0, 1) ** 3                              # bright rim
        inner = ramp(np.clip((y - cy) / (2 * r) + 0.5, 0, 1), [(0, tuple(c2 * 0.25)), (1, tuple(c1 * 0.5))])
        spec = np.exp(-(((x - cx + r * 0.38) ** 2 + (y - cy + r * 0.42) ** 2) / (r * 0.16) ** 2))
        orb = inner * 0.6 + c1 * fres[..., None] * 0.8 + 255 * spec[..., None] * 0.9
        a = (m * (0.55 + 0.45 * fres))[..., None]
        img = img * (1 - a) + orb * a
        img += c1 * (np.exp(-np.clip(d - 1, 0, None) * 6) * 0.06 * (~m))[..., None]
    save('spheres', vignette(img * 0.95, 0.5))


PSY = (wildstyle, liquid, kaleido, biolum, forge, escher, biomorph, sticker, spheres)

if __name__ == '__main__':
    import sys
    wanted = set(sys.argv[1:])
    for fn in (nebula, aurora, topo, synthwave, flow, crystal, scope, marble, circuit, spectrum) + PSY:
        if not wanted or fn.__name__ in wanted or ('psy' in wanted and fn in PSY):
            fn()
