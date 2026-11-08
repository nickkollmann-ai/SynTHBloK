"""Builds random SynTHBloK patches from scratch into patches/SB_*.vcv (and Rack's patches folder), for the
workbench's PATCHES page. Each patch is rolled from a seed (its number), so the same run makes the same patches.
The SynTHBloK series is the heart of every patch:
  - MTX COMPOSER is the clock (its clock, run and reset go to every module with those jacks)
  - a SynTHBloK mixer (MIX-12, MIX-8 or MIX-6) to Rack's Audio, sometimes through BloKMaster
  - a SynTHBloK drum part: MTX-DRUM, EUCLID playing KICK + SNARE/HAT, MTX-8 or MTX FORGE (random beats)
  - a SynTHBloK main voice: MTX QUAD or MTX-DUO (random lead and bass lines)
Around them, from the installed plugins (the series parts stay the most likely pick at each step):
  - layers playing the main voice's notes: SynTHBloK POLY, VCV Fundamental, Plaits, Rings, Elements, an oscillator
    (Bogaudio, Befaco EvenVCO, Braids, Vult Vraids) through Bogaudio's LPG, or oscillator -> filter (kocmoc LADR / SVF-1,
    Bogaudio VCF, Vult Stabile, Hora Juicy) -> VCA with a Bogaudio / Befaco / Fundamental envelope
  - effect chains: at least one SynTHBloK (DUAL FILTER, MORPH FILTER, GRAIN-FORGE, CHARACTER DELAY, STEREO), often
    one of Clouds, Plateau, Surge XT, VCV Pro chorus / flanger / phaser, ChowTape, ChowChorus, 4ms width / comp,
    AS delay, GP chorus, Fundamental Delay
  - 0-2 extra drums (VCV Drums, 4ms XOX, Befaco, Autodafe) on the drum part's trigger for their role
  - a send/return: MTX-ECHO, Plateau, Surge, Spring, VCV Pro Reverb, AS Reverb, ChowFDN, Fundamental Delay
  - modulation (Fundamental LFO / Random, Bogaudio LFO / Walk, the series' own CV outs) into the effects' CV jacks.
MTX-DRUM, MTX QUAD, MTX-ECHO and MTX COMPOSER start from their state in tools/patches_base.json (your patterns).
Every cable is checked against the jacks on the module's panel (Rack drops a cable to a jack that isn't there).
Each patch is in one style (tools/patch_genres.py: tempo, key, scale, chord progression, grooves, tones and
effect amounts), so it sounds musical as it loads; the style's name is in the file name.
    python3 tools/make_series_patches.py [count]
Jack names, panel positions and widths come from tools/portnames/portnames.exe --pos, cached in
tools/patch_parts.json (local only)."""
import copy, json, math, os, random, subprocess, sys
from make_patches import BASE, OUT, RACK_PATCHES, save_vcv, setp, HERE
import patch_genres as PG
from convert_slugs import SINGLES, RENAMED   # the split: which plugin each SynTHBloK module now lives in
import shutil

PLUGINS = os.path.join(os.environ['LOCALAPPDATA'], 'Rack2', 'plugins-win-x64')
EXE = os.path.join(HERE, 'portnames', 'portnames.exe')
PARTS = os.path.join(HERE, 'patch_parts.json')
ENV = dict(os.environ, PATH=';'.join([r'C:\msys64\mingw64\bin', r'C:\Program Files\VCV\Rack2Pro',
                                       r'C:\msys64\home\nickk\Rack-SDK', os.environ.get('PATH', '')]))
parts = json.load(open(PARTS, encoding='utf-8')) if os.path.exists(PARTS) else {}

# parts portnames can't read, written out from their source (src/SynTHBloKFilter.cpp for the Resonator)
_RES_IN = ['Cutoff knob CV', 'Resonance knob CV', 'Exciter knob CV', 'Mix knob CV', 'In L', 'In R', 'Tuning CV',
           'Resonance CV', 'V/Oct', 'Exciter CV', 'Damping CV', 'Strike CV']
_RES_OUT = ['Out L', 'Out R', 'Excitation env', 'Body tap']
KNOWN_PARTS = {'SynTHBloK-Resonator/Resonator': {'in': _RES_IN, 'out': _RES_OUT, 'hp': 13, 'version': '2.1.0',
                                                 'inOk': [True] * 12, 'outOk': [True] * 4}}
_rechecked = set()
def part(plugin, model):
    """names, on-panel jacks, width and plugin version of one model (portnames --pos, cached)"""
    k = plugin + '/' + model
    if k not in parts or (parts[k] is None and k not in _rechecked):   # a part missing last time may be installed now
        _rechecked.add(k)
        r = subprocess.run([EXE, plugin, '--pos', model], capture_output=True, text=True, encoding='utf-8',
                           errors='replace', env=ENV, timeout=90)
        line = next((l for l in reversed(r.stdout.splitlines()) if l.startswith('{')), '{}')
        e = json.loads(line).get(model)
        if not e or not e.get('wmm'):
            parts[k] = KNOWN_PARTS.get(k)   # portnames can't open every panel (it crashes on the Resonator's)
        else:
            ver = json.load(open(os.path.join(PLUGINS, plugin, 'plugin.json'), encoding='utf-8'))['version']
            parts[k] = {'in': e['in'], 'out': e['out'], 'hp': round(e['wmm'] / 5.08), 'version': ver,
                        'inOk': [p is not None for p in e.get('inPos', [])], 'outOk': [p is not None for p in e.get('outPos', [])]}
        json.dump(parts, open(PARTS, 'w', encoding='utf-8'))
    return parts[k]

# your saved module states (patches_base.json), under today's module slugs (the base predates the rename)
DONORS = {RENAMED.get(m['model'], m['model']): m for m in BASE['modules'] if m['plugin'] == 'SynTHBloK'}
COLORS = ['#ff9300', '#ffb437', '#00b56e', '#3695ef', '#9437ff', '#ff2b2b', '#ffe14a', '#00e5ff', '#ff4fd8']
ROW_HP = 190

class Patch:
    def __init__(self, rng):
        self.rng, self.modules, self.cables, self.used = rng, [], [], set()
        self.x, self.row, self.names = 0, 0, []
    def add(self, plugin, model, params=None, data=None, newrow=False):
        series = plugin == 'SynTHBloK'
        if series: plugin = SINGLES.get(model, 'SynTHBloK')   # a single lives in its own plugin since the split
        info = part(plugin, model)
        if not info: return None
        if newrow and self.x: self.x, self.row = 0, self.row + 1
        if self.x + info['hp'] > ROW_HP: self.x, self.row = 0, self.row + 1
        m = {'id': self.rng.randrange(1 << 50, 1 << 52), 'plugin': plugin, 'model': model, 'version': info['version'],
             'params': [], 'pos': [self.x, self.row]}
        d = DONORS.get(model) if series else None
        if d:   # your saved state of this module
            m['params'] = copy.deepcopy(d.get('params', []))
            if 'data' in d: m['data'] = copy.deepcopy(d['data'])
            if 'rows' in m.get('data', {}):   # COMPOSER rows point at the donor's modules: empty, so it takes this patch's
                m['data']['rows'] = [-1] * len(m['data']['rows'])
        if data is not None: m['data'] = data
        setp(m, params or {})
        self.x += info['hp']
        self.modules.append(m)
        m['_info'] = info
        return m
    def ok(self, m, side, i):
        if m is None or i is None: return False
        a = m['_info']['inOk' if side == 'in' else 'outOk']
        return 0 <= i < len(a) and a[i]
    def free(self, m, i):
        return (m['id'], i) not in self.used and self.ok(m, 'in', i)
    def w(self, a, b, color=None):
        """a cable: a = (module, output), b = (module, input); only jacks on the panel, one cable per input"""
        (ma, oa), (mb, ib) = a, b
        if not (ma and mb and self.ok(ma, 'out', oa) and self.free(mb, ib)): return False
        self.used.add((mb['id'], ib))
        self.cables.append({'id': self.rng.randrange(1 << 50, 1 << 52), 'outputModuleId': ma['id'], 'outputId': oa,
                            'inputModuleId': mb['id'], 'inputId': ib,
                            'color': color or COLORS[len(self.cables) % len(COLORS)]})
        return True
    def find(self, m, side, *words):
        """the first on-panel jack whose name starts with one of words (any case)"""
        names = m['_info'][side]
        for wd in words:
            for i, n in enumerate(names):
                if n.lower().startswith(wd.lower()) and self.ok(m, side, i) and (side == 'out' or self.free(m, i)): return i
        return None
    def json(self, audio):
        mods = []
        for m in self.modules:
            m = dict(m); m.pop('_info'); mods.append(m)
        return {'version': '2.6.6', 'zoom': 1.0, 'gridOffset': [-1.0, -1.0], 'modules': mods, 'cables': self.cables,
                'masterModuleId': audio['id']}

