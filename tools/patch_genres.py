"""Genres for tools/make_series_patches.py: each patch is rolled in one style, and everything musical in it
follows that style so a patch sounds right the moment it loads:
  - tempo, key and scale (every sequencer in the patch shares the key and scale; MTX-8 / MTX FORGE voices are
    tuned to C minor pentatonic, so a patch with one of them stays in C with a scale that holds it)
  - a four-chord progression: MTX COMPOSER cycles sections A-D, and each sequencer's patterns 1-4 follow the
    chords (lines land on chord tones on the beat, step between them; bass on roots and fifths)
  - drum grooves for the style (pattern 4 is a fill), and the drum synths tuned to the key
  - synth tones and effect amounts in ranges that stay clean: resonance, feedback, drive and wet levels are
    capped, delays and reverbs sized to the style, glitch effects only in the styles that want them
  - mixer levels, pans and sends set for a balanced mix
Scales are the SynTHBloK list: 0 minor, 1 major, 2 dorian, 3 phrygian, 4 mixolydian, 5 harmonic minor,
6 minor pentatonic, 7 major pentatonic."""

SCALES = [[0, 2, 3, 5, 7, 8, 10], [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 9, 10], [0, 1, 3, 5, 7, 8, 10],
          [0, 2, 4, 5, 7, 9, 10], [0, 2, 3, 5, 7, 8, 11], [0, 3, 5, 7, 10], [0, 2, 4, 7, 9]]
KEYS = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
# MTX-8 / MTX FORGE voices: C minor pentatonic (C Eb F G Bb), so these scales on C agree with them
PENT_SAFE = [0, 2, 3, 6]

# Drum rows (MTX-DRUM's default kit): KICK, SNARE, CLAP, CLOSED HAT, OPEN HAT, TOM, METAL, FM. 8 sixteenths;
# '2' = accent. Fill: rows added on pattern 4.
G = {}
def genre(name, **kw): G[name] = dict(name=name, **kw)

genre('House', bpm=(122, 126), scales=[0, 2], keys=[9, 5, 7, 2], prog=[[0, 5, 2, 6], [0, 3, 6, 5]],
      drums=['20002000', '00000000', '00002000', '10101010', '00100010', '00000000', '00000000', '00000100'],
      fill={5: '00000110', 1: '00000001'}, lead='10010010', bass='00100010', quad=['chords', 'bass'],
      rev=0.35, dly=0.25, drive=0.15, bright=0.55, glitch=False)
genre('Deep House', bpm=(118, 122), scales=[2, 0], keys=[2, 0, 5], prog=[[0, 3, 0, 4], [0, 5, 3, 4]],
      drums=['20002000', '00000000', '00001000', '10111010', '00100010', '00000000', '00010010', '00000000'],
      fill={2: '00001011'}, lead='10001000', bass='10100010', quad=['chords'],
      rev=0.45, dly=0.3, drive=0.1, bright=0.4, glitch=False)
genre('Techno', bpm=(128, 134), scales=[3, 0], keys=[5, 4, 7], prog=[[0, 0, 1, 0], [0, 6, 0, 5]],
      drums=['20002000', '00001000', '00000000', '01110111', '00100010', '00000000', '10001000', '00010001'],
      fill={1: '00001011', 5: '00000100'}, lead='10100100', bass='01110111', quad=['bass', 'lead'],
      rev=0.3, dly=0.3, drive=0.3, bright=0.45, glitch=True)
genre('Acid', bpm=(124, 130), scales=[3, 0], keys=[9, 4, 2], prog=[[0, 0, 6, 0], [0, 1, 0, 6]],
      drums=['20002000', '00000000', '00002000', '01010101', '00100010', '00000000', '00000000', '00000000'],
      fill={2: '00002022'}, lead='11011011', bass='11011011', quad=['acid'],
      rev=0.25, dly=0.35, drive=0.3, bright=0.5, glitch=False)
genre('Trance', bpm=(136, 140), scales=[0, 5], keys=[9, 11, 7], prog=[[0, 5, 2, 6], [0, 5, 3, 4]],
      drums=['20002000', '00000000', '00002000', '10101010', '00100010', '00000000', '00000000', '00000000'],
      fill={1: '00011111'}, lead='11111111', bass='01110111', quad=['lead', 'chords'],
      rev=0.5, dly=0.4, drive=0.15, bright=0.6, glitch=False)
genre('Electro', bpm=(124, 130), scales=[3, 0], keys=[2, 4, 5], prog=[[0, 6, 5, 6], [0, 3, 6, 0]],
      drums=['20000010', '00002000', '00000000', '10111011', '00000000', '00000000', '00000000', '01000100'],
      fill={5: '00000111'}, lead='10010100', bass='10010010', quad=['bass', 'lead'],
      rev=0.25, dly=0.3, drive=0.3, bright=0.55, glitch=True)
