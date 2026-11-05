"""Builds random SynTHBloK patches from scratch into patches/SB_*.vcv (and Rack's patches folder), for the
workbench's PATCHES page. Each patch is rolled from a seed (its number), so the same run makes the same patches.
The SynTHBloK series is the heart of every patch:
  - MTX COMPOSER is the clock (its clock, run and reset go to every module with those jacks)
  - a SynTHBloK mixer (MIX-12, MIX-8 or MIX-6) to Rack's Audio, sometimes through BloKMaster
  - a SynTHBloK drum part: MTX-DRUM, EUCLID playing KICK + SNARE/HAT, MTX-8 or MTX FORGE (random beats)
  - a SynTHBloK main voice: MTX QUAD or MTX-DUO (random lead and bass lines)
Around them: layers playing the main voice's notes (SynTHBloK POLY, VCV Fundamental, Plaits, Rings, Elements),
effect chains (mostly SynTHBloK: DUAL FILTER, MORPH FILTER, GRAIN-FORGE, CHARACTER DELAY, STEREO, MTX-ECHO;
sometimes Clouds, Plateau, Surge XT), a send/return reverb or echo, and modulation (Fundamental LFO / Random,
the series' own CV outs) into the effects' CV jacks.
MTX-DRUM, MTX QUAD, MTX-ECHO and MTX COMPOSER start from their state in tools/patches_base.json (your patterns).
Every cable is checked against the jacks on the module's panel (Rack drops a cable to a jack that isn't there).
    python3 tools/make_series_patches.py [count]
Jack names, panel positions and widths come from tools/portnames/portnames.exe --pos, cached in
tools/patch_parts.json (local only)."""
import copy, json, os, random, subprocess, sys
from make_patches import BASE, OUT, RACK_PATCHES, save_vcv, setp, HERE
import shutil

PLUGINS = os.path.join(os.environ['LOCALAPPDATA'], 'Rack2', 'plugins-win-x64')
EXE = os.path.join(HERE, 'portnames', 'portnames.exe')
PARTS = os.path.join(HERE, 'patch_parts.json')
ENV = dict(os.environ, PATH=';'.join([r'C:\msys64\mingw64\bin', r'C:\Program Files\VCV\Rack2Pro',
                                       r'C:\msys64\home\nickk\Rack-SDK', os.environ.get('PATH', '')]))
parts = json.load(open(PARTS, encoding='utf-8')) if os.path.exists(PARTS) else {}

def part(plugin, model):
    """names, on-panel jacks, width and plugin version of one model (portnames --pos, cached)"""
    k = plugin + '/' + model
    if k not in parts:
        r = subprocess.run([EXE, plugin, '--pos', model], capture_output=True, text=True, encoding='utf-8',
                           errors='replace', env=ENV, timeout=90)
        line = next((l for l in reversed(r.stdout.splitlines()) if l.startswith('{')), '{}')
        e = json.loads(line).get(model)
        if not e or not e.get('wmm'):
            parts[k] = None
        else:
            ver = json.load(open(os.path.join(PLUGINS, plugin, 'plugin.json'), encoding='utf-8'))['version']
            parts[k] = {'in': e['in'], 'out': e['out'], 'hp': round(e['wmm'] / 5.08), 'version': ver,
                        'inOk': [p is not None for p in e.get('inPos', [])], 'outOk': [p is not None for p in e.get('outPos', [])]}
        json.dump(parts, open(PARTS, 'w', encoding='utf-8'))
    return parts[k]

DONORS = {m['model']: m for m in BASE['modules'] if m['plugin'] == 'SynTHBloK'}
COLORS = ['#ff9300', '#ffb437', '#00b56e', '#3695ef', '#9437ff', '#ff2b2b', '#ffe14a', '#00e5ff', '#ff4fd8']
ROW_HP = 190