def clock(P, song, m):
    """MTX COMPOSER's clock, run and reset into m's own clock, run and reset jacks"""
    for side_words, out in ((('clock', 'clk', 'lfo sync'), 0), (('reset',), 2), (('run',), 1)):
        i = P.find(m, 'in', *side_words)
        if i is not None: P.w((song, out), (m, i), ['#9437ff', '#00b56e', '#3695ef'][[0, 1, 2].index(out) if out != 2 else 2])

# --- the parts ---
SERIES_FX = [('DualFilter', 'DualFilter'), ('NoizeeFlagshipFilter', 'Morph'), ('GrainForge', 'GrainForge'),
             ('FX', 'CharDelay'), ('Stereo', 'Stereo')]
OTHER_FX = [('AudibleInstruments', 'Clouds', 'Clouds'), ('Valley', 'Plateau', 'Plateau'), ('SurgeXTRack', 'SurgeXTFXChorus', 'SurgeChorus'),
            ('SurgeXTRack', 'SurgeXTFXPhaser', 'SurgePhaser'), ('SurgeXTRack', 'SurgeXTDelay', 'SurgeDelay'),
            ('VCV-Pro', 'Chorus', 'ProChorus'), ('VCV-Pro', 'Flanger', 'ProFlanger'), ('VCV-Pro', 'Phaser', 'ProPhaser'),
            ('ChowDSP', 'ChowTape', 'Tape'), ('ChowDSP', 'ChowChorus', 'ChowChorus'), ('4ms-ProducerPack', 'StereoWidth', 'Width'),
            ('4ms-ProducerPack', 'SeventiesComp', 'Comp70s'), ('AS', 'DelayPlusStereoFx', 'ASDelay'), ('GP', 'StereoChorus', 'GPChorus'),
            ('Fundamental', 'Delay', 'FDelay')]
SENDS = [('SynTHBloK', 'Echo', 'Echo'), ('SynTHBloK', 'Echo', 'Echo'), ('Valley', 'Plateau', 'Plateau'),
         ('SurgeXTRack', 'SurgeXTFXReverb2', 'SurgeReverb'), ('Befaco', 'SpringReverb', 'Spring'), ('VCV-Pro', 'Reverb', 'ProReverb'),
         ('AS', 'ReverbStereoFx', 'ASReverb'), ('ChowDSP', 'ChowFDN', 'FDN'), ('Fundamental', 'Delay', 'FDelay')]
# effects whose clock jack sets their time: not on the COMPOSER clock
NO_CLOCK = {'Delay'}
# CV jacks worth moving on each effect (by name), for the modulation
MOD_TARGETS = {'DualFilter': ['filter a cutoff', 'filter b cutoff'], 'NoizeeFlagshipFilter': ['morph cv', 'cutoff cv', 'width cv'],
               'Stereo': ['width', 'tilt', 'air'], 'FX': ['time cv', 'fbk', 'tone'],
               'Clouds': ['position', 'size', 'texture'], 'Plateau': ['size', 'decay'], 'BlokMaster': ['saturation', 'stereo width'],
               'Chorus': ['depth'], 'Phaser': ['frequency'], 'StereoWidth': ['width cv'], 'StereoChorus': ['cv depth']}

def io(P, m):
    """a module's main audio in (L, R) and out (L, R) jack ids, by name"""
    il = P.find(m, 'in', 'left', 'in l', 'main', 'audio', 'input 1')
    ir = P.find(m, 'in', 'right', 'in r', 'side', 'audio right')
    ol = P.find(m, 'out', 'left', 'out l', 'main', 'mix', 'audio left', 'chopp', 'audio')
    orr = P.find(m, 'out', 'right', 'out r', 'auxiliary', 'audio right')
    return il, ir, ol, orr

def chain(P, song, src, fxs, mods):
    """src = [(module, out)] (mono or L, R) through each effect in fxs; returns the last [(module, out)]"""
    for plugin, model in fxs:
        m = P.add(plugin, model, fx_knobs(P.rng, model, P.g))
        if not m: continue
        il, ir, ol, orr = io(P, m)
        if model == 'FX': il, ir = 9, 10
        if model == 'NoizeeFlagshipFilter': il, ir = 16, 17
        if il is None or ol is None: continue
        P.w(src[0], (m, il), '#ff9300')
        if ir is not None: P.w(src[1] if len(src) > 1 else src[0], (m, ir), '#ff9300')   # a mono source into both sides
        if model not in NO_CLOCK: clock(P, song, m)
        src = [(m, ol)] + ([(m, orr)] if orr is not None else [])
        P.names.append(dict(SERIES_FX + [(o[1], o[2]) for o in OTHER_FX]).get(model, model))
        mods.append(m)
    return src

def fx_knobs(rng, model, g):
    """an effect's knobs for the style: resonance, feedback and wet levels kept where they stay clean"""
    br, rev, dly, drv = g['bright'], g['rev'], g['dly'], g['drive']
    if model == 'DualFilter':   # ids from src/SynTHBloKDualFilter.cpp (see tools/make_patches.py)
        return {0: 0.3 + 0.35 * br + rng.uniform(-0.05, 0.05), 1: rng.uniform(0.15, 0.4), 3: rng.choice([0, 0, 1, 2]),
                16: rng.choice([0, 1, 2]), 4: 0.4 + 0.3 * br, 5: rng.uniform(0.1, 0.3), 7: 3, 8: 0, 14: 1,
                11: rng.uniform(0.2, 0.45), 12: rng.uniform(0.0, 0.25), 19: 0, 2: drv * 0.5, 6: 0.0,
                30: rng.choice([0, 0, 0.2, 0.3])}
    if model == 'NoizeeFlagshipFilter':
        return {0: 0.4 + 0.3 * br, 1: rng.uniform(0.1, 0.35), 2: rng.choice([0.0, 0.0, 0.25, 0.5]), 3: drv * 0.5,
                4: 0.0, 5: 0.0, 6: rng.choice([0.0, 0.05]), 7: 0.6, 8: rng.uniform(0.7, 1.0)}
    if model == 'GrainForge':
        return PG.grainforge(rng, g)
    if model == 'FX':   # character delay: tempo time, moderate feedback, a send-like mix
        return {0: rng.choice([0.25, 0.375, 0.5]), 1: 0.2 + 0.35 * dly, 2: 0.15 + 0.25 * dly, 3: 0.35 + 0.3 * br,
                4: rng.choice([0.0, 0.15]), 5: 0.2, 6: rng.choice([0.0, 0.3]), 7: 0.6}
    if model == 'Stereo':
        return {0: rng.uniform(1.1, 1.4), 1: (br - 0.5) * 0.3, 2: 0.1 + 0.2 * br, 3: 0.2, 4: rng.uniform(0.0, 0.3),
                5: 0.0, 6: 1.0, 7: 1.0}
    if model == 'Clouds':   # grains in key: pitch by octaves only
        return {3: rng.uniform(0.3, 0.7), 4: rng.uniform(0.35, 0.65), 5: rng.choice([0.0, 0.0, 1.0]), 6: 0.5,
                7: rng.uniform(0.4, 0.6), 8: rng.uniform(0.4, 0.7), 9: 0.25 + 0.3 * rev, 10: 0.5, 11: rng.uniform(0.0, 0.2),
                12: 0.2 + 0.4 * rev}
    if model == 'Plateau':   # as an insert: the dry signal stays
        return {0: 1.0, 1: 0.15 + 0.3 * rev, 2: 0.02, 5: 0.3 + 0.5 * rev, 7: 0.5 + 0.4 * rev, 8: 6.0 + 3 * br, 9: 2.0,
                10: 0.1, 12: 0.5}
    beat = 60.0 / g['bpm']
    if model == 'Chorus':    # VCV Pro: gentle, slow
        return {0: 0.02, 1: (br - 0.5) * 0.4, 2: rng.uniform(0.25, 0.5), 3: rng.uniform(-2.0, -0.8), 6: 0.3}
    if model == 'Flanger':
        return {1: rng.uniform(0.1, 0.35), 2: rng.uniform(0.3, 0.5), 3: rng.uniform(-2.5, -1.5)}
    if model == 'Phaser':
        return {1: rng.uniform(0.1, 0.35), 2: rng.uniform(0.3, 0.6), 3: rng.uniform(-2.5, -1.0), 4: 0.3}
    if model == 'ChowTape':   # its defaults: warm, not crushed
        return {}
    if model == 'ChowChorus':
        return {0: rng.uniform(0.2, 0.4), 1: rng.uniform(0.3, 0.5), 2: 0.0, 3: rng.uniform(0.25, 0.4)}
    if model == 'StereoWidth':
        return {0: rng.uniform(0.6, 0.8), 1: 0.0}
    if model == 'SeventiesComp':
        return {0: rng.uniform(0.25, 0.45), 1: 0.0, 2: 0.25, 4: 1.0}
    if model == 'DelayPlusStereoFx':   # dotted / straight eighths on the style's tempo
        return {0: beat * 0.75, 3: beat * 0.5, 1: 0.2 + 0.3 * dly, 4: 0.2 + 0.3 * dly, 2: 0.4 + 0.2 * br, 5: 0.4 + 0.2 * br,
                8: 0.15 + 0.2 * dly}
    if model == 'StereoChorus':
        return {0: rng.uniform(0.15, 0.35), 1: rng.uniform(20, 40), 3: 3, 4: rng.uniform(25, 40), 6: 30}
    if model == 'Delay':   # Fundamental: time knob = log10000(seconds / 1 ms)
        t = math.log(beat * rng.choice([0.5, 0.75, 1.0]) / 0.001) / math.log(10000)
        return {0: t, 1: 0.2 + 0.3 * dly, 2: 0.4 + 0.2 * br, 3: 0.15 + 0.2 * dly}
    return {}