genre('Hip-Hop', bpm=(86, 94), scales=[6, 0], keys=[1, 3, 0], prog=[[0, 5, 3, 4], [0, 3, 0, 5]],
      drums=['20000110', '00002000', '00000000', '10101010', '00000000', '00000000', '00000000', '00000000'],
      fill={1: '00002010'}, lead='10000100', bass='10000110', quad=['bass', 'chords'],
      rev=0.25, dly=0.2, drive=0.25, bright=0.4, glitch=False)
genre('Trap', bpm=(68, 72), scales=[5, 0], keys=[6, 8, 5], prog=[[0, 5, 3, 4], [0, 0, 5, 4]],
      drums=['20000010', '00002000', '00000000', '11111111', '00000000', '00000000', '00000000', '00000000'],
      fill={3: '11112222'}, lead='10010010', bass='20000010', quad=['bass', 'lead'],
      rev=0.35, dly=0.25, drive=0.35, bright=0.45, glitch=False)
genre('Lo-Fi', bpm=(74, 84), scales=[1, 2, 7], keys=[0, 5, 10], prog=[[0, 5, 3, 4], [1, 4, 0, 5]],
      drums=['20000100', '00002000', '00000000', '10101010', '00000000', '00000000', '00000000', '00000000'],
      fill={1: '00002001'}, lead='10001000', bass='10000100', quad=['chords'],
      rev=0.4, dly=0.2, drive=0.2, bright=0.3, glitch=False)
genre('Synthwave', bpm=(100, 110), scales=[0], keys=[9, 4, 2], prog=[[0, 5, 2, 6], [0, 3, 5, 4]],
      drums=['20000000', '00002000', '00000000', '10101010', '00000000', '00000000', '00000000', '00000000'],
      fill={5: '00001111'}, lead='10101010', bass='11111111', quad=['bass', 'lead'],
      rev=0.5, dly=0.35, drive=0.15, bright=0.55, glitch=False)
genre('Drum & Bass', bpm=(85, 88), scales=[0, 2], keys=[5, 7, 4], prog=[[0, 5, 6, 4], [0, 3, 5, 6]],
      drums=['20000100', '00200020', '00000000', '11111111', '00000000', '00000000', '00000000', '00000000'],
      fill={1: '00200222'}, lead='10000010', bass='10000100', quad=['bass'],
      rev=0.35, dly=0.3, drive=0.25, bright=0.5, glitch=True)
genre('Dub', bpm=(70, 78), scales=[0, 2], keys=[9, 4, 7], prog=[[0, 3, 0, 4], [0, 6, 5, 6]],
      drums=['00002000', '00002000', '00000000', '10101010', '00000000', '00000000', '00100010', '00000000'],
      fill={5: '00000011'}, lead='00100010', bass='10010100', quad=['chords', 'bass'],
      rev=0.5, dly=0.6, drive=0.15, bright=0.35, glitch=False)
genre('Ambient', bpm=(66, 76), scales=[7, 1], keys=[2, 7, 0], prog=[[0, 3, 4, 3], [0, 2, 3, 1]],
      drums=['20000000', '00000000', '00000000', '00100000', '00000000', '00000000', '00000010', '00010000'],
      fill={6: '00100010'}, lead='10000000', bass='10000000', quad=['chords'],
      rev=0.8, dly=0.45, drive=0.0, bright=0.35, glitch=False)
genre('Funk', bpm=(108, 116), scales=[4, 2], keys=[4, 9, 7], prog=[[0, 3, 0, 6], [0, 0, 3, 4]],
      drums=['20100010', '00002000', '00000000', '11111111', '00100000', '00000000', '00000000', '00000000'],
      fill={1: '00002121'}, lead='10110100', bass='10110010', quad=['bass', 'chords'],
      rev=0.25, dly=0.15, drive=0.2, bright=0.55, glitch=False)
genre('Afrobeat', bpm=(100, 108), scales=[4, 7], keys=[7, 0, 5], prog=[[0, 3, 0, 4], [0, 6, 3, 0]],
      drums=['20000100', '00001001', '00000000', '10110110', '00000000', '00100100', '10010010', '00000000'],
      fill={5: '00000111'}, lead='10101001', bass='10010100', quad=['bass', 'chords'],
      rev=0.3, dly=0.2, drive=0.15, bright=0.5, glitch=False)
