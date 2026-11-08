"""Draws res/SynTHBloKRandroyd.svg (40 HP track instrument) and src/SynTHBloKRandroyd_layout.hpp.
Layout (all mm):
  left         TRACKS OLED (saved tracks, SAVE / CLEAR inside it), GENRE OLED, MIDI
  right top    main OLED with TRACK / PATCH tabs, action row under it (NEW TRACK, ROLL, MUTATE, PARK, UNDO)
  right bottom BPM knob (its ring printed 40-240), MUTATE / DENSITY / FX, RUN / STOP, IN and OUT jacks, CPU meter and SIZE
Same look as the other SynTHBloK panels: dark card, orange header line, knob tick scales, text as paths
(Rack's SVG renderer does not draw <text>).
    python3 tools/make_randroyd_panel.py"""
import math, os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LIB = '/usr/share/fonts/truetype/liberation'
if os.path.exists(LIB):
    BOLD = TTFont(os.path.join(LIB, 'LiberationSans-Bold.ttf')); REG = TTFont(os.path.join(LIB, 'LiberationSans-Regular.ttf'))
else:
    BOLD = TTFont(r'C:\Windows\Fonts\arialbd.ttf'); REG = TTFont(r'C:\Windows\Fonts\arial.ttf')

W, H = 203.2, 128.5                  # 40 HP
# ---- left: TRACKS OLED, GENRE, MIDI ----
LIST = (5.0, 11.0, 53.0, 86.0)
GENRE = (5.0, 100.0, 53.0, 11.0)
MIDI = (5.0, 114.0, 53.0, 10.0)
# ---- right ----
OLED = (63.0, 9.0, 137.7, 72.0)
ACT_Y, ACT_H, ACT_X0, ACT_PITCH, ACT_W = 84.0, 6.6, 63.0, 27.6, 26.8
BPM_X, BPM_Y, BPM_LO, BPM_HI = 75.0, 106.0, 40, 240   # the big tempo knob and its printed ring
KNOB_Y = 101.6
KNOB_X = [93.0, 105.0, 117.0]                          # small knobs
TR_Y, TR_H, TR_X, TR_W = 112.6, 5.8, [87.5, 104.6], 16.2   # RUN / STOP
IN_Y = 104.6
IN_X = [127.0, 137.0, 147.0, 157.0]
OUT_Y = 120.6
OUT_X = [132.0, 143.0, 154.0]
CPU_X, CPU_Y = 167.0, 99.4
SIZE_X, SIZE_Y, SIZE_W, SIZE_H, SIZE_DX, SIZE_DY = 167.0, 107.0, 15.5, 4.6, 16.3, 5.6

def text_path(s, x, y, size, f=BOLD, anchor='middle', fill='#ffffff', spacing=0.0):
    gs = f.getGlyphSet(); cmap = f.getBestCmap(); upm = f['head'].unitsPerEm
    scale = size / upm * 1.35
    hmtx = f['hmtx']
    widths = [hmtx[cmap[ord(c)]][0] * scale + spacing for c in s]
    total = sum(widths) - spacing
    x0 = x - total / 2 if anchor == 'middle' else (x - total if anchor == 'end' else x)
    pen = SVGPathPen(gs, ntos=lambda v: ('%.3f' % v).rstrip('0').rstrip('.'))
    cx = x0
    for c, w in zip(s, widths):
        gs[cmap[ord(c)]].draw(TransformPen(pen, (scale, 0, 0, -scale, cx, y)))
        cx += w
    return '<path fill="%s" d="%s"/>' % (fill, pen.getCommands())

def ticks(cx, cy, r0=5.7, rl=6.4, rs=6.1):
    out = []
    for i in range(11):
        a = math.radians(-135 + 27 * i)
        s, c = math.sin(a), -math.cos(a)
        end = i in (0, 10)
        r1 = rl if (end or i == 5) else rs
        out.append('<path d="M%.3f,%.3f L%.3f,%.3f" stroke="%s" stroke-width="0.28" stroke-linecap="round" fill="none"/>'
                   % (cx + s * r0, cy + c * r0, cx + s * r1, cy + c * r1, '#ff7a00' if end else '#6b4217'))
    return out