def send_knobs(rng, model, g):
    """a send/return effect: wet only (the mixer keeps the dry)"""
    rev, dly, br = g['rev'], g['dly'], g['bright']
    if model == 'Plateau':
        return {0: 0.0, 1: 0.8, 2: 0.02, 5: 0.35 + 0.5 * rev, 7: 0.55 + 0.4 * rev, 8: 6.0 + 3 * br, 9: 2.0, 10: 0.1, 12: 0.5}
    if model == 'SpringReverb':
        return {0: 1.0, 3: 0.5}
    if model == 'Echo':   # wet repeats: kill dry, feedback by the style
        return {0: rng.choice([0, 0, 1]), 2: rng.choice([3, 4, 4, 5]), 3: 0.25 + 0.4 * dly, 4: 0.8, 5: 0.35 + 0.3 * br,
                6: rng.choice([0.0, 0.1]), 7: 0.3 * rev, 11: 1.0, 31: 1.0, 40: g['bpm']}
    if model == 'Reverb':   # VCV Pro: time in log2 seconds, filters in log2 Hz
        return {0: 0.3 + 1.5 * rev, 1: 0.5, 2: 7.0, 3: 12.0 + 1.5 * br, 4: -6.0, 5: 1.0, 6: 1.0}
    if model == 'ReverbStereoFx':
        return {0: 0.35 + 0.45 * rev, 1: 0.6 - 0.3 * br, 2: 1.0}
    if model == 'ChowFDN':
        return {0: 0.5, 1: 0.35 + 0.45 * rev, 5: 1.0}
    if model == 'Delay':
        t = math.log(60.0 / g['bpm'] * rng.choice([0.5, 0.75]) / 0.001) / math.log(10000)
        return {0: t, 1: 0.25 + 0.3 * dly, 2: 0.35 + 0.25 * br, 3: 1.0}
    return {}

MIX_SEND = {'Mix12': 96, 'Mix8': 64, 'Mix24': 58}
MIX_MASTER = {'Mix12': 108, 'Mix8': 72, 'Mix24': 48}
def to_mixer(P, mixer, chans, src, level=1.0, pan=0.0, send=0.0):
    """the next free stereo channel of the mixer, at this level, pan and FX send"""
    while chans:
        l, r = chans.pop(0)
        if P.free(mixer, l):
            P.w(src[0], (mixer, l), '#00b56e')
            if len(src) > 1: P.w(src[1], (mixer, r), '#3695ef')
            ch = int(mixer['_info']['in'][l].split()[1]) - 1
            setp(mixer, {ch * 8: level, ch * 8 + 1: pan, MIX_SEND[mixer['model']] + ch: send})
            return True
    return False

def drum_part(P, song, kind):
    """returns (audio sources [[(m, out)...]], trigger outs [(m, out)])"""
    rng, g = P.rng, P.g
    if kind == 'drum':   # the style's groove; the kick and snare rows tuned to the key, the FORGE FX only for glitch styles
        d = P.add('SynTHBloK', 'Drum', {17: 1 if g['glitch'] else 0, 21: 0.5, 108: 0.75, 109: 0.25 * g['dly'],
                                                 110: 0.35 * g['rev'], 94: 1.0, 95: 0.0, 96: 0.0, 106: 1.0,
                                                 35: 0.15 + 0.3 * g['drive'], 43: 0.1 + 0.3 * g['drive']})
        d['data']['patterns'] = PG.drum_patterns(rng, g)
        clock(P, song, d); P.names.append('MtxDrum')
        return [[(d, 0), (d, 1)]], [(d, 2 + r) for r in range(8)]
    if kind == 'euclid':   # per track: length, hits, rotation; kick 4 to the floor (or 3 / 5), backbeat, hats, offbeat open hat
        four = g['drums'][0].startswith('2000200')
        hits = {0: 16, 1: 4 if four else rng.choice([3, 5]), 2: 0, 3: 16, 4: 2, 5: 4,
                6: 16, 7: 16 if g['drums'][3] == '11111111' else 8, 8: 0, 9: 16, 10: 4 if four else rng.choice([0, 1]), 11: 2}
        e = P.add('SynTHBloK', 'Euclid', hits); clock(P, song, e)
        k = P.add('SynTHBloK', 'Kick', {0: PG.hz(g, 42, 62), 1: 0.5, 3: 0.4 + 0.2 * (g['bpm'] < 100), 6: 0.15 + 0.3 * g['drive'], 9: 0.8})
        sh = P.add('SynTHBloK', 'SnareHat', {0: PG.hz(g, 160, 260), 1: 0.55, 2: 0.4, 7: 0.1 + 0.3 * g['drive'], 9: 0.7, 10: 0.5})
        P.w((e, 0), (k, 0), '#ff2b2b'); P.w((e, 1), (sh, 0), '#ffb437'); P.w((e, 2), (sh, 1), '#ffe14a'); P.w((e, 3), (sh, 2), '#ffe14a')
        P.names.append('Euclid')
        return [[(k, 0)], [(sh, 2)]], [(e, i) for i in range(4)]
    model, name = {'mtx8': ('Matrix8', 'Mtx8'), 'forge': ('MatrixForge', 'Forge')}[kind]
    params = {15: 0.75, 17: 0.35 * g['rev'], 19: 0.0} if kind == 'mtx8' else {**PG.grainforge(rng, g), 91: 0.75, 93: 0.35 * g['rev'], 95: 0.0}
    m = P.add('SynTHBloK', model, params, data={'patterns': PG.voice_patterns(rng, g)}); clock(P, song, m)
    P.names.append(name)
    out = (17, 18) if kind == 'mtx8' else (0, 1)
    trig0 = 0 if kind == 'mtx8' else 2
    return [[(m, out[0]), (m, out[1])]], [(m, trig0 + i) for i in range(8)]