class Patch:
    def __init__(self, rng):
        self.rng, self.modules, self.cables, self.used = rng, [], [], set()
        self.x, self.row, self.names = 0, 0, []
    def add(self, plugin, model, params=None, data=None, newrow=False):
        info = part(plugin, model)
        if not info: return None
        if newrow and self.x: self.x, self.row = 0, self.row + 1
        if self.x + info['hp'] > ROW_HP: self.x, self.row = 0, self.row + 1
        m = {'id': self.rng.randrange(1 << 50, 1 << 52), 'plugin': plugin, 'model': model, 'version': info['version'],
             'params': [], 'pos': [self.x, self.row]}
        d = DONORS.get(model) if plugin == 'SynTHBloK' else None
        if d:   # your saved state of this module
            m['params'] = copy.deepcopy(d.get('params', []))
            if 'data' in d: m['data'] = copy.deepcopy(d['data'])
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

# --- random content for the sequencers ---
def beat(rng, rows=8, steps=8):
    """rows x steps of 0/1: kick, snare, hats, then sparse percussion"""
    pat = []
    for r in range(rows):
        if r == 0: s = rng.choice(['10001000', '10001010', '10100100', '10000010'])
        elif r == 1: s = rng.choice(['00001000', '00100010', '00001001'])
        elif r == 2: s = rng.choice(['10101010', '11111111', '01010101', '10111011'])
        else: s = ''.join('1' if rng.random() < 0.18 else '0' for _ in range(8))
        pat.append(s[:steps])
    return ''.join(pat)

def duo_data(rng):
    """MTX-DUO pattern 1: LEAD 8 steps of scale degrees (some chords), BASS on roots and fifths"""
    lead = []
    for _ in range(8):
        if rng.random() < 0.3: lead.append(0)
        else:
            m = 1 << rng.randrange(8)
            if rng.random() < 0.2: m |= 1 << rng.randrange(8)
            lead.append(m)
    bass = [rng.choice([1, 1, 1 << 4, 0, 1 << 2]) for _ in range(8)]
    chords = ''.join('%02x' % v for v in lead + bass)
    return {'chords': [chords] + ['00' * 16] * 7, 'accents': ['%02x00' % rng.randrange(256)] + ['0000'] * 7}

# --- the parts ---
SERIES_FX = [('SynTHBloKDualFilter', 'DualFilter'), ('NoizeeFlagshipFilter', 'Morph'), ('SynTHBloKGrainForge', 'GrainForge'),
             ('SynTHBloKFX', 'CharDelay'), ('SynTHBloKStereo', 'Stereo')]
OTHER_FX = [('AudibleInstruments', 'Clouds', 'Clouds'), ('Valley', 'Plateau', 'Plateau'), ('SurgeXTRack', 'SurgeXTFXChorus', 'SurgeChorus'),
            ('SurgeXTRack', 'SurgeXTFXPhaser', 'SurgePhaser'), ('SurgeXTRack', 'SurgeXTDelay', 'SurgeDelay')]
SENDS = [('SynTHBloK', 'SynTHBloKEcho', 'Echo'), ('Valley', 'Plateau', 'Plateau'), ('SurgeXTRack', 'SurgeXTFXReverb2', 'SurgeReverb'),
         ('Befaco', 'SpringReverb', 'Spring')]
# CV jacks worth moving on each effect (by name), for the modulation
MOD_TARGETS = {'SynTHBloKDualFilter': ['filter a cutoff', 'filter b cutoff'], 'NoizeeFlagshipFilter': ['morph cv', 'cutoff cv', 'width cv'],
               'SynTHBloKStereo': ['width', 'tilt', 'air'], 'SynTHBloKFX': ['time cv', 'fbk', 'tone'],
               'Clouds': ['position', 'size', 'texture'], 'Plateau': ['size', 'decay'], 'BlokMaster': ['saturation', 'stereo width']}

def io(P, m):
    """a module's main audio in (L, R) and out (L, R) jack ids, by name"""
    il = P.find(m, 'in', 'left', 'in l', 'main', 'audio', 'input 1')
    ir = P.find(m, 'in', 'right', 'in r', 'side')
    ol = P.find(m, 'out', 'left', 'out l', 'main', 'mix', 'audio left', 'chopp')
    orr = P.find(m, 'out', 'right', 'out r', 'auxiliary')
    return il, ir, ol, orr