def bpm_ring(cx, cy):
    # every 10 BPM a tick, every 20 a longer one with its number; the lit arc is drawn live inside it
    out = []
    for v in range(BPM_LO, BPM_HI + 1, 10):
        a = math.radians(-135 + 270 * (v - BPM_LO) / (BPM_HI - BPM_LO))
        s, c = math.sin(a), -math.cos(a)
        major = v % 20 == 0
        out.append('<path d="M%.3f,%.3f L%.3f,%.3f" stroke="%s" stroke-width="%s" stroke-linecap="round" fill="none"/>'
                   % (cx + s * 8.3, cy + c * 8.3, cx + s * (9.2 if major else 8.8), cy + c * (9.2 if major else 8.8),
                      '#ff7a00' if v in (BPM_LO, BPM_HI) else ('#9a6a30' if major else '#6b4217'), '0.3' if major else '0.22'))
        if major:
            out.append(text_path(str(v), cx + s * 10.7, cy + c * 10.7 + 0.45, 0.82, fill='#d8d8d8'))
    return out

def frame(x, y, w, h):
    return '<rect x="%g" y="%g" width="%g" height="%g" rx="1.2" fill="#161616" stroke="#2c2c2c" stroke-width="0.25"/>' % (x, y, w, h)

def well(x, y, w, h):
    return '<rect x="%g" y="%g" width="%g" height="%g" rx="1.2" fill="#070605" stroke="#2c2c2c" stroke-width="0.25"/>' % (x - 0.8, y - 0.8, w + 1.6, h + 1.6)

def jack_ring(cx, cy):
    return '<circle cx="%.3f" cy="%.3f" r="4.3" fill="#0c0c0c" stroke="#2c2c2c" stroke-width="0.25"/>' % (cx, cy)

p = ['<?xml version="1.0" encoding="UTF-8"?>',
     '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
     '<rect x="0" y="0" width="%g" height="%g" fill="#111111"/>' % (W, H),
     '<defs><linearGradient id="fxg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff" stop-opacity="0.035"/>'
     '<stop offset="1" stop-color="#000000" stop-opacity="0.16"/></linearGradient></defs>',
     '<rect x="0" y="0" width="%g" height="%g" fill="url(#fxg)"/>' % (W, H),
     '<path fill="#080808" d="M0,0 H%g V7 H0 Z"/>' % W,
     '<path d="M0,7 H%g" stroke="#ff7a00" stroke-width="0.3" stroke-opacity="0.75" fill="none"/>' % W,
     text_path('SynTHBloK-R', 9.18, 3.5, 2.25, REG, anchor='start', fill='#ffffff'),
     text_path('RANDROYD RANDOMYZER', 9.18, 6.3, 1.92, BOLD, anchor='start', fill='#ff7a00')]

# frames
p.append(frame(2.5, 9.0, 58.0, 116.5))
p.append(frame(62.5, 93.0, 138.7, 32.5))
# wells
p.append(well(*LIST))
p.append(well(*OLED))
p.append(well(*GENRE))
# knobs
p += bpm_ring(BPM_X, BPM_Y)
p.append(text_path('BPM', BPM_X, BPM_Y + 11.3, 1.3, fill='#ff7a00'))
for x, lab in zip(KNOB_X, ['MUTATE', 'DENSITY', 'FX']):
    p += ticks(x, KNOB_Y, 4.0, 4.7, 4.45)
    p.append(text_path(lab, x, KNOB_Y + 6.9, 1.0, fill='#ffffff'))
# jacks
p.append(text_path('IN', IN_X[0] - 4.0, 96.8, 1.05, fill='#ff7a00', anchor='start'))
for x, lab in zip(IN_X, ['CLOCK', 'SCENE+', 'ROLL', 'MUT CV']):
    p.append(jack_ring(x, IN_Y))
    p.append(text_path(lab, x, IN_Y - 5.0, 0.95, fill='#ffffff'))