genre('Trip-Hop', bpm=(80, 88), scales=[5, 0], keys=[0, 2, 7], prog=[[0, 5, 3, 4], [0, 3, 5, 4]],
      drums=['20000010', '00002000', '00000000', '10101010', '00000001', '00000000', '00000000', '00000000'],
      fill={1: '00002010'}, lead='10000100', bass='10000010', quad=['chords', 'bass'],
      rev=0.5, dly=0.35, drive=0.3, bright=0.35, glitch=True)
NAMES = list(G)

def pick(rng, n, pent_safe):
    """the genre for patch n (they take turns, so all 16 show up) and its key, scale, tempo and progression"""
    g = dict(G[NAMES[(n - 1) % len(NAMES)]])
    sc = rng.choice(g['scales'])
    key = rng.choice(g['keys'])
    if pent_safe:
        key = 0
        if sc not in PENT_SAFE: sc = 0
    g.update(scale=sc, key=key, bpm=rng.randrange(g['bpm'][0], g['bpm'][1] + 1), chords=rng.choice(g['prog']))
    return g

# --- harmony on the 8-row grids (row = scale degree 0..7 from the bottom) ---
def triad(g, root):
    """the chord on degree root, as grid degrees (0..7) in the scale: root, 3rd, 5th (pentatonics: root, +2, +3)"""
    n = len(SCALES[g['scale']])
    st = [0, 2, 4] if n == 7 else [0, 2, 3]
    out = []
    for s in st:
        d = root + s
        while d > 7: d -= n
        out.append(d)
    return out