def chain(P, song, src, fxs, mods):
    """src = [(module, out)] (mono or L, R) through each effect in fxs; returns the last [(module, out)]"""
    for plugin, model in fxs:
        m = P.add(plugin, model, fx_knobs(P.rng, model))
        if not m: continue
        il, ir, ol, orr = io(P, m)
        if model == 'SynTHBloKFX': il, ir = 9, 10
        if model == 'NoizeeFlagshipFilter': il, ir = 16, 17
        if il is None or ol is None: continue
        P.w(src[0], (m, il), '#ff9300')
        if len(src) > 1 and ir is not None: P.w(src[1], (m, ir), '#ff9300')
        clock(P, song, m)
        src = [(m, ol)] + ([(m, orr)] if orr is not None else [])
        P.names.append(dict(SERIES_FX + [(o[1], o[2]) for o in OTHER_FX]).get(model, model))
        mods.append(m)
    return src

def fx_knobs(rng, model):
    if model == 'SynTHBloKDualFilter':   # ids from src/SynTHBloKDualFilter.cpp (see tools/make_patches.py)
        return {0: rng.uniform(0.3, 0.6), 1: rng.uniform(0.2, 0.7), 3: rng.choice([0, 0, 1, 2]), 16: rng.choice([0, 1, 2]),
                4: rng.uniform(0.1, 0.3), 7: 3, 8: 0, 14: 1, 11: rng.uniform(0.2, 0.6), 12: rng.uniform(0.0, 0.3), 19: 0,
                30: rng.choice([0, 0, 0.25, 0.4])}
    return {}

def to_mixer(P, mixer, chans, src, label=None):
    """the next free stereo channel of the mixer"""
    while chans:
        l, r = chans.pop(0)
        if P.free(mixer, l):
            P.w(src[0], (mixer, l), '#00b56e')
            if len(src) > 1: P.w(src[1], (mixer, r), '#3695ef')
            return True
    return False

def drum_part(P, song, kind):
    """returns (audio sources [[(m, out)...]], trigger outs [(m, out)])"""
    rng = P.rng
    if kind == 'drum':
        d = P.add('SynTHBloK', 'SynTHBloKDrum'); clock(P, song, d); P.names.append('MtxDrum')
        return [[(d, 0), (d, 1)]], [(d, 2 + r) for r in range(8)]
    if kind == 'euclid':
        hits = {0: 16, 1: rng.choice([3, 4, 5]), 2: 0, 3: 16, 4: rng.choice([2, 3]), 5: 4, 6: 16, 7: rng.choice([5, 7, 8, 11]),
                8: rng.randrange(3), 9: 16, 10: rng.choice([1, 2, 3]), 11: rng.randrange(16)}
        e = P.add('SynTHBloK', 'SynTHBloKEuclid', hits); clock(P, song, e)
        k, sh = P.add('SynTHBloK', 'SynTHBloKKick'), P.add('SynTHBloK', 'SynTHBloKSnareHat')
        P.w((e, 0), (k, 0), '#ff2b2b'); P.w((e, 1), (sh, 0), '#ffb437'); P.w((e, 2), (sh, 1), '#ffe14a'); P.w((e, 3), (sh, 2), '#ffe14a')
        P.names.append('Euclid')
        return [[(k, 0)], [(sh, 2)]], [(e, i) for i in range(4)]
    model, name = {'mtx8': ('SynTHBloKMatrix8', 'Mtx8'), 'forge': ('SynTHBloKMatrixForge', 'Forge')}[kind]
    m = P.add('SynTHBloK', model, data={'patterns': [beat(rng)] + ['0' * 64] * 7}); clock(P, song, m)
    P.names.append(name)
    out = (17, 18) if kind == 'mtx8' else (0, 1)
    trig0 = 0 if kind == 'mtx8' else 2
    return [[(m, out[0]), (m, out[1])]], [(m, trig0 + i) for i in range(8)]