def main_voice(P, song, kind):
    """returns (audio [(m, out)...], pitch, gate, cv outs)"""
    rng, g = P.rng, P.g
    br, drv = g['bright'], g['drive']
    if kind == 'quad':   # its role in the style: bass, lead, chord stabs or an acid line
        role = rng.choice(g['quad'])
        P.quad_role = role
        cut = {'bass': 0.25, 'lead': 0.4, 'chords': 0.38, 'acid': 0.22}[role] + 0.15 * br
        q = P.add('SynTHBloK', 'MtxQuad', {
            39: g['key'], 40: g['scale'], 41: {'bass': -1, 'acid': -1, 'lead': 0, 'chords': 0}[role], 37: 8, 36: 1, 44: 1,
            9: cut, 10: {'acid': 0.5}.get(role, rng.uniform(0.1, 0.3)), 8: {'acid': 0.55, 'chords': 0.25}.get(role, 0.35),
            12: 0.25 if role == 'chords' and g['bpm'] < 100 else 0.02, 13: {'chords': 0.5, 'acid': 0.25}.get(role, 0.35),
            14: {'chords': 0.6, 'lead': 0.6}.get(role, 0.4), 15: {'chords': 0.6}.get(role, 0.25),
            16: {'acid': 0.35, 'lead': 0.1}.get(role, 0.0), 17: 0.3, 18: 0.6 if role != 'chords' else 0.45, 19: 0.3 if role == 'acid' else 0.2,
            20: 0.7, 25: 0.35 if role == 'bass' else 0.0, 26: 0.9, 27: 0.4 if role == 'acid' else 0.2, 34: 0.3 if role == 'chords' else 0.2,
            35: 0.2 + 0.4 * drv, 3: 0.0, 4: 0.0, 6: 0.0, 7: 0.3 if role == 'lead' else 0.0}, data=PG.quad_data(rng, g, role))
        clock(P, song, q); P.names.append('MtxQuad')
        return [(q, 7), (q, 8)], (q, 13), (q, 14), [(q, 15)]
    d = P.add('SynTHBloK', 'Duo', {
        4: g['key'], 5: g['scale'], 0: 1, 1: 8, 73: 1 if g['glitch'] else 0, 47: 0.75, 58: 0.5,
        12: 0.0, 15: 0.1 + 0.3 * drv, 16: 0.6, 17: 0.4 + 0.25 * br, 18: rng.uniform(0.1, 0.3), 19: 0.35,
        20: 0.1, 22: 0.4, 23: 0.5, 25: 0.55, 26: 0, 27: 0,
        31: 0.0, 34: 0.15 + 0.3 * drv, 35: 0.75, 36: 0.25 + 0.15 * br, 37: rng.uniform(0.1, 0.3), 38: 0.45, 44: 0.5, 45: 0, 46: 0,
        59: 1.0, 60: 0.0, 61: 0.0, 70: 1.0, 71: 1.0, 75: 0.2 * g['dly'], 76: 0.3 * g['rev']}, data=PG.duo_data(rng, g))
    clock(P, song, d); P.names.append('Duo')
    lead = rng.random() < 0.6
    return [(d, 0), (d, 1)], (d, 4 if lead else 6), (d, 5 if lead else 7), [(d, 8 + i) for i in range(8)]

def layer(P, kind, pitch, gate):
    """another voice on the main voice's notes, its tone set for the style; returns its audio [(m, out)...]
    (on a bass line it plays an octave up, so the two don't fight in the low end)"""
    rng, g = P.rng, P.g
    br, rev, slow = g['bright'], g['rev'], g['bpm'] < 95
    up = 1 if getattr(P, 'low_voice', False) else 0
    if kind == 'poly':   # pads for slow styles, plucks for fast ones
        m = P.add('SynTHBloK', 'Poly', {0: rng.choice([0.33, 0.66]), 1: 0, 2: 0.15 if slow else 0.01, 3: 0.5 if slow else 0.25,
                                                 4: 0.6 if slow else 0.3, 5: 0.6 if slow else 0.25, 6: (br - 0.5) * 0.8, 7: 0.4, 8: 0.7})
        P.w(pitch, (m, 0), '#ffb437'); P.w(gate, (m, 1), '#ffe14a'); P.names.append('Poly')
        return [(m, 0), (m, 1)]
    if kind == 'plaits':
        m = P.add('AudibleInstruments', 'Plaits', {2: up, 3: rng.uniform(0.3, 0.5), 4: 0.3 + 0.3 * br, 5: rng.uniform(0.3, 0.6),
                                                   9: 0.5, 10: 0.5 if slow else 0.35})
        P.w(pitch, (m, 7), '#ffb437'); P.w(gate, (m, 5), '#ffe14a'); P.names.append('Plaits')
        return [(m, 0)]
    if kind == 'rings':   # modal, low structure: harmonic, not clangy
        m = P.add('AudibleInstruments', 'Rings', {1: 0, 2: 30 + 12 * up, 3: rng.uniform(0.2, 0.3), 4: 0.35 + 0.25 * br, 5: 0.5, 6: 0.3})
        P.w(pitch, (m, 6), '#ffb437'); P.w(gate, (m, 5), '#ffe14a'); P.names.append('Rings')
        return [(m, 0), (m, 1)]
    if kind == 'elements':
        m = P.add('AudibleInstruments', 'Elements', {3: 0.3, 4: 12 * up, 9: rng.uniform(0.25, 0.4), 10: 0.35 + 0.25 * br,
                                                     16: 0.3 + 0.8 * rev})
        P.w(pitch, (m, 0), '#ffb437'); P.w(gate, (m, 2), '#ffe14a'); P.names.append('Elements')
        return [(m, 0), (m, 1)]
    if kind == 'lpg':   # an oscillator (Bogaudio, Befaco, Braids, Vraids) through Bogaudio's low-pass gate: plucks
        osc = rng.choice(['bog', 'even', 'braids', 'vraids'])
        if osc == 'bog': o, pin, out, name = P.add('Bogaudio', 'Bogaudio-VCO', {0: up}), 0, rng.choice([1, 2, 3]), 'BogVCO'
        elif osc == 'even': o, pin, out, name = P.add('Befaco', 'EvenVCO', {0: up}), 0, rng.choice([0, 3, 4]), 'EvenVCO'
        elif osc == 'braids':
            o, pin, out, name = P.add('AudibleInstruments', 'Braids', {1: -1 + up, 3: rng.uniform(0.3, 0.5), 5: 0.3 + 0.3 * br}), 1, 0, 'Braids'
            P.w(gate, (o, 0), '#ffe14a')
        else:
            o, pin, out, name = P.add('VultCompacts', 'Vraids', {3: rng.uniform(-0.3, 0.3), 7: rng.uniform(-0.3, 0.3)}), 3, 0, 'Vraids'
            P.w(gate, (o, 0), '#ffe14a')
        lpg = P.add('Bogaudio', 'Bogaudio-LPG', {0: 0.55 if slow else 0.35, 1: 0, 4: 0.6 + 0.3 * br, 5: 0.15 + 0.2 * br, 6: 1.0, 7: 0.0})
        P.w(pitch, (o, pin), '#ffb437'); P.w((o, out), (lpg, 4)); P.w(gate, (lpg, 3), '#ffe14a')
        P.names += [name, 'LPG']
        return [(lpg, 0)]
    if kind == 'sub':   # oscillator -> filter -> VCA, an envelope on both, from the installed brands
        oscs = [('Bogaudio', 'Bogaudio-VCO', {0: up}, 0, [0, 1], 'BogVCO'), ('Befaco', 'EvenVCO', {0: up}, 0, [3, 4], 'EvenVCO'),
                ('Fundamental', 'VCO', {2: 12 * up}, 0, [1, 2, 3], 'VCO')]
        filts = [('kocmoc', 'LADR', {0: 0.3 + 0.3 * br, 1: rng.uniform(0.1, 0.35), 2: 0.5, 3: 0, 4: 0.3, 5: 0.3}, 2, 1, 'LADR'),
                 ('kocmoc', 'SVF-1', {0: 0.3 + 0.3 * br, 1: rng.uniform(0.1, 0.35), 2: 0.5, 3: 0, 4: 0.3, 5: 0.3}, 2, 1, 'SVF1'),
                 ('Bogaudio', 'Bogaudio-VCF', {0: 0.25 + 0.2 * br, 1: 0.4, 3: rng.uniform(0.1, 0.3), 4: 0}, 3, 0, 'BogVCF'),
                 ('VultModulesFree', 'Stabile', {0: 0.35 + 0.25 * br, 1: rng.uniform(0.3, 1.0), 2: 0.4, 4: 0.5}, 0, 1, 'Stabile'),
                 ('Hora-VCO_VCF_VCA_Free', 'Juicy-VCF', {0: 0.35 + 0.25 * br, 1: rng.uniform(0.05, 0.3), 2: 0.5, 3: 0.4}, 2, 0, 'JuicyVCF')]
        envs = [('Bogaudio', 'Bogaudio-ADSR', {0: 0.4 if slow else 0.1, 1: 0.45, 2: 0.5, 3: 0.45}, 0),
                ('Befaco', 'ADSR', {0: 0, 3: 0.4 if slow else 0.1, 4: 0.4, 5: 0.5, 6: 0.4}, 0),
                ('Fundamental', 'ADSR', {0: 0.2 if slow else 0.02, 1: 0.4, 2: 0.5, 3: 0.4}, 4)]
        op, om, ok_, opin, oouts, oname = rng.choice(oscs)
        fp, fm, fk, fin, fcv, fname = rng.choice(filts)
        ep, em, ek, ein = rng.choice(envs)
        o, f, e, vca = P.add(op, om, ok_), P.add(fp, fm, fk), P.add(ep, em, ek), P.add('Fundamental', 'VCA-1')
        P.w(pitch, (o, opin), '#ffb437'); P.w((o, rng.choice(oouts)), (f, fin)); P.w(gate, (e, ein), '#ffe14a')
        P.w((e, 0), (f, fcv), '#9437ff'); P.w((f, 0), (vca, 1)); P.w((e, 0), (vca, 0), '#9437ff')
        P.names += [oname, fname]
        return [(vca, 0)]
    # VCV Fundamental: VCO -> VCF -> VCA with an ADSR, summed to mono
    vco = P.add('Fundamental', 'VCO', {2: 12 * up})
    vcf = P.add('Fundamental', 'VCF', {0: 0.35 + 0.3 * br, 2: rng.uniform(0.05, 0.3), 3: 0.35})
    adsr = P.add('Fundamental', 'ADSR', {0: 0.2 if slow else 0.02, 1: rng.uniform(0.3, 0.5), 2: rng.uniform(0.4, 0.6), 3: rng.uniform(0.3, 0.5)})
    vca, sm = P.add('Fundamental', 'VCA-1'), P.add('Fundamental', 'Sum')
    P.w(pitch, (vco, 0), '#ffb437'); P.w((vco, rng.choice([1, 2, 3])), (vcf, 3)); P.w(gate, (adsr, 4), '#ffe14a')
    P.w((adsr, 0), (vcf, 0), '#9437ff'); P.w((vcf, 0), (vca, 1)); P.w((adsr, 0), (vca, 0), '#9437ff'); P.w((vca, 0), (sm, 0))
    P.names.append('VCO')
    return [(sm, 0)]

