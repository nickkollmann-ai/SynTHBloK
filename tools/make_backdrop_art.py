"""Renders BACKDROP's ART backgrounds into res/backdrops/*.jpg (44: the first 10, the PSYCHEDELIC set, and round 6 from Nick's Midjourney picks) (1920 x 1200, dark, so modules stay the star).

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


# ======== ROUND 6: 25 pieces drawn from Nick's 50 approved Midjourney picks (#65-#195 of the scan) ========
# Shared bits: polar coordinates, a mirrored (butterfly) field, glossy orbs, escape-time fractals.
def polar(cx=W / 2, cy=H / 2):
    y, x = np.mgrid[0:H, 0:W].astype(float)
    return x, y, np.sqrt((x - cx) ** 2 + (y - cy) ** 2), np.arctan2(y - cy, x - cx)


def neon_ramp(t, cols=(4, 0, 3, 5, 2, 1, 4)):
    st = [(i / (len(cols) - 1), NEON[c]) for i, c in enumerate(cols)]
    return ramp(np.mod(t, 1), st)


def escape(c, z, it=160):
    """Smooth escape-time count for z -> z^2 + c (both arrays)."""
    n = np.zeros(z.shape); alive = np.ones(z.shape, bool)
    for i in range(it):
        z[alive] = z[alive] ** 2 + (c[alive] if np.ndim(c) else c)
        esc = alive & (np.abs(z) > 4)
        n[esc] = i + 1 - np.log2(np.log2(np.abs(z[esc]) + 1e-9) + 1e-9)
        alive &= ~esc
    n[alive] = 0
    return n, alive


# 20 MOTION PAINT (#65): broad loaded-brush strokes sweeping round the edges, red / teal / gold, wet highlights
def motion():
    rng = np.random.default_rng(201)
    ang = ndimage.gaussian_filter(noise(202, 3, 2, 0.5, W // 4, H // 4), 3) * np.pi * 3
    base = ramp(noise(203, 4, 2), [(0, (10, 6, 8)), (1, (26, 12, 14))])
    cols = [(224, 52, 46), (24, 168, 170), (246, 186, 70), (250, 238, 220), (120, 30, 60)]
    im = Image.new('RGB', (W, H)); im.paste(Image.fromarray(base.astype(np.uint8)))
    dr = ImageDraw.Draw(im)
    for i in range(900):
        px, py = rng.random() * W, rng.random() * H
        c = np.array(cols[int(rng.integers(0, len(cols)))]) * (0.55 + 0.45 * rng.random())
        w_ = int(rng.integers(3, 16)); pts = []
        for k in range(int(rng.integers(12, 40))):
            a = ang[min(int(py / 4), H // 4 - 1), min(int(px / 4), W // 4 - 1)]
            px += np.cos(a) * 9; py += np.sin(a) * 9
            if not (0 <= px < W and 0 <= py < H): break
            pts.append((px, py))
        if len(pts) > 2: dr.line(pts, fill=tuple(int(v) for v in c), width=w_, joint='curve')
    img = np.asarray(im.filter(ImageFilter.GaussianBlur(1.2))).astype(float)
    img = img * 0.85 + ndimage.gaussian_filter(img, (10, 10, 0)) * 0.3
    save('motion', vignette(calm_centre(img, 0.65), 0.45))


# 21 MIRROR SWIRL (#97-#111): the butterfly symmetry of the neon illusion paintings, swirls meeting on the centre line
def mirror_swirl():
    x, y, r, a = polar()
    xm = np.abs(x - W / 2)
    n = ndimage.gaussian_filter(noise(211, 5, 2, 0.55), 4)
    t = np.sin(xm / 70 + np.sin(y / 90 + n * 5) * 2.4) + np.cos(np.hypot(xm - 520, y - H / 2) / 34 - n * 6)
    band = np.clip(1 - np.abs(np.sin(t * 3.0)) / 0.18, 0, 1)
    col = neon_ramp(t * 0.18 + n * 0.6 + y / H * 0.3)
    img = col * (0.04 + 0.7 * band[..., None]) + col * ndimage.gaussian_filter(band, 6)[..., None] * 0.3
    save('mirror_swirl', vignette(calm_centre(img, 0.8), 0.45))


# 22 IRIS VORTEX (#67-#75): concentric eye-like vortices at the corners, a ring of colour round a dark pupil
def iris():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = np.zeros((H, W, 3)) + np.array([8, 4, 12])
    n = ndimage.gaussian_filter(noise(221, 4, 3, 0.5), 3)
    for i, (cx, cy, R) in enumerate([(220, 200, 330), (W - 240, 240, 300), (260, H - 220, 280), (W - 210, H - 200, 340), (W / 2, -40, 260), (W / 2, H + 60, 280)]):
        r = np.hypot(x - cx, y - cy) / R; a = np.arctan2(y - cy, x - cx)
        fib = np.abs(np.sin(a * 40 + r * 6 + n * 4)) ** 3
        ring = np.clip(1 - np.abs(r - 0.55) / 0.42, 0, 1)
        col = neon_ramp(r * 0.8 + a / np.pi * 0.2 + i * 0.17)
        irisc = col * (ring * (0.35 + 0.65 * fib))[..., None]
        pupil = np.clip((0.16 - r) / 0.03, 0, 1)
        img = img + irisc * (r < 1)[..., None]
        img = img * (1 - pupil[..., None] * 0.95)
        img += np.array([255, 250, 240]) * np.exp(-((x - cx + R * 0.09) ** 2 + (y - cy + R * 0.09) ** 2) / (R * 0.035) ** 2)[..., None] * 0.8
    save('iris', vignette(calm_centre(img, 0.5), 0.4))


# 23 ILLUSION TUNNEL (#113, #114): nested frames receding into a far vanishing point, each edge lit in a new colour
def tunnel():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    u = np.abs(x - W / 2) / (W / 2); v = np.abs(y - H / 2) / (H / 2)
    d = np.maximum(u, v) + 1e-3
    depth = np.log(d) * 5.0
    n = ndimage.gaussian_filter(noise(231, 4, 2, 0.5), 3)
    edge = np.clip(1 - np.abs(np.sin(depth * np.pi + n * 1.5)) / 0.12, 0, 1)
    col = neon_ramp(depth * 0.09 + n * 0.3)
    img = col * (edge * 0.8 + ndimage.gaussian_filter(edge, 5) * 0.7)[..., None] * np.clip(d * 1.6, 0, 1)[..., None]
    img += np.array([6, 4, 10])
    save('tunnel', vignette(calm_centre(img, 0.45), 0.35))


# 24 PRISM SHARDS (#119, #121): faceted crystal shards, each face its own colour, white-hot edges
def prism():
    rng = np.random.default_rng(241)
    pts = rng.random((260, 2)) * [W, H]
    y, x = np.mgrid[0:H:2, 0:W:2].astype(float)
    d = (x[..., None] - pts[:, 0]) ** 2 + (y[..., None] - pts[:, 1]) ** 2
    o = np.argsort(d, axis=2)
    i0 = o[..., 0]; d0 = np.take_along_axis(d, o[..., :1], 2)[..., 0]; d1 = np.take_along_axis(d, o[..., 1:2], 2)[..., 0]
    edge = np.clip(1 - (np.sqrt(d1) - np.sqrt(d0)) / 3.0, 0, 1)
    hue = rng.random(260)[i0]; lit = (0.25 + 0.75 * rng.random(260))[i0]
    grad = (x / W - y / H) * 0.6
    col = neon_ramp(hue + grad * 0.3) * (lit * (0.35 + 0.65 * np.clip(0.5 + grad, 0, 1)))[..., None]
    col += 255 * edge[..., None] * 0.7
    img = np.asarray(Image.fromarray(np.clip(col, 0, 255).astype(np.uint8)).resize((W, H), Image.BILINEAR)).astype(float)
    img = img * 0.8 + ndimage.gaussian_filter(img, (8, 8, 0)) * 0.4
    save('prism', vignette(calm_centre(img * 0.75, 0.75), 0.45))


def _geom_lines(r, a, n, folds):
    seg = np.pi / folds; am = np.abs(np.mod(a, 2 * seg) - seg)
    u, v = r * np.cos(am), r * np.sin(am)
    k = np.clip(1 - np.abs(np.sin(u / 46 + np.cos(v / 30) + n * 2)) / 0.06, 0, 1)
    k += np.clip(1 - np.abs(np.sin(r / 38)) / 0.03, 0, 1) * 0.6
    k += np.clip(1 - np.abs(np.sin(am * folds + r / 120)) / 0.04, 0, 1) * 0.7
    return k


# 25 BRONZE MANDALA (#135-#157): sacred geometry in hammered bronze and verdigris teal, lit from above
def bronze():
    x, y, r, a = polar()
    n = ndimage.gaussian_filter(noise(251, 4, 2, 0.5), 2)
    k = _geom_lines(r, a, n, 8)
    metal = ramp(n * 0.6 + k * 0.25, [(0, (40, 22, 10)), (0.4, (150, 94, 40)), (0.7, (230, 180, 100)), (1, (255, 236, 190))])
    teal = np.array([40, 150, 140]) * (ndimage.gaussian_filter(k, 6) * 0.6)[..., None]
    k = np.minimum(k, 1.6)
    img = metal * (k * 1.3 + ndimage.gaussian_filter(k, 3) * 0.8 + 0.06)[..., None] + teal * 1.5 + np.array([6, 6, 8])
    img *= np.clip((r - 120) / 420, 0.1, 1)[..., None]
    save('bronze_mandala', vignette(img, 0.4))


# 26 FLOWER OF LIFE (#135, #137): overlapping circles across the whole field, glowing where they cross
def flower_of_life():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    R = 110.0; k = np.zeros((H, W))
    for j in range(-1, int(H / (R * 0.866)) + 2):
        for i in range(-1, int(W / R) + 2):
            cx = i * R + (R / 2 if j % 2 else 0); cy = j * R * 0.866
            k += np.clip(1 - np.abs(np.hypot(x - cx, y - cy) - R) / 2.2, 0, 1)
    n = ndimage.gaussian_filter(noise(261, 3, 2, 0.5), 4)
    col = neon_ramp(n * 0.8 + y / H * 0.3, cols=(1, 4, 0, 5, 1))
    img = col * (np.minimum(k, 2) * 0.45 + ndimage.gaussian_filter(k, 5) * 0.45)[..., None] * (0.3 + n[..., None])
    save('flower_of_life', vignette(calm_centre(img, 0.75), 0.45))


def _fibres(seed, cols, count=320, steps=180, width=1, field=4.0):
    rng = np.random.default_rng(seed)
    ang = ndimage.gaussian_filter(noise(seed + 1, 3, 2, 0.5, W // 4, H // 4), 2) * np.pi * field
    def draw(d, rng):
        for i in range(count):
            px, py = rng.random() * W, rng.random() * H; pts = []
            for k in range(steps):
                a = ang[min(int(py / 4), H // 4 - 1), min(int(px / 4), W // 4 - 1)] + rng.normal() * 0.15
                px += np.cos(a) * 4; py += np.sin(a) * 4
                if not (0 <= px < W and 0 <= py < H): break
                pts.append((px, py))
            if len(pts) > 3:
                c = cols[i % len(cols)]; f = 0.3 + 0.7 * rng.random()
                d.line(pts, fill=tuple(int(v * f) for v in c), width=width)
    return glow_layer(draw, blur=(2, 8, 20), seed=seed)


# 27 GOLD FILAMENT (#82): fine golden fibre-optic strands drifting on black
def gold_filament():
    img = _fibres(271, [(255, 196, 90), (255, 150, 40), (255, 236, 180)]) * 0.55 + np.array([6, 4, 2])
    save('gold_filament', vignette(calm_centre(img, 0.7), 0.45))


# 28 VIOLET NEURAL (#82, #189): purple and magenta neurons, bright somas, dendrites branching outward
def violet_neural():
    def draw(d, rng):
        for i in range(60):
            cx, cy = rng.random() * W, rng.random() * H
            c = [(180, 80, 255), (255, 60, 200), (120, 140, 255)][i % 3]
            def branch(px, py, a, ln, w_, depth):
                if depth == 0 or ln < 6: return
                ex, ey = px + np.cos(a) * ln, py + np.sin(a) * ln
                d.line([(px, py), (ex, ey)], fill=tuple(int(v * (0.4 + 0.15 * depth)) for v in c), width=max(1, int(w_)))
                for s_ in (-1, 1):
                    if rng.random() < 0.8: branch(ex, ey, a + s_ * rng.uniform(0.25, 0.7), ln * rng.uniform(0.6, 0.85), w_ * 0.7, depth - 1)
            for k in range(int(rng.integers(4, 8))): branch(cx, cy, rng.random() * 6.3, rng.uniform(30, 70), 3, 6)
            d.ellipse([cx - 7, cy - 7, cx + 7, cy + 7], fill=(255, 220, 255))
    img = glow_layer(draw, blur=(2, 7, 18), seed=281) * 0.6 + np.array([8, 2, 14])
    save('violet_neural', vignette(calm_centre(img, 0.7), 0.45))


# 29 COSMIC SWIRL (#76, #77): a spiral of star-stuff in every colour, arms wrapping the dark middle
def cosmic_swirl():
    x, y, r, a = polar()
    n = noise(291, 7, 3, 0.6)
    arm = np.sin(a * 3 - np.log(r + 30) * 5 + n * 3)
    dens = np.clip(arm * 0.5 + 0.5, 0, 1) ** 3 * np.exp(-(r / 900) ** 2) * (0.4 + n)
    col = neon_ramp(np.log(r + 30) * 0.25 + n * 0.4, cols=(4, 0, 3, 5, 1, 4))
    img = col * dens[..., None] + ndimage.gaussian_filter(col * dens[..., None], (14, 14, 0)) * 0.8
    img = stars(img * np.clip((r - 150) / 300, 0.05, 1)[..., None], 292, 2400)
    save('cosmic_swirl', vignette(img, 0.4))


# 30 DREAM CLOUDS (#86): billowing clouds in purple, green and blue, curling into one another
def dream_clouds():
    n1, n2 = noise(301, 8, 3, 0.6), noise(302, 8, 2, 0.6)
    y, x = np.mgrid[0:H, 0:W].astype(float)
    w = ndimage.map_coordinates(n1, [np.clip(y + (n2 - 0.5) * 260, 0, H - 1), np.clip(x + (n1 - 0.5) * 260, 0, W - 1)], order=1)
    col = ramp(w, [(0, (6, 4, 16)), (0.35, (70, 30, 150)), (0.55, (40, 170, 120)), (0.75, (60, 110, 230)), (1, (240, 220, 255))])
    curl = np.clip(1 - np.abs(np.sin(w * 30)) / 0.05, 0, 1) * 0.2
    g = col.mean(2, keepdims=True); col = g + (col - g) * 1.5
    img = col * (0.6 + curl[..., None]) * 0.85
    save('dream_clouds', vignette(calm_centre(img, 0.6), 0.45))


# 31 INK BLOOM (#88): ink dropped in water, plumes of magenta, blue and gold blooming from the edges
def ink_bloom():
    rng = np.random.default_rng(311)
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = np.zeros((H, W, 3)) + 4
    for i in range(14):
        cx, cy = rng.choice([rng.random() * W * 0.25, W - rng.random() * W * 0.25]), rng.random() * H
        n = noise(312 + i, 6, 3, 0.6)
        d = np.hypot(x - cx, y - cy) / rng.uniform(260, 520) + (n - 0.5) * 1.4
        plume = np.clip(1 - d, 0, 1) ** 1.6
        fil = np.clip(1 - np.abs(np.sin(n * 40)) / 0.1, 0, 1) * plume
        c = np.array([(230, 40, 150), (40, 90, 240), (250, 190, 60), (40, 210, 200)][i % 4], float)
        img += c * (plume * 0.45 + fil * 0.5)[..., None]
    save('ink_bloom', vignette(calm_centre(img, 0.6), 0.45))


# 32 GLASS WORLD (#90): big glass spheres, each holding a spiralling world, rim light and reflections
def glass_world():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = ramp(noise(321, 4, 2), [(0, (6, 4, 12)), (1, (20, 10, 30))])
    for i, (cx, cy, R) in enumerate([(250, 330, 230), (W - 260, 300, 250), (W / 2 - 560, H - 240, 190), (W / 2 + 560, H - 230, 210), (W / 2, H + 120, 300)]):
        r = np.hypot(x - cx, y - cy) / R; a = np.arctan2(y - cy, x - cx); m = r < 1
        sw = np.sin(a * 2 + np.log(r + 0.02) * 6 + i)
        inside = neon_ramp(sw * 0.2 + r * 0.6 + i * 0.2) * (0.25 + 0.75 * np.clip(sw * 0.5 + 0.5, 0, 1))[..., None]
        inside *= (0.5 + 0.5 * np.sqrt(np.clip(1 - r ** 2, 0, 1)))[..., None]
        rim = np.clip(1 - np.abs(r - 0.97) / 0.04, 0, 1)
        spec = np.exp(-((x - cx + R * 0.38) ** 2 + (y - cy + R * 0.42) ** 2) / (R * 0.12) ** 2)
        img = np.where(m[..., None], inside, img) + 255 * (rim * 0.55 + spec * 0.8 * m)[..., None]
    save('glass_world', vignette(img * 0.95, 0.45))


# 33 COPPER MAZE (#96): Escher-style isometric stairs and blocks in copper, impossible and endless
def copper_maze():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    s = 64.0
    u = (x / s + y / (s * 0.577)) / 2; v = (y / (s * 0.577) - x / s) / 2
    iu, iv = np.floor(u), np.floor(v)
    h = (np.sin(iu * 1.7 + iv * 0.9) * 0.5 + 0.5 + noise(331, 3, 2, 0.5) * 0.5)
    fu, fv = u - iu, v - iv
    face = np.where(fu > fv, 0.55, 0.85) * (0.4 + 0.6 * h)
    top = np.clip(1 - np.abs(fu - fv) / 0.06, 0, 1)
    edge = np.clip(1 - np.minimum(np.minimum(fu, 1 - fu), np.minimum(fv, 1 - fv)) / 0.04, 0, 1)
    col = ramp(face, [(0, (30, 14, 8)), (0.5, (150, 70, 30)), (0.8, (220, 130, 60)), (1, (255, 210, 150))])
    img = col * (1 - edge * 0.7)[..., None] + np.array([255, 200, 140]) * top[..., None] * 0.25
    save('copper_maze', vignette(calm_centre(img, 0.7), 0.45))


# 34 TESSELLATION (#80, #81): interlocking Escher tiles that morph from birds to fish across the field (abstract shapes)
def tessellate():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    s = 90.0; m = x / W
    wx = x / s + 0.35 * np.sin(y / s * np.pi * (1 + m)); wy = y / s + 0.35 * np.sin(x / s * np.pi * (2 - m))
    cell = (np.floor(wx) + np.floor(wy)) % 2
    edge = np.clip(1 - np.minimum(np.abs(wx - np.round(wx)), np.abs(wy - np.round(wy))) / 0.05, 0, 1)
    c1 = ramp(m, [(0, (240, 120, 40)), (1, (40, 200, 220))]); c2 = ramp(m, [(0, (40, 30, 90)), (1, (120, 30, 80))])
    img = np.where(cell[..., None] > 0, c1 * 0.55, c2 * 0.7) * (1 - edge * 0.85)[..., None]
    save('tessellation', vignette(calm_centre(img, 0.65), 0.45))


# 35 VIOLET RIDGES (#127, #128): organic purple ridges like a living landscape, magenta crests
def violet_ridges():
    n = noise(351, 8, 3, 0.6)
    ridged = 1 - np.abs(n * 2 - 1)
    lines = np.clip(1 - np.abs(np.sin(ridged * 26)) / 0.12, 0, 1)
    col = ramp(ridged, [(0, (12, 2, 24)), (0.5, (90, 20, 150)), (0.8, (200, 50, 200)), (1, (255, 170, 240))])
    img = col * (0.45 + 0.9 * lines[..., None]) * 1.2
    save('violet_ridges', vignette(calm_centre(img, 0.65), 0.45))


# 36 SNOW GLOBES (#133, #134): little psychedelic worlds sealed in glass, a Mandelbrot swirl in each
def snow_globes():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = np.zeros((H, W, 3)) + np.array([6, 4, 10])
    rng = np.random.default_rng(361)
    spots = [(200, 220), (560, 140), (980, 110), (1380, 150), (1720, 260), (170, 640), (1760, 700), (300, 1010), (720, 1080), (1180, 1070), (1600, 1020)]
    for i, (cx, cy) in enumerate(spots):
        cx += rng.normal() * 30; cy += rng.normal() * 30; R = rng.uniform(80, 135)
        x0, y0 = int(max(cx - R, 0)), int(max(cy - R, 0)); x1, y1 = int(min(cx + R, W)), int(min(cy + R, H))
        if x1 <= x0 or y1 <= y0: continue
        yy, xx = y[y0:y1, x0:x1], x[y0:y1, x0:x1]
        r = np.hypot(xx - cx, yy - cy) / R; m = r < 1
        c = (xx - cx) / R * 1.3 - 0.6 + 1j * (yy - cy) / R * 1.3
        nn, inside = escape(c, np.zeros(c.shape, complex), 90)
        col = neon_ramp(nn * 0.04 + i * 0.13) * (nn > 0)[..., None]
        rim = 255 * (np.clip(1 - np.abs(r - 0.98) / 0.03, 0, 1) * 0.5 + np.exp(-((xx - cx + R * 0.4) ** 2 + (yy - cy + R * 0.4) ** 2) / (R * 0.1) ** 2) * 0.8)
        img[y0:y1, x0:x1] = np.where(m[..., None], col * 0.9 + rim[..., None], img[y0:y1, x0:x1])
    save('snow_globes', vignette(img, 0.4))


# 37 MANDELBROT (#134, #137): the whole set, its black body filling the middle, edges banded in neon
def mandelbrot():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    sc = 3.6 / W
    c = (-0.62 + (x - W / 2) * sc) + 1j * ((y - H / 2) * sc)
    n, inside = escape(c, np.zeros(c.shape, complex), 400)
    t = np.log1p(np.maximum(n, 0))
    band = np.clip(np.sin(t * 6) * 0.5 + 0.5, 0, 1) ** 2
    img = neon_ramp(t * 0.3) * (0.1 + 0.9 * band)[..., None] * np.clip((t - 0.6) / 1.8, 0, 1)[..., None] * (~inside)[..., None]
    save('mandelbrot', vignette(img * 0.8, 0.4))


# 38 JULIA BLOOM (#189): a Julia set opening like a flower, petals of cyan and rose
def julia():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    z = (x - W / 2) / H * 2.3 + 1j * (y - H / 2) / H * 2.3
    n, inside = escape(np.full(z.shape, -0.8 + 0.156j), z, 220)
    t = np.log1p(np.maximum(n, 0)) * 0.22
    img = neon_ramp(t, cols=(1, 0, 4, 5, 1)) * np.clip(t * 1.4, 0, 1)[..., None] * (~inside)[..., None]
    img += ndimage.gaussian_filter(img, (6, 6, 0)) * 0.5
    save('julia', vignette(calm_centre(img * 0.85, 0.6), 0.4))


# 39 CORAL CELLS (#195): glowing cellular walls like living coral, blue-green with warm nuclei
def coral_cells():
    rng = np.random.default_rng(391)
    pts = rng.random((900, 2)) * [W, H]
    y, x = np.mgrid[0:H:2, 0:W:2].astype(float)
    from scipy.spatial import cKDTree
    dd, ii = cKDTree(pts).query(np.stack([x.ravel(), y.ravel()], 1), k=2)
    d0, d1 = dd[:, 0].reshape(x.shape), dd[:, 1].reshape(x.shape)
    wall = np.clip(1 - (d1 - d0) / 6, 0, 1); nuc = np.clip(1 - d0 / 7, 0, 1)
    n = noise(392, 4, 2, 0.5, x.shape[1], x.shape[0])
    col = np.array([40, 230, 210]) * wall[..., None] * (0.4 + n[..., None]) + np.array([255, 150, 60]) * nuc[..., None] * 0.8
    col += np.array([10, 60, 120]) * (d0 / 40)[..., None].clip(0, 1) * 0.3
    img = np.asarray(Image.fromarray(np.clip(col, 0, 255).astype(np.uint8)).resize((W, H), Image.BILINEAR)).astype(float)
    img = img * 0.8 + ndimage.gaussian_filter(img, (6, 6, 0)) * 0.6
    save('coral_cells', vignette(calm_centre(img, 0.7), 0.45))


# 40 LOTUS KALEIDO (#189): an 8-petal kaleidoscope flower in soft neon, petals layered outward
def lotus():
    x, y, r, a = polar()
    seg = np.pi / 8; am = np.abs(np.mod(a, 2 * seg) - seg)
    petal = np.clip(1 - np.abs(np.sin(r / 60 - np.cos(am * 8) * 1.4)) / 0.15, 0, 1)
    vein = np.clip(1 - np.abs(np.sin(am * 24 + r / 30)) / 0.05, 0, 1) * 0.4
    col = neon_ramp(r / 700 + am * 0.3, cols=(0, 4, 1, 5, 0))
    img = col * (petal * 0.7 + vein + ndimage.gaussian_filter(petal, 5) * 0.4)[..., None] * np.clip((r - 150) / 380, 0.08, 1)[..., None]
    save('lotus', vignette(img, 0.4))


# 41 NEON MARBLE (#97-#112): strata of neon paint folded like marble, wildstyle colours, black veins
def neon_marble():
    n = noise(411, 7, 3, 0.6)
    y, x = np.mgrid[0:H, 0:W].astype(float)
    t = np.sin(y / 70 + n * 9 + np.sin(x / 260) * 2)
    vein = np.clip(1 - np.abs(t) / 0.05, 0, 1)
    col = neon_ramp(t * 0.15 + n * 0.8)
    strata = np.abs(np.sin(y / 70 + n * 9 + np.sin(x / 260) * 2) ) ** 3
    img = col * (0.7 * strata * (1 - vein))[..., None] + np.array([4, 2, 8])
    save('neon_marble', vignette(calm_centre(img, 0.7), 0.45))


# 42 MELT (#66, #78): colours dripping down from the top edge, Dali-soft, pooling at the foot
def melt():
    rng = np.random.default_rng(421)
    y, x = np.mgrid[0:H, 0:W].astype(float)
    drip = np.zeros(W)
    for i in range(70):
        cx, w_, ln = rng.random() * W, rng.uniform(20, 80), rng.uniform(150, 700)
        drip = np.maximum(drip, ln * np.clip(1 - ((np.arange(W) - cx) / w_) ** 2, 0, 1) ** 0.5)
    top = (y < (180 + drip[None, :] * 0.8))
    band = neon_ramp(x / W * 1.3 + y / H * 0.4)
    img = np.where(top[..., None], band * 0.5, 0) + np.where((y > H - 150)[..., None], band[::-1] * 0.3, 0)
    edge = top.astype(float); glow = ndimage.gaussian_filter(edge, 10)
    img = ndimage.gaussian_filter(img, (2.5, 2.5, 0)) * 0.7
    img += band * (glow - edge).clip(0, 1)[..., None] * 0.5 + np.array([6, 4, 10])
    save('melt', vignette(img, 0.4))


# 43 THIRD EYE (#67-#81): the eyes that run through the face pieces, abstracted into a field of glowing rings
def third_eye():
    rng = np.random.default_rng(431)
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = np.zeros((H, W, 3)) + np.array([8, 4, 12])
    for i in range(40):
        cx, cy, R = rng.random() * W, rng.random() * H, rng.uniform(26, 90)
        if ((cx - W / 2) / (W * 0.3)) ** 2 + ((cy - H / 2) / (H * 0.3)) ** 2 < 1: continue
        r = np.hypot((x - cx) / 1.6, y - cy) / R
        lid = np.clip(1 - np.abs(r - 1) / 0.08, 0, 1)
        iris_ = np.clip(1 - np.abs(np.hypot(x - cx, y - cy) / R - 0.45) / 0.25, 0, 1) * (r < 1)
        pupil = np.hypot(x - cx, y - cy) / R < 0.15
        c = np.array(NEON[i % 6], float)
        img += c * (lid * 0.8 + iris_ * 0.5)[..., None]
        img[pupil] *= 0.1
    img += ndimage.gaussian_filter(img, (8, 8, 0)) * 0.5
    save('third_eye', vignette(img, 0.4))


# 44 RAINBOW SURF (#98): prismatic waves rolling across, crests catching white light
def rainbow_surf():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    n = ndimage.gaussian_filter(noise(441, 5, 2, 0.55), 4)
    t = y / H * 6 + np.sin(x / 180 + n * 4) * 1.2 + n * 2
    crest = np.clip(1 - np.abs(np.sin(t * np.pi)) / 0.06, 0, 1)
    col = neon_ramp(t * 0.12 + x / W * 0.2)
    img = col * (0.25 + 0.5 * (np.sin(t * np.pi) * 0.5 + 0.5))[..., None] + 255 * crest[..., None] * 0.35
    save('rainbow_surf', vignette(calm_centre(img * 0.6, 0.75), 0.45))


R6 = (motion, mirror_swirl, iris, tunnel, prism, bronze, flower_of_life, gold_filament, violet_neural, cosmic_swirl,
      dream_clouds, ink_bloom, glass_world, copper_maze, tessellate, violet_ridges, snow_globes, mandelbrot, julia,
      coral_cells, lotus, neon_marble, melt, third_eye, rainbow_surf)

# ======== ROUND 6b: 25 more from the same 50 picks ========
def _bg(seed, c0=(6, 4, 12), c1=(18, 8, 28)):
    return ramp(noise(seed, 4, 2), [(0, c0), (1, c1)])


def _line_glow(segs, cols, seed, width=2, blur=(2, 8, 18), scale=0.55):
    def draw(d, rng):
        for i, pts in enumerate(segs):
            d.line(pts, fill=tuple(int(v) for v in cols[i % len(cols)]), width=width)
    return glow_layer(draw, blur=blur, seed=seed) * scale


# 45 HYPERCUBE (#77): tesseracts turning in 4D, projected as neon wireframes round the edges
def hypercube():
    rng = np.random.default_rng(451)
    v = np.array([[(i >> k) & 1 for k in range(4)] for i in range(16)], float) * 2 - 1
    edges = [(a, b) for a in range(16) for b in range(a + 1, 16) if np.sum(np.abs(v[a] - v[b])) == 2]
    segs, cols = [], []
    for j, (cx, cy, sz) in enumerate([(260, 260, 170), (W - 280, 250, 190), (240, H - 260, 150), (W - 260, H - 250, 175), (W / 2, 120, 110), (W / 2, H - 110, 110)]):
        t1, t2 = rng.random() * 6, rng.random() * 6
        R = np.eye(4); c, s_ = np.cos(t1), np.sin(t1); R[[0, 0, 3, 3], [0, 3, 0, 3]] = [c, -s_, s_, c]
        Q = np.eye(4); c, s_ = np.cos(t2), np.sin(t2); Q[[1, 1, 2, 2], [1, 2, 1, 2]] = [c, -s_, s_, c]
        p = v @ R.T @ Q.T
        k = 2.6 / (3.2 - p[:, 3]); xy = p[:, :2] * k[:, None] * sz
        for a, b in edges:
            segs.append([(cx + xy[a, 0], cy + xy[a, 1]), (cx + xy[b, 0], cy + xy[b, 1])]); cols.append(NEON[j % 6])
    img = _line_glow(segs, cols, 452, 2) + _bg(453)
    save('hypercube', vignette(img, 0.4))


# 46 LAVA LAMP (#65, #90): soft metaballs of molten colour drifting up the sides
def lava():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    rng = np.random.default_rng(461); f = np.zeros((H, W)); hue = np.zeros((H, W))
    for i in range(34):
        cx = rng.choice([rng.uniform(0, W * 0.28), rng.uniform(W * 0.72, W)]); cy = rng.random() * H; r = rng.uniform(50, 150)
        g = r * r / ((x - cx) ** 2 + (y - cy) ** 2 + 1); f += g; hue += g * (i % 6)
    hue /= f + 1e-6
    m = np.clip((f - 1.0) / 0.6, 0, 1)
    col = np.stack([ramp(np.full(1, 0.5), [(0, NEON[k]), (1, NEON[k])])[0] for k in range(6)])
    c = col[np.clip(np.round(hue).astype(int), 0, 5)]
    img = c * (m * (0.25 + 0.4 * np.clip(f / 4, 0, 1)))[..., None] + c * ndimage.gaussian_filter(m, 20)[..., None] * 0.25
    img *= (0.55 + 0.45 * np.clip(1 - np.abs(np.sin(f * 2.5)) / 0.15, 0, 1))[..., None]
    save('lava', vignette(img + np.array([8, 4, 10]), 0.45))


# 47 OP ART (#97-#112): moire rings from two centres, black and white bands shifting into neon at the edges
def op_art():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    a = np.sin(np.hypot(x - W * 0.3, y - H * 0.5) / 9); b = np.sin(np.hypot(x - W * 0.7, y - H * 0.5) / 9)
    m = (a * b > 0).astype(float)
    edge = np.clip(np.hypot((x - W / 2) / (W / 2), (y - H / 2) / (H / 2)) - 0.3, 0, 1)
    col = neon_ramp(x / W + y / H * 0.5)
    img = (m[..., None] * (col * edge[..., None] + 120 * (1 - edge[..., None]))) * 0.55
    save('op_art', vignette(calm_centre(img, 0.75), 0.4))


# 48 NEBULA EYE (#76, #86): a ring nebula, gas glowing round a dark heart, stars behind
def nebula_eye():
    x, y, r, a = polar()
    n = noise(481, 8, 3, 0.6)
    ring = np.exp(-((r / 520 - 1 + (n - 0.5) * 0.5) / 0.18) ** 2)
    col = ramp(np.clip(r / 900 + n * 0.3, 0, 1), [(0, (40, 220, 255)), (0.5, (190, 60, 255)), (0.8, (255, 90, 60)), (1, (255, 200, 120))])
    img = col * (ring * (0.4 + n))[..., None] * 0.9
    img = stars(img, 482, 2200)
    save('nebula_eye', vignette(img, 0.35))


# 49 AURORA MANDALA (#86, #135): curtains of aurora light turning round a dark centre
def aurora_mandala():
    x, y, r, a = polar()
    n = noise(491, 5, 2, 0.5)
    rays = np.abs(np.sin(a * 18 + n * 6)) ** 6 * np.exp(-((r - 560) / 300) ** 2)
    col = ramp(np.clip(r / 900, 0, 1), [(0, (40, 255, 160)), (0.5, (60, 180, 255)), (1, (220, 80, 255))])
    img = col * (rays * 0.9 + ndimage.gaussian_filter(rays, 8) * 0.8)[..., None] + np.array([4, 6, 14])
    save('aurora_mandala', vignette(img, 0.4))


# 50 CRYSTAL CAVE (#119, #121): crystal points growing in from the top and bottom, lit from inside
def crystal_cave():
    rng = np.random.default_rng(501)
    im = Image.new('RGB', (W, H)); d = ImageDraw.Draw(im)
    for i in range(160):
        top = i % 2 == 0; bx = rng.random() * W; w_ = rng.uniform(18, 70); ln = rng.uniform(120, 420)
        by = 0 if top else H; tip = by + (ln if top else -ln)
        c = np.array(NEON[int(rng.integers(0, 6))]) * rng.uniform(0.3, 0.8)
        d.polygon([(bx - w_ / 2, by), (bx + w_ / 2, by), (bx + rng.normal() * 10, tip)], fill=tuple(int(v) for v in c))
        d.line([(bx, by), (bx + rng.normal() * 5, tip)], fill=tuple(int(min(255, v * 1.8)) for v in c), width=2)
    a = np.asarray(im).astype(float)
    img = a * 0.8 + ndimage.gaussian_filter(a, (14, 14, 0)) * 0.9 + np.array([6, 4, 12])
    save('crystal_cave', vignette(img, 0.4))


# 51 NEON FERNS (#191, #194): Barnsley ferns unfurling from the corners in glowing green and cyan
def ferns():
    rng = np.random.default_rng(511); acc = np.zeros((H, W))
    T = [(0, 0, 0, 0.16, 0, 0, 0.01), (0.85, 0.04, -0.04, 0.85, 0, 1.6, 0.85), (0.2, -0.26, 0.23, 0.22, 0, 1.6, 0.07), (-0.15, 0.28, 0.26, 0.24, 0, 0.44, 0.07)]
    pr = np.cumsum([t[6] for t in T])
    for (ox, oy, sc, rot, flip) in [(90, H, 150, 0.5, 1), (W - 90, H, 150, -0.5, -1), (140, 0, 120, 2.7, 1), (W - 140, 0, 120, -2.7, -1)]:
        p = np.zeros(2); pts = []
        for i in range(140000):
            t = T[int(np.searchsorted(pr, rng.random()))]
            p = np.array([t[0] * p[0] + t[1] * p[1] + t[4], t[2] * p[0] + t[3] * p[1] + t[5]]); pts.append(p.copy())
        P = np.array(pts) * sc; P[:, 0] *= flip
        c, s_ = np.cos(rot), np.sin(rot)
        X = ox + P[:, 0] * c - P[:, 1] * s_; Y = oy - (P[:, 0] * s_ + P[:, 1] * c)
        m = (X >= 0) & (X < W) & (Y >= 0) & (Y < H)
        np.add.at(acc, (Y[m].astype(int), X[m].astype(int)), 1)
    k = np.clip(acc / 3, 0, 1)
    col = ramp(k, [(0, (0, 0, 0)), (0.5, (40, 255, 140)), (1, (180, 255, 255))])
    img = col + ndimage.gaussian_filter(col, (6, 6, 0)) * 1.5 + np.array([2, 8, 10])
    save('ferns', vignette(img, 0.4))


# 52 LIGHTNING (#82): branching arcs of electricity crackling in from the edges
def lightning():
    segs, cols = [], []
    rng = np.random.default_rng(521)
    def bolt(x0, y0, a, ln, depth):
        if depth == 0: return
        pts = [(x0, y0)]
        for k in range(int(ln / 14)):
            a += rng.normal() * 0.35; x0 += np.cos(a) * 14; y0 += np.sin(a) * 14; pts.append((x0, y0))
            if rng.random() < 0.06: bolt(x0, y0, a + rng.choice([-0.7, 0.7]), ln * 0.5, depth - 1)
        segs.append(pts); cols.append((200, 220, 255) if depth > 2 else (140, 120, 255))
    for i in range(26):
        side = i % 4
        x0, y0, a = [(rng.random() * W, 0, np.pi / 2), (rng.random() * W, H, -np.pi / 2), (0, rng.random() * H, 0), (W, rng.random() * H, np.pi)][side]
        bolt(x0, y0, a, rng.uniform(250, 480), 4)
    img = _line_glow(segs, cols, 522, 2, (2, 6, 16, 40), 0.8) + np.array([4, 4, 14])
    save('lightning', vignette(img, 0.4))


# 53 DRAGON CURVE (#67-#75): Heighway dragons folding round the frame in neon
def dragon():
    def curve(n):
        s_ = [1]
        for i in range(n): s_ = s_ + [1] + [-v for v in s_[::-1]]
        return s_
    turns = curve(13); segs, cols = [], []
    for j, (x0, y0, a0, st) in enumerate([(300, 260, 0, 4.2), (W - 300, H - 260, np.pi, 4.2), (W - 340, 280, np.pi / 2, 3.6), (340, H - 280, -np.pi / 2, 3.6)]):
        x, y, a = x0, y0, a0; pts = [(x, y)]
        for t in turns:
            a += t * np.pi / 2; x += np.cos(a) * st; y += np.sin(a) * st; pts.append((x, y))
        for k in range(0, len(pts) - 1, 400): segs.append(pts[k:k + 401]); cols.append(NEON[(j + k // 400) % 6])
    img = _line_glow(segs, cols, 531, 1, (1.5, 5, 14), 0.6) + _bg(532)
    save('dragon', vignette(calm_centre(img, 0.5), 0.4))


# 54 STAINED GLASS (#119, #121): leaded glass in jewel colours, the light coming through unevenly
def stained_glass():
    rng = np.random.default_rng(541)
    from scipy.spatial import cKDTree
    pts = rng.random((420, 2)) * [W, H]
    y, x = np.mgrid[0:H:2, 0:W:2].astype(float)
    dd, ii = cKDTree(pts).query(np.stack([x.ravel(), y.ravel()], 1), k=2)
    d0, d1, i0 = dd[:, 0].reshape(x.shape), dd[:, 1].reshape(x.shape), ii[:, 0].reshape(x.shape)
    lead = np.clip(1 - (d1 - d0) / 4, 0, 1)
    jewel = np.array([(160, 20, 40), (20, 60, 160), (200, 150, 20), (20, 120, 60), (110, 30, 140), (20, 140, 150)], float)
    c = jewel[i0 % 6] * (0.45 + 0.55 * noise(542, 4, 2, 0.5, x.shape[1], x.shape[0]))[..., None]
    c = c * (1 - lead[..., None]) + np.array([12, 10, 8]) * lead[..., None]
    img = np.asarray(Image.fromarray(np.clip(c, 0, 255).astype(np.uint8)).resize((W, H), Image.BILINEAR)).astype(float)
    save('stained_glass', vignette(calm_centre(img, 0.7), 0.45))


# 55 CHROME DRIP (#90, #96): liquid chrome ribbons reflecting a neon sky
def chrome():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    n = ndimage.gaussian_filter(noise(551, 5, 2, 0.5), 5)
    t = np.sin(x / 150 + n * 8) + np.sin(y / 110 + n * 6)
    gy, gx = np.gradient(t)
    refl = np.clip(0.5 + gy * 25, 0, 1)
    sky = ramp(refl, [(0, (10, 6, 30)), (0.4, (60, 60, 90)), (0.6, (230, 230, 240)), (0.75, (255, 60, 180)), (1, (40, 230, 255))])
    band = np.clip(1 - np.abs(np.sin(t * 1.6)) / 0.5, 0, 1) ** 0.7
    img = sky * band[..., None] * 0.7
    save('chrome', vignette(calm_centre(img, 0.7), 0.45))


# 56 METATRON (#135, #157): Metatron's cube, thirteen circles and every line between them, in gold
def metatron():
    segs, cols = [], []
    for (cx, cy, R) in [(W / 2, H / 2, 300)]:
        P = [(cx, cy)] + [(cx + R * np.cos(k * np.pi / 3 + np.pi / 6), cy + R * np.sin(k * np.pi / 3 + np.pi / 6)) for k in range(6)] + \
            [(cx + 2 * R * np.cos(k * np.pi / 3 + np.pi / 6), cy + 2 * R * np.sin(k * np.pi / 3 + np.pi / 6)) for k in range(6)]
        for i in range(13):
            for j in range(i + 1, 13): segs.append([P[i], P[j]]); cols.append((255, 196, 90))
        for (px, py) in P:
            segs.append([(px + R / 2 * np.cos(t), py + R / 2 * np.sin(t)) for t in np.linspace(0, 2 * np.pi, 80)]); cols.append((90, 230, 220))
    img = _line_glow(segs, cols, 561, 2, (2, 7, 20), 0.5) + np.array([6, 5, 8])
    x, y, r, a = polar(); img *= np.clip((r - 60) / 260, 0.15, 1)[..., None]
    save('metatron', vignette(img, 0.35))


# 57 PENROSE (#80, #81): Penrose rhombs, never repeating, edges lit in two neons
def penrose():
    gold = (1 + 5 ** 0.5) / 2
    tri = []
    for i in range(10):
        b = complex(np.cos((2 * i - 1) * np.pi / 10), np.sin((2 * i - 1) * np.pi / 10)) * 1300
        c = complex(np.cos((2 * i + 1) * np.pi / 10), np.sin((2 * i + 1) * np.pi / 10)) * 1300
        if i % 2 == 0: b, c = c, b
        tri.append((0, 0j, b, c))
    for g in range(7):
        nt = []
        for col, A, B, C in tri:
            if col == 0:
                P = A + (B - A) / gold; nt += [(0, C, P, B), (1, P, C, A)]
            else:
                Q = B + (A - B) / gold; R = B + (C - B) / gold; nt += [(1, R, C, A), (1, Q, R, B), (0, R, Q, A)]
        tri = nt
    im = Image.new('RGB', (W, H)); d = ImageDraw.Draw(im)
    for col, A, B, C in tri:
        pts = [(z.real + W / 2, z.imag + H / 2) for z in (A, B, C)]
        d.polygon(pts, fill=(40, 12, 60) if col else (10, 40, 60))
        d.line([pts[1], pts[0], pts[2]], fill=NEON[0] if col else NEON[1], width=1)
    a = np.asarray(im).astype(float)
    img = a * 1.0 + ndimage.gaussian_filter(a, (5, 5, 0)) * 1.0
    save('penrose', vignette(calm_centre(img, 0.6), 0.45))


# 58 GLOWING FOREST (#191): recursive trees of light growing up from the foot, bioluminescent buds
def glow_forest():
    rng = np.random.default_rng(581); segs, cols = [], []
    def tree(x, y, a, ln, depth):
        if depth == 0: return
        ex, ey = x + np.cos(a) * ln, y + np.sin(a) * ln
        segs.append([(x, y), (ex, ey)]); cols.append((40, 200 - depth * 10, 255) if depth < 4 else (20, 80, 120))
        for s_ in (-1, 1): tree(ex, ey, a + s_ * rng.uniform(0.25, 0.55), ln * rng.uniform(0.65, 0.8), depth - 1)
    for i in range(14):
        tree(rng.random() * W, H + 10, -np.pi / 2 + rng.normal() * 0.1, rng.uniform(70, 150), 9)
    img = _line_glow(segs, cols, 582, 1, (2, 6, 16), 0.5) + _bg(583, (2, 4, 10), (6, 14, 24))
    save('glow_forest', vignette(img, 0.4))


# 59 SOAP BUBBLES (#192): a drift of iridescent bubbles, thin-film colour round each rim
def bubbles():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = _bg(591, (4, 4, 8), (10, 8, 18)); rng = np.random.default_rng(592)
    for i in range(70):
        cx, cy, R = rng.random() * W, rng.random() * H, rng.uniform(25, 120)
        if ((cx - W / 2) / (W * 0.32)) ** 2 + ((cy - H / 2) / (H * 0.32)) ** 2 < 1: continue
        x0, x1, y0, y1 = int(max(cx - R - 2, 0)), int(min(cx + R + 2, W)), int(max(cy - R - 2, 0)), int(min(cy + R + 2, H))
        if x1 <= x0 or y1 <= y0: continue
        r = np.hypot(x[y0:y1, x0:x1] - cx, y[y0:y1, x0:x1] - cy) / R
        film = neon_ramp(r * 2.5 + (y[y0:y1, x0:x1] - cy) / R * 0.4 + i * 0.1, cols=(1, 4, 0, 5, 2, 1))
        k = (np.clip(1 - np.abs(r - 0.95) / 0.08, 0, 1) * 0.9 + np.clip(r, 0, 1) ** 6 * 0.4) * (r < 1.02)
        spec = np.exp(-((x[y0:y1, x0:x1] - cx + R * 0.4) ** 2 + (y[y0:y1, x0:x1] - cy + R * 0.4) ** 2) / (R * 0.12) ** 2)
        img[y0:y1, x0:x1] = img[y0:y1, x0:x1] + film * k[..., None] * 0.6 + 255 * spec[..., None] * 0.6
    save('bubbles', vignette(img, 0.4))


# 60 KALEIDO SHARDS (#67-#75): a six-fold kaleidoscope of torn neon texture, sharp mirror seams
def kaleido_shards():
    x, y, r, a = polar()
    seg = np.pi / 3; am = np.abs(np.mod(a, 2 * seg) - seg)
    u, v = r * np.cos(am), r * np.sin(am)
    src = ndimage.gaussian_filter(noise(601, 4, 2, 0.5), 3)
    sx = np.clip(np.mod(u, W) .astype(int), 0, W - 1); sy = np.clip(np.mod(v * 2, H).astype(int), 0, H - 1)
    t = src[sy, sx]
    shard = np.clip(1 - np.abs(np.sin(t * 12)) / 0.25, 0, 1)
    img = neon_ramp(t * 1.5 + r / 1200) * shard[..., None] * np.clip((r - 140) / 380, 0.06, 1)[..., None] * 0.8
    save('kaleido_shards', vignette(img, 0.4))


# 61 RIPPLES (#98): two stones dropped in neon water, their rings crossing into an interference pattern
def ripples():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    s_ = 0
    for (cx, cy) in [(W * 0.12, H * 0.2), (W * 0.88, H * 0.8), (W * 0.85, H * 0.15)]:
        d = np.hypot(x - cx, y - cy); s_ = s_ + np.sin(d / 14) / (1 + d / 900)
    k = np.clip(s_ / 2.2 + 0.5, 0, 1) ** 3
    img = neon_ramp(s_ * 0.1 + x / W * 0.4, cols=(1, 4, 0, 1)) * k[..., None] * 0.8
    save('ripples', vignette(calm_centre(img, 0.7), 0.4))


# 62 STRANGE ATTRACTOR (#77): a Clifford attractor, millions of points tracing a silk veil of light
def attractor():
    a_, b_, c_, d_ = -1.4, 1.6, 1.0, 0.7
    n = 2_400_000; xs = np.empty(n); ys = np.empty(n); x0 = y0 = 0.1
    xv = np.full(4000, 0.1) + np.random.default_rng(621).random(4000) * 0.01; yv = xv.copy()
    acc = np.zeros((H, W))
    xv = np.random.default_rng(621).random(20000) * 0.1; yv = xv.copy()
    for i in range(500):
        xv, yv = np.sin(a_ * yv) + c_ * np.cos(a_ * xv), np.sin(b_ * xv) + d_ * np.cos(b_ * yv)
        if i > 20:
            px = ((xv + 2.1) / 4.2 * H * 1.25 + (W - H * 1.25) / 2).astype(int); py = ((yv + 1.8) / 3.6 * H).astype(int)
            m = (px >= 0) & (px < W) & (py >= 0) & (py < H); np.add.at(acc, (py[m], px[m]), 1)
    k = np.log1p(acc) / np.log1p(acc.max())
    img = neon_ramp(k * 0.8 + 0.6, cols=(4, 0, 3, 5)) * k[..., None] ** 1.2 * 1.1
    img += ndimage.gaussian_filter(img, (4, 4, 0)) * 0.6
    save('attractor', vignette(calm_centre(img, 0.45), 0.4))


# 63 SPIROGRAPH (#135): looping hypotrochoids layered round the frame, like a neon drafting toy
def spirograph():
    segs, cols = [], []
    for j, (cx, cy, R, r, d) in enumerate([(260, 260, 200, 75, 110), (W - 260, 260, 210, 84, 120), (260, H - 260, 180, 72, 90), (W - 260, H - 260, 200, 88, 130), (W / 2, -60, 260, 104, 150), (W / 2, H + 60, 260, 91, 140)]):
        t = np.linspace(0, 2 * np.pi * r / np.gcd(R, r), 5000)
        X = cx + (R - r) * np.cos(t) + d * np.cos((R - r) / r * t); Y = cy + (R - r) * np.sin(t) - d * np.sin((R - r) / r * t)
        segs.append(list(zip(X, Y))); cols.append(NEON[j % 6])
    img = _line_glow(segs, cols, 631, 1, (1.5, 5, 14), 0.6) + _bg(632)
    save('spirograph', vignette(img, 0.4))


# 64 PLASMA (#65, #86): the old demo-scene plasma, sines folding into soft neon fields, dimmed in the middle
def plasma():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    v = np.sin(x / 110) + np.sin(y / 80) + np.sin((x + y) / 140) + np.sin(np.hypot(x - W * 0.7, y - H * 0.3) / 90)
    img = neon_ramp(v * 0.18) * (0.35 + 0.25 * np.sin(v * 3)[..., None])
    save('plasma', vignette(calm_centre(img, 0.8), 0.45))


# 65 VORTEX STREAMS (#76, #113): dense coloured streamlines pulled into whirlpools at the corners
def vortex_streams():
    y, x = np.mgrid[0:H // 4, 0:W // 4].astype(float) * 4
    ang = np.zeros(x.shape)
    for (cx, cy, s_) in [(0, 0, 1), (W, 0, -1), (0, H, -1), (W, H, 1), (W / 2, H / 2, 1)]:
        ang += s_ * np.exp(-np.hypot(x - cx, y - cy) / 700) * 3
    rng = np.random.default_rng(651); segs, cols = [], []
    for i in range(1400):
        px, py = rng.random() * W, rng.random() * H; pts = []
        for k in range(60):
            gx, gy = min(int(px / 4), W // 4 - 1), min(int(py / 4), H // 4 - 1)
            a = np.arctan2(py - H / 2, px - W / 2) + np.pi / 2 + ang[gy, gx]
            px += np.cos(a) * 5; py += np.sin(a) * 5
            if not (0 <= px < W and 0 <= py < H): break
            pts.append((px, py))
        if len(pts) > 3: segs.append(pts); cols.append(np.array(NEON[i % 6]) * 0.6)
    img = _line_glow(segs, cols, 652, 1, (1.5, 6), 0.55) + np.array([6, 4, 10])
    save('vortex_streams', vignette(calm_centre(img, 0.65), 0.4))


# 66 JELLYFISH (#195): translucent bells trailing glowing tentacles through deep water
def jellyfish():
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img = ramp(y / H, [(0, (2, 10, 26)), (1, (0, 2, 8))]); rng = np.random.default_rng(661); segs, cols = [], []
    for i in range(9):
        cx, cy, R = rng.choice([rng.uniform(80, W * 0.3), rng.uniform(W * 0.7, W - 80)]), rng.uniform(80, H * 0.7), rng.uniform(50, 110)
        c = np.array([(120, 220, 255), (255, 120, 220), (180, 140, 255)][i % 3], float)
        dx, dy = (x - cx) / R, (y - cy) / (R * 0.7)
        bell = (dx ** 2 + dy ** 2 < 1) & (dy < 0.3)
        rim = np.clip(1 - np.abs(np.hypot(dx, dy) - 1) / 0.06, 0, 1) * (dy < 0.3)
        img = img + c * (bell * 0.18 + rim * 0.6)[..., None]
        for t in range(10):
            tx = cx + (t / 9 - 0.5) * R * 1.6; pts = [(tx + np.sin(k / 6 + t) * 10, cy + R * 0.25 + k * 6) for k in range(int(rng.uniform(30, 70)))]
            segs.append(pts); cols.append(c * 0.6)
    img = img + _line_glow(segs, cols, 662, 1, (2, 6), 0.6)
    save('jellyfish', vignette(img, 0.4))


# 67 NEON LATTICE (#96): an isometric lattice of neon tubes stretching away, gaps of black between
def neon_lattice():
    segs, cols = [], []
    s_ = 70
    for i in range(-30, 60):
        segs.append([(i * s_, 0), (i * s_ - H * 1.732, H)]); cols.append(NEON[0])
        segs.append([(i * s_ - W, 0), (i * s_ - W + H * 1.732, H)]); cols.append(NEON[1])
    for j in range(0, H, int(s_ * 0.866)): segs.append([(0, j), (W, j)]); cols.append(NEON[4])
    img = _line_glow(segs, [np.array(c) * 0.8 for c in cols], 671, 2, (2, 8), 0.9)
    y, x = np.mgrid[0:H, 0:W].astype(float)
    img *= (0.25 + 0.75 * noise(672, 3, 2, 0.5))[..., None]
    save('neon_lattice', vignette(calm_centre(img, 0.75), 0.45))


# 68 SUNBURST (#87, #98): seventies rays in warm psychedelic colour, fanning from below
def sunburst():
    x, y, r, a = polar(W / 2, H * 1.15)
    rays = (np.floor((a + np.pi) / (np.pi / 24)) % 2)
    col = ramp(np.clip(r / 1500, 0, 1), [(0, (255, 210, 60)), (0.3, (255, 120, 30)), (0.55, (230, 40, 110)), (0.8, (120, 30, 160)), (1, (20, 10, 40))])
    img = col * (0.25 + 0.4 * rays)[..., None]
    save('sunburst', vignette(calm_centre(img, 0.7), 0.45))


# 69 NAUTILUS (#90, #115): a golden spiral of chambers, pearly walls glowing, curling in from a corner
def nautilus():
    x, y, r, a = polar(W * 0.62, H * 0.58)
    b = np.log((1 + 5 ** 0.5) / 2) / (np.pi / 2)
    u = (np.log(np.maximum(r, 1)) / b - a) / (2 * np.pi)          # which turn of the spiral this point sits on
    f = np.mod(u, 1)
    wall = np.clip(1 - np.abs(f - 0.5) / 0.05, 0, 1)
    turn = np.floor(u)
    sept = np.clip(1 - np.abs(np.sin(a * 5 + turn)) / 0.04, 0, 1) * (np.abs(f - 0.5) < 0.5) * 0.35
    shade = np.sin(f * np.pi) ** 2
    col = ramp(np.clip(r / 1100, 0, 1), [(0, (255, 240, 225)), (0.35, (250, 180, 130)), (0.7, (190, 90, 160)), (1, (70, 40, 130))])
    img = col * (wall * 0.9 + sept + shade * 0.12)[..., None] * np.clip(r / 120, 0, 1)[..., None]
    img += ndimage.gaussian_filter(img, (6, 6, 0)) * 0.5 + np.array([6, 4, 10])
    save('nautilus', vignette(calm_centre(img, 0.4), 0.4))


R6B = (hypercube, lava, op_art, nebula_eye, aurora_mandala, crystal_cave, ferns, lightning, dragon, stained_glass, chrome,
       metatron, penrose, glow_forest, bubbles, kaleido_shards, ripples, attractor, spirograph, plasma, vortex_streams,
       jellyfish, neon_lattice, sunburst, nautilus)


if __name__ == '__main__':
    import sys
    wanted = set(sys.argv[1:])
    for fn in (nebula, aurora, topo, synthwave, flow, crystal, scope, marble, circuit, spectrum) + PSY + R6 + R6B:
        if not wanted or fn.__name__ in wanted or ('psy' in wanted and fn in PSY) or ('r6' in wanted and fn in R6) or ('r6b' in wanted and fn in R6B):
            fn()