def main_voice(P, song, kind):
    """returns (audio [(m, out)...], pitch, gate, cv outs)"""
    if kind == 'quad':
        q = P.add('SynTHBloK', 'SynTHBloKMtxQuad'); clock(P, song, q); P.names.append('MtxQuad')
        return [(q, 7), (q, 8)], (q, 13), (q, 14), [(q, 15)]
    d = P.add('SynTHBloK', 'SynTHBloKDuo', data=duo_data(P.rng)); clock(P, song, d); P.names.append('Duo')
    lead = P.rng.random() < 0.6
    return [(d, 0), (d, 1)], (d, 4 if lead else 6), (d, 5 if lead else 7), [(d, 8 + i) for i in range(8)]

def layer(P, kind, pitch, gate):
    """another voice on the main voice's notes; returns its audio [(m, out)...]"""
    if kind == 'poly':
        m = P.add('SynTHBloK', 'SynTHBloKPoly'); P.w(pitch, (m, 0), '#ffb437'); P.w(gate, (m, 1), '#ffe14a'); P.names.append('Poly')
        return [(m, 0), (m, 1)]
    if kind == 'plaits':
        m = P.add('AudibleInstruments', 'Plaits', {0: P.rng.randrange(16)} if False else None)
        P.w(pitch, (m, 7), '#ffb437'); P.w(gate, (m, 5), '#ffe14a'); P.names.append('Plaits')
        return [(m, 0)]
    if kind == 'rings':
        m = P.add('AudibleInstruments', 'Rings'); P.w(pitch, (m, 6), '#ffb437'); P.w(gate, (m, 5), '#ffe14a'); P.names.append('Rings')
        return [(m, 0), (m, 1)]
    if kind == 'elements':
        m = P.add('AudibleInstruments', 'Elements'); P.w(pitch, (m, 0), '#ffb437'); P.w(gate, (m, 2), '#ffe14a'); P.names.append('Elements')
        return [(m, 0), (m, 1)]
    # VCV Fundamental: VCO -> VCF -> VCA with an ADSR, summed to mono
    rng = P.rng
    vco = P.add('Fundamental', 'VCO', {2: rng.choice([-12, 0, 0, 12])})
    vcf = P.add('Fundamental', 'VCF', {0: rng.uniform(0.3, 0.7), 2: rng.uniform(0.0, 0.5), 3: 0.4})
    adsr = P.add('Fundamental', 'ADSR', {0: rng.uniform(0, 0.3), 1: rng.uniform(0.2, 0.6), 2: rng.uniform(0.2, 0.7), 3: rng.uniform(0.2, 0.7)})
    vca, sm = P.add('Fundamental', 'VCA-1'), P.add('Fundamental', 'Sum')
    P.w(pitch, (vco, 0), '#ffb437'); P.w((vco, rng.choice([1, 2, 3])), (vcf, 3)); P.w(gate, (adsr, 4), '#ffe14a')
    P.w((adsr, 0), (vcf, 0), '#9437ff'); P.w((vcf, 0), (vca, 1)); P.w((adsr, 0), (vca, 0), '#9437ff'); P.w((vca, 0), (sm, 0))
    P.names.append('VCO')
    return [(sm, 0)]

def extra_drum(P, trigs):
    """another VCV drum on one of the drum part's triggers"""
    rng = P.rng
    plugin, model, name = rng.choice([('VCV-Drums', 'Clap', 'Clap'), ('VCV-Drums', 'ClosedHat', 'Hat'), ('VCV-Drums', 'Tom', 'Tom'),
                                      ('Befaco', 'Percall', 'Percall'), ('Befaco', 'Kickall', 'Kickall')])
    m = P.add(plugin, model)
    if not m: return None
    i = P.find(m, 'in', 'trig', 'gate', 'ch 1 trig', 'channel 1 trig')
    if i is None: return None
    P.w(rng.choice(trigs[1:4] or trigs), (m, i), '#ff2b2b')
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
        if pick < 0.4:
            l = P.add('Fundamental', 'LFO', {2: rng.uniform(-4.5, -1.0)}); src = (l, rng.choice([0, 1])); P.names.append('LFO')
        elif pick < 0.7:
            r = P.add('Fundamental', 'Random', {1: rng.choice([0, 0.5, 1])}); P.w((song, rng.choice([0, 3])), (r, 2), '#9437ff')
            src = (r, rng.choice([0, 2])); P.names.append('RND')
        elif cvs:
            src = rng.choice(cvs)
        else:
            src = (song, 4)
        P.w(src, (m, i), '#9437ff')