def extra_drums(P):
    """the installed drum modules: (plugin, model, name, role, knobs), tuned to the style"""
    g = P.g; drv, rev = g['drive'], g['rev']
    return [('VCV-Drums', 'Clap', 'Clap', 'snare', {3: 0.4}), ('VCV-Drums', 'ClosedHat', 'Hat', 'hat', {0: 0.5, 3: 0.35}),
            ('VCV-Drums', 'Tom', 'Tom', 'perc', {0: 0.4, 3: 0.45}), ('VCV-Drums', 'Rim', 'Rim', 'perc', {0: 0.5, 3: 0.3}),
            ('VCV-Drums', 'OpenHat', 'OHat', 'open', {0: 0.5, 1: 0.4, 3: 0.4}), ('VCV-Drums', 'Ride', 'Ride', 'hat', {0: 0.5, 1: 0.4, 3: 0.5}),
            ('VCV-Drums', 'Snare', 'Snare', 'snare', {0: 0.5, 1: 0.5, 2: 0.5, 3: 0.4}),
            ('Befaco', 'Percall', 'Percall', 'perc', {0: 0.8, 4: 0.3, 5: 0.3, 6: 0.3, 7: 0.3}),
            ('Befaco', 'Kickall', 'Kickall', 'kick', {0: PG.hz(g, 42, 62), 3: 0.4, 4: 0.2, 5: 0.3}),
            ('4ms-XOXDrums', 'Snare', 'XOXSnare', 'snare', {0: PG.hz(g, 160, 320), 8: 20 + 30 * drv}),
            ('4ms-XOXDrums', 'HiHat', 'XOXHat', 'hat', {1: 120, 3: 10}),
            ('4ms-XOXDrums', 'Clap', 'XOXClap', 'snare', {4: 15 + 30 * rev, 5: 20 + 20 * drv}),
            ('4ms-XOXDrums', 'Rimshot', 'XOXRim', 'perc', {}), ('4ms-XOXDrums', 'Cowbell', 'Cowbell', 'perc', {0: PG.hz(g, 240, 480)}),
            ('4ms-XOXDrums', 'Tom', 'XOXTom', 'perc', {1: 50}), ('4ms-XOXDrums', 'Clave', 'Clave', 'perc', {}),
            ('4ms-XOXDrums', 'Maraca', 'Maraca', 'hat', {0: 6}),
            ('Autodafe-DrumKit', 'DrumsClaps', 'ADClap', 'snare', {}), ('Autodafe-DrumKit', 'DrumsHiHatClosed', 'ADHat', 'hat', {}),
            ('Autodafe-DrumKit', 'DrumsRimClaves', 'ADRim', 'perc', {})]

# the drum part's trigger for each role: MTX-DRUM rows KICK SNARE CLAP CHH OHH TOM METAL FM, EUCLID KICK SNARE CHH OHH
ROLE_TRIG = {'drum': {'kick': [0], 'snare': [1, 2], 'hat': [3], 'open': [4], 'perc': [5, 6, 7]},
             'euclid': {'kick': [0], 'snare': [1], 'hat': [2], 'open': [3], 'perc': [1, 2]}}

def extra_drum(P, trigs, pick):
    """another installed drum on the drum part's trigger for its role"""
    rng = P.rng
    plugin, model, name, role, knobs = pick
    m = P.add(plugin, model, knobs)
    if not m: return None
    i = P.find(m, 'in', 'trig', 'gate', 'closed hihat trigger', 'ch 1 trig', 'channel 1 trig', '#1')
    if i is None: return None
    rows = ROLE_TRIG.get(P.drum_kind, {}).get(role) or list(range(1, len(trigs)))
    P.w(trigs[rng.choice([r for r in rows if r < len(trigs)] or [0])], (m, i), '#ff2b2b')
    o = P.find(m, 'out', 'out', 'audio', 'ch 1', 'channel 1', 'left', 'main')
    if o is None: o = 0
    P.names.append(name)
    return [(m, o)]

def modulate(P, song, targets, cvs):
    """1-2 modulation cables into the effects' CV jacks: a Fundamental LFO or Random, or a series CV out"""
    rng = P.rng
    cands = []
    for m in targets:
        key = m['model'] if m['plugin'] == 'SynTHBloK' or m['model'] == 'BlokMaster' else m['model']
        for wd in MOD_TARGETS.get(key, []):
            i = P.find(m, 'in', wd)
            if i is not None: cands.append((m, i))
    rng.shuffle(cands)
    for m, i in cands[:rng.choice([1, 2])]:
        pick = rng.random()
        bars = rng.choice([2, 4, 8])
        if pick < 0.3:   # a slow sine / triangle, one cycle every 2, 4 or 8 bars of the style's tempo, around the knob
            l = P.add('Fundamental', 'LFO', {0: 0, 2: math.log2(P.g['bpm'] / 60.0 / (4 * bars))}); src = (l, rng.choice([0, 1])); P.names.append('LFO')
        elif pick < 0.45:   # Bogaudio's LFO, the same slow sine at a smaller depth
            l = P.add('Bogaudio', 'Bogaudio-LFO', {0: math.log2(P.g['bpm'] / 60.0 / (4 * bars)), 5: 0.6}); src = (l, 4); P.names.append('LFO')
        elif pick < 0.6:   # Bogaudio's random walk: a slow drift
            l = P.add('Bogaudio', 'Bogaudio-Walk', {0: rng.uniform(0.05, 0.2), 2: 0.4}); src = (l, 0); P.names.append('LFO')
        elif pick < 0.8:   # stepped (or smooth) random on each bar, a modest spread
            r = P.add('Fundamental', 'Random', {1: rng.choice([0, 0.5, 1]), 2: 0, 5: 0.35}); P.w((song, 3), (r, 2), '#9437ff')
            src = (r, rng.choice([0, 2])); P.names.append('RND')
        elif cvs:
            src = rng.choice(cvs)
        else:
            src = (song, 4)
        P.w(src, (m, i), '#9437ff')

