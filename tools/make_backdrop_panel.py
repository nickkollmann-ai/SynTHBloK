"""Draws res/SynTHBloKBackdrop.svg: BACKDROP's 6 HP panel, in the SynTHBloK look (dark card, the black header with
its orange line, white maker name over the orange module name, NoiZee ToyZ at the foot). No texture.
Text as paths (Rack's SVG renderer does not draw <text>).
    python3 tools/make_backdrop_panel.py"""
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

LIB = '/usr/share/fonts/truetype/liberation'
if os.path.exists(LIB):
    BOLD = TTFont(os.path.join(LIB, 'LiberationSans-Bold.ttf')); REG = TTFont(os.path.join(LIB, 'LiberationSans-Regular.ttf'))
else:
    BOLD = TTFont(r'C:\Windows\Fonts\arialbd.ttf'); REG = TTFont(r'C:\Windows\Fonts\arial.ttf')

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

def frame(x, y, w, h):
    return '<rect x="%g" y="%g" width="%g" height="%g" rx="1.2" fill="#161616" stroke="#2c2c2c" stroke-width="0.25"/>' % (x, y, w, h)

def well(x, y, w, h):
    return '<rect x="%g" y="%g" width="%g" height="%g" rx="1.2" fill="#070605" stroke="#2c2c2c" stroke-width="0.25"/>' % (x - 0.8, y - 0.8, w + 1.6, h + 1.6)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
W, H = 30.48, 128.5        # 6 HP
LED_Y = 112.0              # the status light (drawn by the module: amber = dressing the rack)

p = ['<?xml version="1.0" encoding="UTF-8"?>',
     '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
     '<rect x="0" y="0" width="%g" height="%g" fill="#111111"/>' % (W, H),
     '<defs><linearGradient id="fxg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff" stop-opacity="0.035"/>'
     '<stop offset="1" stop-color="#000000" stop-opacity="0.16"/></linearGradient></defs>',
     '<rect x="0" y="0" width="%g" height="%g" fill="url(#fxg)"/>' % (W, H),
     '<path fill="#080808" d="M0,0 H%g V7 H0 Z"/>' % W,
     '<path d="M0,7 H%g" stroke="#ff7a00" stroke-width="0.3" stroke-opacity="0.75" fill="none"/>' % W,
     text_path('SynTHBloK-B', 2.4, 3.5, 2.25, REG, anchor='start', fill='#ffffff'),
     text_path('BACKDROP', 2.4, 6.3, 1.92, BOLD, anchor='start', fill='#ff7a00')]
# the card, with the name up its middle
p.append(frame(2.5, 9.0, W - 5.0, 116.5))
p.append(well(5.0, 12.0, W - 10.0, 86.0))
p.append('<g transform="translate(%g %g) rotate(-90)">' % (W / 2 + 2.6, 55.0))
p.append(text_path('BACKDROP', 0, 0, 5.6, BOLD, anchor='middle', fill='#ff7a00', spacing=0.9))
p.append('</g>')
# what it does, small, under the well
for i, line in enumerate(['BACKGROUND', 'CASE', 'TITLE']):
    p.append(text_path(line, W / 2, 101.6 + i * 2.2, 1.05, REG, fill='#cfcfcf'))
# the status light's ring and label
p.append('<circle cx="%g" cy="%g" r="2.6" fill="#0c0c0c" stroke="#2c2c2c" stroke-width="0.25"/>' % (W / 2, LED_Y))
p.append(text_path('ON THE RACK', W / 2, LED_Y + 5.2, 0.95, fill='#ff7a00'))
p.append(text_path('RIGHT-CLICK: SETTINGS', W / 2, 121.6, 0.85, REG, fill='#8a8a8a'))
p.append(text_path('NoiZee ToyZ', W / 2, 127.6, 1.0, REG, fill='#6b4217'))
p.append('</svg>')
open(os.path.join(ROOT, 'res', 'SynTHBloKBackdrop.svg'), 'w', encoding='utf-8').write('\n'.join(p) + '\n')
print('wrote res/SynTHBloKBackdrop.svg')