def build(n):
    rng = random.Random('SB%02d' % n)
    P = Patch(rng)
    song = P.add('SynTHBloK', 'SynTHBloKMatrixSong')
    drum_kind = rng.choice(['drum', 'drum', 'euclid', 'mtx8', 'forge'])
    drums, trigs = drum_part(P, song, drum_kind)
    voice_kind = rng.choice(['quad', 'duo'])
    vaudio, pitch, gate, cvs = main_voice(P, song, voice_kind)
    if drum_kind == 'drum': cvs += [(P.modules[1], 18 + i) for i in range(8)]   # MTX-DRUM's strip CVs
    mods, sources = [], []
    # the drum part, sometimes through a series effect
    for k, src in enumerate(drums):
        if k == 0 and rng.random() < 0.45:
            src = chain(P, song, src, [rng.choice([('SynTHBloK', 'SynTHBloKDualFilter'), ('SynTHBloK', 'SynTHBloKGrainForge'), ('SynTHBloK', 'SynTHBloKStereo')])], mods)
        sources.append(src)
    if rng.random() < 0.4:
        x = extra_drum(P, trigs)
        if x: sources.append(x)
    # the main voice through 0-2 series effects, now and then one of the others last
    fx = [('SynTHBloK' if f[0] != 'NoizeeFlagshipFilter' else 'SynTHBloK', f[0]) for f in rng.sample(SERIES_FX, rng.choice([0, 1, 1, 2]))]
    if rng.random() < 0.3: o = rng.choice(OTHER_FX); fx.append((o[0], o[1]))
    sources.append(chain(P, song, vaudio, fx, mods))
    # 1-2 layers on its notes, each maybe through an effect
    for kind in rng.sample(['poly', 'vco', 'plaits', 'rings', 'elements', 'poly', 'vco'], rng.choice([1, 1, 2])):
        src = layer(P, kind, pitch, gate)
        if rng.random() < 0.5:
            f = rng.choice(SERIES_FX + [(o[1], o[2]) for o in OTHER_FX[:2]])
            plugin = next((o[0] for o in OTHER_FX if o[1] == f[0]), 'SynTHBloK')
            src = chain(P, song, src, [(plugin, f[0])], mods)
        sources.append(src)
    # the mixer, the send/return, the master
    mix_model = rng.choice(['SynTHBloKMix12', 'SynTHBloKMix12', 'SynTHBloKMix8', 'SynTHBloKMix24'])
    mixer = P.add('SynTHBloK', mix_model, newrow=True)
    names = mixer['_info']['in']
    chans = [(i, i + 1) for i, nm in enumerate(names) if nm.startswith('CH ') and nm.endswith(' L')]
    for src in sources: to_mixer(P, mixer, chans, src)
    ret_l = P.find(mixer, 'in', 'fx return left')
    if rng.random() < 0.7 and ret_l is not None:
        plugin, model, name = rng.choice(SENDS)
        s = P.add(plugin, model)
        if s:
            il, ir, ol, orr = io(P, s)
            P.w((mixer, 2), (s, il)); P.w((mixer, 3), (s, ir)) if ir is not None else None
            clock(P, song, s)
            P.w((s, ol), (mixer, ret_l), '#00b56e')
            if orr is not None: P.w((s, orr), (mixer, ret_l + 1), '#3695ef')
            P.names.append(name + 'Send'); mods.append(s)
    last = [(mixer, 0), (mixer, 1)]
    if rng.random() < 0.45:
        bm = P.add('SynTHBloK', 'BlokMaster'); P.w(last[0], (bm, 0)); P.w(last[1], (bm, 1)); last = [(bm, 0), (bm, 1)]
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
    return 'SB_%02d_%s' % (n, '_'.join(label[:5])), P.json(audio)

if __name__ == '__main__':
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 50
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