def build_classic(n, seed=None, style=None, prefix='SB_%03d'):
    """the first PATCH GEN engine (random parts from every installed plugin); build() below is RANDROYD's"""
    rng = random.Random(seed if seed is not None else 'SB%02d' % n)
    P = Patch(rng)
    drum_kind = P.drum_kind = rng.choice(['drum', 'drum', 'euclid', 'mtx8', 'forge'])
    g = P.g = PG.pick(rng, n, drum_kind in ('mtx8', 'forge'), style)
    # MTX COMPOSER: the style's tempo; sections A-D play patterns 1-4 (the progression), 2 bars each, cycling
    song = P.add('SynTHBloK', 'MatrixSong', {0: 0, 5: g['bpm'], 8: 3, 9: 1, 10: 0})
    song['data']['cells'] = [[[s % 4, False, 0, 1.0] for r in range(8)] for s in range(8)]
    song['data']['section'] = 0
    drums, trigs = drum_part(P, song, drum_kind)
    voice_kind = rng.choice(['quad', 'duo'])
    vaudio, pitch, gate, cvs = main_voice(P, song, voice_kind)
    P.low_voice = pitch[1] == 6 if voice_kind == 'duo' else P.quad_role in ('bass', 'acid')
    if drum_kind == 'drum': cvs += [(P.modules[1], 18 + i) for i in range(8)]   # MTX-DRUM's strip CVs
    mods, sources = [], []   # sources: (audio, level, pan, send)
    rev = g['rev']
    # the drum part, often through a series effect
    for k, src in enumerate(drums):
        if k == 0 and rng.random() < 0.55:
            src = chain(P, song, src, [rng.choice([('SynTHBloK', 'DualFilter'), ('SynTHBloK', 'GrainForge'), ('SynTHBloK', 'Stereo')])], mods)
        sources.append((src, 0.95 if k == 0 else 0.75, 0.0, 0.1 * rev))
    # 0-2 of the installed drum modules on the drum part's triggers
    for pick in rng.sample(extra_drums(P), rng.choice([0, 1, 1, 2])):
        x = extra_drum(P, trigs, pick)
        if x: sources.append((x, 0.55, rng.choice([-0.3, 0.3]), 0.2 * rev))
    # the main voice through 1-2 series effects, often one of the others last
    fx = [('SynTHBloK', f[0]) for f in rng.sample(SERIES_FX, rng.choice([1, 1, 2]))]
    if rng.random() < 0.55: o = rng.choice(OTHER_FX); fx.append((o[0], o[1]))
    sources.append((chain(P, song, vaudio, fx, mods), 0.85, 0.0, 0.1 + 0.3 * rev))
    # 1-2 layers on its notes (SynTHBloK POLY most often), each maybe through an effect (a series one twice as often),
    # under the main voice and to the sides
    pans = [-0.35, 0.35]; rng.shuffle(pans)
    layer_fx = [('SynTHBloK', f[0]) for f in SERIES_FX] * 2 + [(o[0], o[1]) for o in OTHER_FX]
    for kind in rng.sample(['poly', 'poly', 'vco', 'plaits', 'rings', 'elements', 'lpg', 'sub', 'sub'], rng.choice([1, 2, 2])):
        src = layer(P, kind, pitch, gate)
        if rng.random() < 0.65: src = chain(P, song, src, [rng.choice(layer_fx)], mods)
        sources.append((src, 0.55, pans.pop() if pans else 0.0, 0.15 + 0.45 * rev))
    # the mixer, the send/return, the master
    mix_model = rng.choice(['Mix12', 'Mix12', 'Mix8', 'Mix24'])
    mixer = P.add('SynTHBloK', mix_model, newrow=True)
    mixer['params'] = []   # not the saved mixer's knobs: its solos and levels belong to another song
    setp(mixer, {MIX_MASTER[mix_model]: 0.9})
    names = mixer['_info']['in']
    chans = [(i, i + 1) for i, nm in enumerate(names) if nm.startswith('CH ') and nm.endswith(' L')]
    for src, lv, pan, snd in sources: to_mixer(P, mixer, chans, src, lv, pan, snd)
    ret_l = P.find(mixer, 'in', 'fx return left')
    if rng.random() < 0.85 and ret_l is not None:
        plugin, model, name = rng.choice(SENDS)
        s = P.add(plugin, model, send_knobs(rng, model, g))
        if s:
            il, ir, ol, orr = io(P, s)
            P.w((mixer, 2), (s, il)); P.w((mixer, 3), (s, ir)) if ir is not None else None
            if model not in NO_CLOCK: clock(P, song, s)
            P.w((s, ol), (mixer, ret_l), '#00b56e')
            P.w((s, orr if orr is not None else ol), (mixer, ret_l + 1), '#3695ef')   # a mono return to both sides
            P.names.append(name + 'Send'); mods.append(s)
    last = [(mixer, 0), (mixer, 1)]
    if rng.random() < 0.55:
        bm = P.add('SynTHBloK', 'BlokMaster', {1: 0.0, 8: 0.5, 16: -1.0, 20: 2.0 + 4.0 * g['drive'], 21: 0.5})
        P.w(last[0], (bm, 0)); P.w(last[1], (bm, 1)); last = [(bm, 0), (bm, 1)]
        P.names.append('BloKMaster'); mods.append(bm)
    audio = P.add('Core', 'AudioInterface2') if False else None
    audio = {'id': rng.randrange(1 << 50, 1 << 52), 'plugin': 'Core', 'model': 'AudioInterface2', 'version': '2.6.6',
             'params': [{'value': 1.0, 'id': 0}], 'pos': [P.x, P.row],
             '_info': {'in': ['To device 1', 'To device 2'], 'out': ['From device 1', 'From device 2'], 'hp': 5,
                       'inOk': [True, True], 'outOk': [True, True]}}
    P.modules.append(audio)
    P.w(last[0], (audio, 0), '#00b56e'); P.w(last[1], (audio, 1), '#3695ef')
    modulate(P, song, mods, cvs)
    # name: number, then the parts in the order they went in, without repeats
    seen, label = set(), []
    for nm in P.names:
        if nm not in seen and nm not in ('LFO', 'RND'): seen.add(nm); label.append(nm)
    style = g['name'].replace(' & ', 'n').replace(' ', '').replace('-', '')
    return '%s_%s_%s' % (prefix % n if '%' in prefix else prefix, style, '_'.join(label[:4])), P.json(audio)

# --- RANDROYD's song engine (src/SynTHBloKRandroyd.cpp), ported for PATCH GEN ---------------------------------
# The same light roster RANDROYD builds (few heavy MTX modules), its routing, its CPU sizes and its arrangement:
#   drums   MTX-DRUM (or EUCLID playing KICK + SNARE/HAT: the light drums, always on)
#   bass    MTX-DUO bass > MORPH FILTER (keytracked)          lead  the DUO's lead > DUAL FILTER (gated) > CHARACTER DELAY
#   chords  MTX QUAD > POLY > STEREO PHASER (ducked by the kick) > RESONATOR or DUAL FILTER
#   perc    EUCLID > KICK tuned up > RESONATOR (FULL)          pad   the QUAD's chords on a slow POLY > GRAIN-FORGE (FULL)
#   MIX-8, MTX-ECHO on the send, BloKMaster on the master.
# CPU sizes: LIGHT = drums + bass, MED = + lead + chords, FULL = + perc + pad.
# Arrangement: COMPOSER conducts the drums, the DUO and the QUAD; a part that sits a section out plays a blank
# pattern there (never a mute), so notes and tails ring out. Patterns 1-4 follow the chords, 8 is blank, and on
# the DUO 5-7 hold a chord with one half blank (only the lead or only the bass sits out).
RD_D, RD_B, RD_L, RD_C, RD_P, RD_X, RD_ALL = 1, 2, 4, 8, 16, 32, 63
RD_ARR = [
    [RD_D | RD_X, RD_D | RD_B | RD_X, RD_D | RD_B | RD_P | RD_X, RD_D | RD_B | RD_P | RD_C | RD_X, RD_ALL, RD_ALL, RD_ALL, RD_L | RD_C | RD_X],
    [RD_D | RD_P, RD_D | RD_P | RD_B, RD_D | RD_P | RD_B | RD_C, RD_D | RD_P | RD_B | RD_C | RD_L, RD_ALL, RD_ALL, RD_ALL & ~RD_L, RD_C | RD_X | RD_B],
    [RD_ALL & ~RD_L, RD_ALL, RD_ALL, RD_L | RD_X, RD_L | RD_X | RD_C, RD_D | RD_L | RD_X | RD_C, RD_ALL, RD_ALL],
    [RD_X, RD_X | RD_C, RD_X | RD_C | RD_P, RD_X | RD_C | RD_P | RD_B, RD_X | RD_C | RD_P | RD_B | RD_D, RD_ALL, RD_ALL, RD_ALL & ~RD_D],
]
RD_ARR_NAMES = ['BuildDrop', 'GrooveFirst', 'Breakdown', 'SlowBurn']
RD_SIZES = {'LIGHT': RD_D | RD_B, 'MED': RD_D | RD_B | RD_L | RD_C, 'FULL': RD_ALL}
# how often each size is dealt when none is asked for: most patches stay light enough for a busy PC
RD_SIZE_ODDS = [('LIGHT', 3), ('MED', 5), ('FULL', 2)]

