#!/usr/bin/env python3
"""BACKDROP display order, darkest to lightest (Nick, Nov 2026), NoiZee ToyZ builds first.
Patches save looks/colours/cases/... by number, so the arrays never move: this writes src/BackdropOrder.hpp, the order
the OLED and the menus SHOW them in. Brightness: colours from the source, art and bank pictures measured from res/.
Re-run after adding a theme, look, colour, case, material, ear or bank picture."""
import re, os, sys
from PIL import Image, ImageStat
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, 'src/SynTHBloKBackdrop.cpp'), encoding='utf-8').read()

def lum(r, g, b):
    f = lambda c: (c / 255.0) ** 2.2
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)
def img_lum(path):
    im = Image.open(path).convert('RGB'); im.thumbnail((96, 96))
    return lum(*ImageStat.Stat(im).mean)
def block(start, end='};'):
    i = src.index(start); return src[i:src.index(end, i)]

# colours
cols = [tuple(map(int, m)) for m in re.findall(r'rgb\((\d+), (\d+), (\d+)\)', block('static const NVGcolor kColor[kColorN]'))]
colL = [lum(*c) for c in cols]
# built-in cases: name + base colour
cases = re.findall(r'\{"([^"]+)", M_\w+, rgb\((\d+), (\d+), (\d+)\)', block('static std::vector<CaseStyle> builtInCases()', '\n    };'))
caseName = [c[0] for c in cases]; caseL = [lum(*map(int, c[1:])) for c in cases]
# panel materials and ears
pan = re.findall(r'\{"([^"]+)", (?:NULL|"[^"]*"), (\d+), (\d+), (\d+)\}', block('static const PanelMat kPanelMat[]'))
panL = [lum(*map(int, p[1:])) for p in pan]
ear = re.findall(r'\{"([^"]+)", (\d+), (\d+), (\d+), [\d.]+f, \w+\}', block('static const EarFin kEarFin[]'))
earL = [lum(*map(int, e[1:])) for e in ear]
PANEL_NOIZEE = int(re.search(r'kPanelNoizee = (\d+)', src).group(1)); EAR_NOIZEE = int(re.search(r'kEarNoizee = (\d+)', src).group(1))
# looks: the procedural ones by eye (0 plain, 18 gradient and 19 case gradient follow the colour / case), art measured
artFiles = re.findall(r'"([a-z_0-9]+)"', block('static const char* const kArtFile[kArtN]'))
LOOK_ART = 20
proc = {1: .05, 2: .03, 3: .30, 4: .01, 5: .06, 6: .04, 7: .05, 8: .04, 9: .06, 10: .14, 11: .20, 12: .05, 13: .38, 14: .22, 15: .10, 16: .18, 17: .22}
def look_lum(k, color=0, case=None):
    if k == 0: return colL[color]
    if k == 18: return colL[color] * 0.5
    if k == 19: return (caseL[case] if case is not None else 0.05) * 0.5
    if k >= LOOK_ART: return artL[k - LOOK_ART]
    return proc[k]
artL = [img_lum(os.path.join(ROOT, 'res/backdrops/%s.jpg' % f)) for f in artFiles]
lookN = LOOK_ART + len(artFiles)
# bank pictures
bankN = int(re.search(r'kBankN = (\d+)', src).group(1))
bankL = [img_lum(os.path.join(ROOT, 'res/bank/bank_%02d.jpg' % i)) for i in range(1, bankN + 1)]
# themes: background and case together; NoiZee ToyZ builds (NoiZee ears or plates, the SynTHBloK black rig) first
LOOKS = {'LOOK_GRADIENT': 18, 'LOOK_CASE_GRADIENT': 19}
SW = re.search(r'enum \{ SW_BLACK_WALNUT = 20,([^}]*)\}', src).group(1)
swNames = ['SW_BLACK_WALNUT'] + [s.strip() for s in SW.split(',') if s.strip()]
themes = []
for m in re.finditer(r'\{"([^"]+)", ([A-Z_]+(?: \+ \d+)?|\d+), (\d+), "([^"]+)", [\d.]+f(?:, (\w+), \w+, (\d+), \d+)?\}', block('static const Theme kThemes[]')):
    name, lk, color, cname, sideMat, earK = m.groups()
    if '+' in lk: a, b = lk.split('+'); k = LOOKS.get(a.strip(), LOOK_ART) + int(b)
    else: k = LOOKS[lk] if lk in LOOKS else int(lk)
    ci = caseName.index(cname) if cname in caseName else None
    cl = caseL[ci] if ci is not None else 0.1
    L = 0.6 * look_lum(k, int(color), ci) + 0.4 * cl
    noizee = (earK is not None and int(earK) >= EAR_NOIZEE) or name.startswith('SYNTHBLOK')
    themes.append((name, L, noizee))
def order(Ls, first=(), keep=()):
    idx = [i for i in range(len(Ls)) if i not in keep]
    head = [i for i in idx if i in first]; rest = [i for i in idx if i not in first]
    return list(keep) + sorted(head, key=lambda i: Ls[i]) + sorted(rest, key=lambda i: Ls[i])
oTheme = order([t[1] for t in themes], first={i for i, t in enumerate(themes) if t[2]})
oLook = order([look_lum(k) for k in range(lookN)])
oColor = order(colL)
oCase = order(caseL, keep=(0,))                                             # NO CASE stays first
oPanel = order(panL, first=set(range(PANEL_NOIZEE, len(pan))), keep=(0,))   # NONE, the NoiZee plates, then dark to light
oEar = order(earL, first=set(range(EAR_NOIZEE, len(ear))), keep=(0,))
oBank = [i + 1 for i in order(bankL)]
for nm, o, n in [('theme', oTheme, len(themes)), ('look', oLook, lookN), ('color', oColor, len(cols)), ('case', oCase, len(cases)),
                 ('panel', oPanel, len(pan)), ('ear', oEar, len(ear))]:
    assert sorted(o) == list(range(n)), nm
out = ['// Written by tools/make_backdrop_order.py: the order BACKDROP SHOWS its lists in, darkest to lightest,',
       '// NoiZee ToyZ builds first. The arrays themselves never move (patches save numbers). Do not edit by hand.', '#pragma once']
def arr(nm, o): out.append('static const int %s[] = {%s};' % (nm, ', '.join(map(str, o))))
arr('kOrderTheme', oTheme); arr('kOrderLook', oLook); arr('kOrderColor', oColor); arr('kOrderCase', oCase)
arr('kOrderPanel', oPanel); arr('kOrderEar', oEar); arr('kOrderBank', oBank)
out.append('static const int kOrderCaseN = %d;   // built-in cases; ones from cases.json follow in file order' % len(cases))
open(os.path.join(ROOT, 'src/BackdropOrder.hpp'), 'w').write('\n'.join(out) + '\n')
print('themes:', ', '.join(themes[i][0] for i in oTheme[:12]), '...', ', '.join(themes[i][0] for i in oTheme[-5:]))
print('cases:', ', '.join(caseName[i] for i in oCase[:8]), '...', ', '.join(caseName[i] for i in oCase[-4:]))
print('ears:', ', '.join(ear[i][0] for i in oEar)); print('panels:', ', '.join(pan[i][0] for i in oPanel[:9]))
print('looks first:', oLook[:10], 'bank first:', oBank[:10])