def semis(g, d):
    sc = SCALES[g['scale']]
    return 12 * (d // len(sc)) + sc[d % len(sc)]

def lead_line(rng, g, root, rhythm=None, chordy=False):
    """8 steps of grid degree masks: chord tones on the beats, steps between; 0 = rest"""
    rhythm = rhythm or g['lead']
    tones = triad(g, root)
    out, cur = [], rng.choice(tones)
    for c in range(8):
        if rhythm[c] == '0': out.append(0); continue
        if c % 4 == 0: cur = min(tones, key=lambda t: abs(t - cur) + rng.random() * 1.5)
        else: cur = max(0, min(7, cur + rng.choice([-1, 1, -2, 2, 0])))
        m = 1 << cur
        if chordy and c % 4 == 0:
            for t in tones: m |= 1 << t
        out.append(m)
    return out

def bass_line(rng, g, root):
    """roots and fifths (an octave up now and then) on the style's bass rhythm; step 1 is always the root"""
    tones = triad(g, root)
    n = len(SCALES[g['scale']])
    lo = tones[0] if tones[0] < n else tones[0] - n
    out = []
    for c, ch in enumerate(g['bass']):
        if ch == '0': out.append(0); continue
        if c == 0: d = lo
        else: d = rng.choice([lo, lo, tones[2] if tones[2] >= lo else lo, min(7, lo + n)])
        out.append(1 << d)
    return out

def stab_line(rng, g, root):
    """chords on the style's lead rhythm (thinned), for QUAD in chords mode"""
    tones = triad(g, root)
    m = 0
    for t in tones: m |= 1 << t
    r = g['lead'] if g['lead'].count('1') <= 4 else '10010010'
    return [m if ch == '1' else 0 for ch in r]

def duo_data(rng, g):
    """MTX-DUO patterns 1-4 on the progression (LEAD steps 0-7, BASS 8-15), 5-8 empty"""
    chords, acc = [], []
    for root in g['chords']:
        lead, bass = lead_line(rng, g, root), bass_line(rng, g, root)
        chords.append(''.join('%02x' % v for v in lead + bass))
        la = sum(1 << c for c in range(8) if lead[c] and c % 4 == 0)
        ba = sum(1 << c for c in range(8) if bass[c] and c == 0)
        acc.append('%02x%02x' % (la, ba))
    return {'chords': chords + ['00' * 16] * 4, 'accents': acc + ['0000'] * 4}

QB = 8   # MTX QUAD slider banks: ACC CUT GATE GLIDE PROB RATCH OCT RES
def quad_data(rng, g, role):
    """MTX QUAD: notes and step sliders for patterns 1-4 (5-8 empty)"""
    notes, steps = [], []
    for p in range(8):
        if p < 4:
            root = g['chords'][p]
            if role == 'chords': line = stab_line(rng, g, root)
            elif role == 'lead': line = lead_line(rng, g, root)
            else: line = bass_line(rng, g, root)
            if role == 'acid':   # a 303-style line: root-heavy, accents, slides, octave jumps
                tones = triad(g, root)
                line = [(1 << rng.choice([tones[0], tones[0], tones[1], tones[2]])) if ch == '1' else 0 for ch in g['lead']]
                line[0] = 1 << tones[0]
        else:
            line = [0] * 8
        notes.append(''.join('%02x' % v for v in line))
        gate = {'chords': 0.35, 'lead': 0.55, 'bass': 0.45, 'acid': 0.5}[role]
        for b in range(QB):
            for c in range(8):
                on = line[c] != 0
                if b == 0: v = 1.0 if on and (c % 4 == 0 or (role == 'acid' and rng.random() < 0.3)) else 0.0
                elif b == 1: v = 0.5 + rng.uniform(-0.08, 0.08)
                elif b == 2: v = gate + rng.uniform(-0.1, 0.1)
                elif b == 3: v = (0.55 if role == 'acid' and rng.random() < 0.3 else 0.0)
                elif b == 4: v = 1.0
                elif b == 5: v = 0.0
                elif b == 6: v = 0.75 if role == 'acid' and rng.random() < 0.15 else 0.5
                else: v = 0.5
                steps.append(round(v, 3))
    return {'notes': notes, 'steps': steps, 'justChords': True}

def drum_patterns(rng, g):
    """MTX-DRUM: the style's groove on patterns 1-4 (2 adds ghost hats, 4 is the fill), 5-8 empty"""
    pats = []
    for p in range(4):
        rows = list(g['drums'])
        if p == 1:
            r = list(rows[3]); i = rng.randrange(8); r[i] = r[i] if r[i] != '0' else '1'; rows[3] = ''.join(r)
        if p == 3:
            for k, s in g['fill'].items(): rows[k] = ''.join(max(a, b) for a, b in zip(rows[k], s))
        pats.append(''.join(rows))
    return pats + ['0' * 64] * 4

# MTX-8 / MTX FORGE voices, top row first: F3 Eb3 C3 Bb2 G2 F2 Eb2 C2 (C minor pentatonic)
VOICE_PC = [5, 3, 0, 10, 7, 5, 3, 0]
def voice_patterns(rng, g):
    """MTX-8 / MTX FORGE: the low voice on the kick, a mid voice on the snare, a high voice on the hats, each
    a tone of the section's chord, so the pitched voices follow the progression"""
    pats = []
    for p in range(4):
        root = g['chords'][p]
        pcs = {semis(g, d) % 12 for d in triad(g, root)}
        def voice(rows):
            hit = [r for r in rows if VOICE_PC[r] in pcs]
            return hit[0] if hit else rows[0]
        low, mid, high = voice([7, 6, 5, 4]), voice([4, 3, 2]), voice([2, 1, 0])
        kick = g['drums'][0].replace('2', '1')
        snare = ''.join('1' if a != '0' or b != '0' else '0' for a, b in zip(g['drums'][1], g['drums'][2]))
        hats = ''.join('1' if c != '0' and i % 2 == 0 else '0' for i, c in enumerate(g['drums'][3]))
        rows = ['0' * 8] * 8
        rows[low], rows[mid] = kick, snare
        if high not in (low, mid): rows[high] = hats
        if p == 3: rows[mid] = ''.join(max(a, b) for a, b in zip(rows[mid], '00000111'))
        pats.append(''.join(rows))
    return pats + ['0' * 64] * 4

def hz(g, lo, hi):
    """the key's root frequency, moved by octaves into lo..hi Hz (to tune a kick or a snare)"""
    f = 32.703 * 2 ** (g['key'] / 12.0)
    while f < lo: f *= 2
    while f > hi: f /= 2
    return f

def grainforge(rng, g, base=0):
    """GRAIN-FORGE stages (ids from the GRAIN-FORGE; MTX FORGE shares them): off unless the style wants
    glitch, ROBOT in key, WARP DELAY only by octaves, a moderate dry/wet"""
    on = {0: 0, 7: 0, 14: 0, 21: 0, 48: 0}
    if g['glitch']:
        for k in rng.sample([0, 7, 48], rng.choice([1, 2])): on[k] = 1
    if g['dly'] >= 0.35 and rng.random() < 0.6: on[21] = 1
    p = {base + k: v for k, v in on.items()}
    p.update({base + 1: 0.15, base + 3: 0.1, base + 4: 0.05, base + 5: 0.0, base + 9: 0.3, base + 15: g['key'] + 12,
              base + 18: 0.0, base + 20: 0.15, base + 23: min(0.45, g['dly']), base + 24: rng.choice([0, 0, 12, -12]),
              base + 26: 0.45, base + 27: 0.45, base + 37: 0.3 if any(on.values()) else 0.0, base + 50: 0.25,
              base + 52: 0.0, base + 65: 0.0})
    return p