def rd_arrangement(rng, present, always_on):
    """one of RANDROYD's four arrangements: per section A-H, a bit per part that plays"""
    k = rng.randrange(4)
    n_present = bin(present).count('1')
    out = []
    for i in range(8):
        v = RD_ARR[k][i]
        if v == RD_ALL and rng.random() < 0.15: v &= ~(RD_L if rng.random() < 0.5 else RD_P)   # a drop breathes now and then
        v |= always_on   # light parts have no blank pattern: they play every section
        for add in (0, 1, 3, 2, 4, 5):   # never a thin section: at least three parts (drums, bass, chords join first)
            if bin(v & present).count('1') >= min(3, n_present): break
            if present >> add & 1: v |= 1 << add
        out.append(v & present)
    return k, out

def rd_blank_half(hx, lead):
    """an MTX-DUO pattern (16 hex lead, 16 hex bass) with one half silent"""
    return ('0' * 16 + hx[16:]) if lead else (hx[:16] + '0' * 16)

def rd_port(P, m, side, *words):
    i = P.find(m, side, *words)
    return (m, i) if i is not None else None

def rd_wire(P, a, b, color=None):
    if a and b: P.w(a, b, color)

def build(n, seed=None, style=None, prefix='SB_%03d', size=None):
    rng = random.Random(seed if seed is not None else 'RD%02d' % n)
    P = Patch(rng)
    if size not in RD_SIZES:
        size = rng.choices([s for s, _ in RD_SIZE_ODDS], [w for _, w in RD_SIZE_ODDS])[0]
    present = RD_SIZES[size]
    light_drums = size == 'LIGHT' or rng.random() < 0.2   # a small track takes the light drums (no MTX sequencer)
    P.drum_kind = 'euclid' if light_drums else 'drum'
    g = P.g = PG.pick(rng, n, False, style)
    rev = g['rev']
    song = P.add('SynTHBloK', 'MatrixSong', {0: 0, 5: g['bpm'], 8: 3, 9: 1, 10: 0})
    P.names.append(size)

    # 1 DRUMS
    drums, trigs = drum_part(P, song, P.drum_kind)
    drum = next((m for m in P.modules if m['model'] == 'Drum'), None)
    kick = next((m for m in P.modules if m['model'] == 'Kick'), None)
    kick_audio = rd_port(P, drum, 'out', 'row 1 audio') if drum else ((kick, 0) if kick else None)

    # 2 + 3 MTX-DUO: bass > MORPH (keytracked); lead > DUAL (gated envelope) > CHARACTER DELAY
    main_voice(P, song, 'duo')
    duo = P.modules[-1]
    morph = P.add('SynTHBloK', 'NoizeeFlagshipFilter', fx_knobs(rng, 'NoizeeFlagshipFilter', g))
    rd_wire(P, rd_port(P, duo, 'out', 'bass audio'), rd_port(P, morph, 'in', 'left in'), '#ffb437')
    rd_wire(P, rd_port(P, duo, 'out', 'bass pitch'), rd_port(P, morph, 'in', 'keytrack'), '#ffb437')
    bass = [rd_port(P, morph, 'out', 'left out'), rd_port(P, morph, 'out', 'right out')]
    P.names.append('Duo')
    lead = None
    if present & RD_L:
        dual = P.add('SynTHBloK', 'DualFilter', fx_knobs(rng, 'DualFilter', g))
        dly = P.add('SynTHBloK', 'FX', fx_knobs(rng, 'FX', g))
        rd_wire(P, rd_port(P, duo, 'out', 'lead audio'), rd_port(P, dual, 'in', 'left'), '#ff9300')
        rd_wire(P, rd_port(P, duo, 'out', 'lead gate'), rd_port(P, dual, 'in', 'envelope gate'), '#ff9300')
        rd_wire(P, rd_port(P, dual, 'out', 'left'), rd_port(P, dly, 'in', 'in l'), '#ff9300')
        rd_wire(P, rd_port(P, dual, 'out', 'right'), rd_port(P, dly, 'in', 'in r'), '#ff9300')
        clock(P, song, dual); clock(P, song, dly)
        lead = [rd_port(P, dly, 'out', 'out l'), rd_port(P, dly, 'out', 'out r')]

    # 4 CHORDS: MTX QUAD > POLY > STEREO PHASER (side = the kick) > RESONATOR or DUAL
    quad = chords = pitch = gate = None
    if present & RD_C:
        roles = g['quad']; g['quad'] = ['chords']
        _, pitch, gate, _ = main_voice(P, song, 'quad')
        g['quad'] = roles
        quad = P.modules[-1]
        poly = layer(P, 'poly', pitch, gate)
        phs = P.add('SynTHBloK', 'Stereo', fx_knobs(rng, 'Stereo', g))
        rd_wire(P, poly[0], rd_port(P, phs, 'in', 'main'), '#ffe14a')
        rd_wire(P, kick_audio, rd_port(P, phs, 'in', 'side'), '#ff2b2b')
        cres = rng.random() < 0.5
        cfx = P.add('SynTHBloK', 'Resonator', {0: rng.uniform(0.35, 0.6), 1: rng.uniform(0.3, 0.5), 2: 0.3, 3: 0.5,
                                                4: rng.choice([0.0, 0.33, 0.66]), 5: 0.3 + 0.15 * g['bright']}) if cres else None
        if cfx is None: cfx = P.add('SynTHBloK', 'DualFilter', fx_knobs(rng, 'DualFilter', g))
        rd_wire(P, rd_port(P, phs, 'out', 'left'), rd_port(P, cfx, 'in', 'in l', 'left'), '#ffe14a')
        rd_wire(P, rd_port(P, phs, 'out', 'right'), rd_port(P, cfx, 'in', 'in r', 'right'), '#ffe14a')
        if cfx['model'] == 'DualFilter': clock(P, song, cfx)
        chords = [rd_port(P, cfx, 'out', 'out l', 'left'), rd_port(P, cfx, 'out', 'out r', 'right')]
        P.names += ['MtxQuad', 'Poly', 'Resonator' if cfx['model'] == 'Resonator' else 'DualFilter']

    # 5 PERC: EUCLID > KICK tuned up (a tom / clave) > RESONATOR
    perc = None
    if present & RD_P:
        e = P.add('SynTHBloK', 'Euclid', {0: 16, 1: rng.choice([3, 5, 7]), 2: rng.randrange(1, 4),
                                          3: 16, 4: 0, 5: 0, 6: 16, 7: 0, 8: 0, 9: 16, 10: 0, 11: 0})
        clock(P, song, e)
        pk = P.add('SynTHBloK', 'Kick', {0: PG.hz(g, 90, 160), 1: 0.2, 3: 0.2, 6: 0.1, 9: 0.7})
        pr = P.add('SynTHBloK', 'Resonator', {0: rng.uniform(0.45, 0.75), 1: rng.uniform(0.5, 0.7), 2: 0.45, 3: 0.4,
                                               4: rng.choice([0.33, 0.66, 1.0]), 5: 0.55})
        rd_wire(P, (e, 0), (pk, 0) if pk else None, '#ff2b2b')
        if pr: rd_wire(P, (pk, 0) if pk else None, rd_port(P, pr, 'in', 'in l'), '#ff2b2b')
        perc = [rd_port(P, pr, 'out', 'out l'), rd_port(P, pr, 'out', 'out r')] if pr else ([(pk, 0)] if pk else None)
        P.names.append('Perc')

    # 6 PAD: the QUAD's chords on a slow POLY > GRAIN-FORGE
    pad = None
    if present & RD_X and pitch and gate:
        pp = P.add('SynTHBloK', 'Poly', {0: 0.66, 1: 0, 2: 0.45, 3: 0.6, 4: 0.8, 5: 0.75, 6: (g['bright'] - 0.6) * 0.8, 7: 0.35, 8: 0.6})
        gf = P.add('SynTHBloK', 'GrainForge', PG.grainforge(rng, g))
        rd_wire(P, pitch, (pp, 0) if pp else None, '#9437ff'); rd_wire(P, gate, (pp, 1) if pp else None, '#9437ff')
        if gf:
            rd_wire(P, (pp, 0) if pp else None, rd_port(P, gf, 'in', 'left'), '#9437ff')
            clock(P, song, gf)
            pad = [rd_port(P, gf, 'out', 'left'), rd_port(P, gf, 'out', 'right')]
        elif pp: pad = [(pp, 0), (pp, 1)]
        P.names.append('Pad')

    # the arrangement: COMPOSER rows = the drums (MTX-DRUM), the DUO, the QUAD; blank patterns, never mutes
    always = (RD_D if drum is None else 0) | (RD_P if perc else 0)
    has = RD_D | RD_B | (RD_L if lead else 0) | (RD_C if chords else 0) | (RD_P if perc else 0) | (RD_X if pad else 0)
    kind, arr = rd_arrangement(rng, has, always)
    rows, cells = [-1] * 8, [[None] * 8 for _ in range(8)]
    duo_d = duo.get('data') or {}
    chords_hex, acc_hex = list(duo_d.get('chords', [])), list(duo_d.get('accents', []))
    slot, nxt = {}, 4
    for s in range(8):
        ci, v = s % 4, arr[s]
        if drum: cells[s][0] = ci if v & RD_D else 7
        b, l = bool(v & RD_B), bool(v & RD_L) and lead is not None
        if lead is None: dp = ci if b else 7   # LIGHT: the lead isn't patched, the bass follows B
        elif b and l: dp = ci
        elif not b and not l: dp = 7
        else:
            key = (ci, l)
            if key not in slot and nxt <= 6 and len(chords_hex) == 8:
                slot[key] = nxt
                chords_hex[nxt] = rd_blank_half(chords_hex[ci], lead=not l)
                if len(acc_hex) == 8: acc_hex[nxt] = ('00' + acc_hex[ci][2:]) if not l else (acc_hex[ci][:2] + '00')
                nxt += 1
            dp = slot.get(key, ci)
        cells[s][1] = dp
        if quad: cells[s][2] = ci if v & (RD_C | RD_X) else 7
    if len(chords_hex) == 8: duo_d['chords'] = chords_hex
    if len(acc_hex) == 8: duo_d['accents'] = acc_hex
    duo['data'] = duo_d
    rows[0] = drum['id'] if drum else -1
    rows[1] = duo['id']
    rows[2] = quad['id'] if quad else -1
    sd = song.setdefault('data', {})
    sd['rows'] = rows
    sd['cells'] = [[[cells[s][r] if cells[s][r] is not None else s % 4, False, 0, 1.0] for r in range(8)] for s in range(8)]
    sd['mutes'] = [False] * 8
    sd['autoFill'] = False
    sd['dropped'] = []
    sd['section'] = 0
    P.names.append(RD_ARR_NAMES[kind])

    # MIX-8: levels, pans and sends as RANDROYD sets them; MTX-ECHO on the send; BloKMaster on the master
    mixer = P.add('SynTHBloK', 'Mix8', newrow=True)
    mixer['params'] = []
    setp(mixer, {MIX_MASTER['Mix8']: 0.9})
    names = mixer['_info']['in']
    chans = [(i, i + 1) for i, nm in enumerate(names) if nm.startswith('CH ') and nm.endswith(' L')]
    def mix(src, lv, pan, snd):
        src = [x for x in (src or []) if x]
        if src: to_mixer(P, mixer, chans, src, lv, pan, snd)
    for k, d in enumerate(drums): mix(d, 0.95 if k == 0 else 0.75, 0.0, 0.08 * rev)
    mix(bass, 0.85, 0.0, 0.05)
    mix(lead, 0.7, -0.2, 0.1 + 0.3 * rev)
    mix(chords, 0.6, 0.25, 0.15 + 0.45 * rev)
    mix(perc, 0.55, rng.choice([-0.3, 0.3]), 0.2 + 0.3 * rev)
    mix(pad, 0.5, 0.0, 0.3 + 0.5 * rev)
    ret_l = P.find(mixer, 'in', 'fx return left')
    if ret_l is not None:
        s = P.add('SynTHBloK', 'Echo', send_knobs(rng, 'Echo', g))
        if s:
            P.w((mixer, 2), (s, 0)); P.w((mixer, 3), (s, 1)); clock(P, song, s)
            P.w((s, 0), (mixer, ret_l), '#00b56e'); P.w((s, 1), (mixer, ret_l + 1), '#3695ef')
    last = [(mixer, 0), (mixer, 1)]
    bm = P.add('SynTHBloK', 'BlokMaster', {1: 0.0, 8: 0.5, 16: -1.0, 20: 2.0 + 4.0 * g['drive'], 21: 0.5})
    if bm: P.w(last[0], (bm, 0)); P.w(last[1], (bm, 1)); last = [(bm, 0), (bm, 1)]
    audio = {'id': rng.randrange(1 << 50, 1 << 52), 'plugin': 'Core', 'model': 'AudioInterface2', 'version': '2.6.6',
             'params': [{'value': 1.0, 'id': 0}], 'pos': [P.x, P.row],
             '_info': {'in': ['To device 1', 'To device 2'], 'out': ['From device 1', 'From device 2'], 'hp': 5,
                       'inOk': [True, True], 'outOk': [True, True]}}
    P.modules.append(audio)
    P.w(last[0], (audio, 0), '#00b56e'); P.w(last[1], (audio, 1), '#3695ef')
    seen, label = set(), []
    for nm in P.names:
        if nm not in seen and nm not in ('LFO', 'RND'): seen.add(nm); label.append(nm)
    style_name = g['name'].replace(' & ', 'n').replace(' ', '').replace('-', '')
    return '%s_%s_%s' % (prefix % n if '%' in prefix else prefix, style_name, '_'.join(label[:5])), P.json(audio)

