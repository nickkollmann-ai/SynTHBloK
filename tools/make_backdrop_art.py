"""Renders BACKDROP's ART backgrounds into res/backdrops/*.jpg (1920 x 1200, dark, so modules stay the star).

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


if __name__ == '__main__':
    for fn in (nebula, aurora, topo, synthwave, flow, crystal, scope, marble, circuit, spectrum):
        fn()