p.append('<rect x="%g" y="%g" width="41" height="14.4" rx="1.0" fill="#1a120a" stroke="#ff7a00" stroke-width="0.3" stroke-opacity="0.6"/>' % (IN_X[0] - 4.5, 110.2))
p.append(text_path('OUT', IN_X[0] - 3.0, 113.2, 1.05, fill='#ff7a00', anchor='start'))
for x, lab in zip(OUT_X, ['TRIG', 'SCENE', 'CPU']):
    p.append(jack_ring(x, OUT_Y))
    p.append(text_path(lab, x, OUT_Y - 4.9, 0.95, fill='#ffffff'))
# CPU / SIZE labels
p.append(text_path('CPU', CPU_X, CPU_Y, 1.2, fill='#cfcfcf', anchor='start'))
p.append(text_path('SIZE', SIZE_X, SIZE_Y + 2 * SIZE_DY + 3.4, 0.95, REG, fill='#8a8a8a', anchor='start'))
p.append(text_path('NoiZee ToyZ', W / 2, 127.6, 1.0, REG, fill='#6b4217'))
p.append('</svg>')
svg = '\n'.join(p) + '\n'
open(os.path.join(ROOT, 'res', 'SynTHBloKRandroyd.svg'), 'w', encoding='utf-8').write(svg)

def f(v): return '%.2ff' % v
def box(name, b): return 'static const float %s_X = %s, %s_Y = %s, %s_W = %s, %s_H = %s;' % (name, f(b[0]), name, f(b[1]), name, f(b[2]), name, f(b[3]))
hpp = ['// generated by tools/make_randroyd_panel.py (mm)', '#pragma once', 'namespace RDL {',
       'static const float PANEL_W = %s;' % f(W),
       box('LIST', LIST), box('GENRE', GENRE), box('MIDI', MIDI), box('OLED', OLED),
       'static const float ACT_Y = %s, ACT_H = %s, ACT_X0 = %s, ACT_PITCH = %s, ACT_W = %s;' % (f(ACT_Y), f(ACT_H), f(ACT_X0), f(ACT_PITCH), f(ACT_W)),
       'static const float BPM_X = %s, BPM_Y = %s, BPM_LO = %s, BPM_HI = %s;' % (f(BPM_X), f(BPM_Y), f(BPM_LO), f(BPM_HI)),
       'static const float KNOB_Y = %s;' % f(KNOB_Y), 'static const float KNOB_X[3] = {%s};' % ', '.join(map(f, KNOB_X)),
       'static const float TR_Y = %s, TR_H = %s, TR_W = %s;' % (f(TR_Y), f(TR_H), f(TR_W)),
       'static const float TR_X[2] = {%s};' % ', '.join(map(f, TR_X)),
       'static const float IN_Y = %s;' % f(IN_Y), 'static const float IN_X[4] = {%s};' % ', '.join(map(f, IN_X)),
       'static const float OUT_Y = %s;' % f(OUT_Y), 'static const float OUT_X[3] = {%s};' % ', '.join(map(f, OUT_X)),
       'static const float CPU_X = %s, CPU_Y = %s;' % (f(CPU_X), f(CPU_Y)),
       'static const float SIZE_X = %s, SIZE_Y = %s, SIZE_W = %s, SIZE_H = %s, SIZE_DX = %s, SIZE_DY = %s;' % tuple(map(f, (SIZE_X, SIZE_Y, SIZE_W, SIZE_H, SIZE_DX, SIZE_DY))),
       '}', '']
open(os.path.join(ROOT, 'src', 'SynTHBloKRandroyd_layout.hpp'), 'w', encoding='utf-8').write('\n'.join(hpp))
print('wrote res/SynTHBloKRandroyd.svg (%d bytes) and src/SynTHBloKRandroyd_layout.hpp' % len(svg))