def gen_one(style=None):
    """the workbench's GEN button: one new patch from a fresh seed (in this style, or any), saved only into Rack's
    patches folder as GEN_<time>_..., so the PATCHES pop-up shows it; returns its file name"""
    import time
    seed = time.time_ns()
    n = random.Random(seed).randrange(1, len(PG.NAMES) + 1)
    name, p = build(n, seed, style if style in PG.G else None, 'GEN_' + time.strftime('%m%d_%H%M%S'))
    save_vcv(p, os.path.join(RACK_PATCHES, name + '.vcv'))
    return name + '.vcv'

if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--gen':   # --gen [style]: one new patch; prints {"file": ...}
        print(json.dumps({'file': gen_one(sys.argv[2] if len(sys.argv) > 2 else None)}))
        sys.exit(0)
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    os.makedirs(OUT, exist_ok=True)
    # this set replaces itself on every run
    for d in (OUT, RACK_PATCHES):
        for f in os.listdir(d):
            if f.startswith('SB_') and f.endswith('.vcv'): os.remove(os.path.join(d, f))
    for n in range(1, count + 1):
        name, p = build(n)
        path = os.path.join(OUT, name + '.vcv')
        save_vcv(p, path)
        shutil.copy2(path, os.path.join(RACK_PATCHES, name + '.vcv'))
        print('wrote', name, len(p['modules']), 'modules', len(p['cables']), 'cables')
