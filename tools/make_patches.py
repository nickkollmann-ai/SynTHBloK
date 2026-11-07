"""Builds the SynTHBloK track patches in patches/ (each a full track of SynTHBloK modules: drums, MTX QUAD synth,
LOOP, ECHO, mixer, the MTX COMPOSER as the clock)
and copies them into Rack's patches folder, so they show on the workbench's PATCHES page and in Rack's File > Open.
Each one starts from tools/patches_base.json (a working track) and adds a recipe on top: where the DUAL FILTER
sits in the signal and how it's set.
    python3 tools/make_patches.py
(tools/make_vcv_patches.py builds the series-with-VCV-modules set on top of these helpers.)
A .vcv is a zstd-compressed tar of patch.json (MSYS2's tar and zstd)."""
import copy, json, os, random, shutil, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, 'patches')
RACK_PATCHES = os.path.join(os.environ['LOCALAPPDATA'], 'Rack2', 'patches')
BASE = json.load(open(os.path.join(HERE, 'patches_base.json'), encoding='utf-8'))

def mod(p, model):
    return next(m for m in p['modules'] if m['model'] == model)

def setp(m, values):
    """values: {param id: value}"""
    have = {e['id']: e for e in m['params']}
    for k, v in values.items():
        if k in have: have[k]['value'] = v
        else: m['params'].append({'value': v, 'id': k})

def unplug(p, to_mod, to_ids):
    p['cables'] = [c for c in p['cables'] if not (c['inputModuleId'] == to_mod['id'] and c['inputId'] in to_ids)]

def plug(p, a, out_id, b, in_id, color):
    p['cables'].append({'id': random.randrange(1 << 50, 1 << 52), 'outputModuleId': a['id'], 'outputId': out_id,
                        'inputModuleId': b['id'], 'inputId': in_id, 'color': color})

# DUAL FILTER ids (src/SynTHBloKDualFilter.cpp)
A_CUT, A_RES, A_DRIVE, A_MODE, B_CUT, B_RES, B_DRIVE, B_MODE = range(8)
ROUTE, LINK, OFFSET, LFO_RATE, LFO_AMT, ENV_AMT, SYNC, MIX, A_SLOPE, B_SLOPE = range(8, 18)
LFO_SHAPE, LFO_DEST, LFO_RES, ENV_SRC, ENV_A, ENV_D, ENV_S, ENV_R, ENV_TRIG = range(18, 27)
PH_RATE, PH_DEPTH, PH_FDBK, PH_MIX, EQ_LO, EQ_MID, EQ_HI = range(27, 34)
IN_L, IN_R, VOCT, CUT_A, CUT_B, SYNC_IN, GATE = range(7)
OUT_A, OUT_B, OUT_L, OUT_R = range(4)
def sync_div(pulses):   # LFO RATE with SYNC on: clock pulses per LFO cycle (32, 16, 8, 4, 2, 1, 0.5)
    return ([32, 16, 8, 4, 2, 1, 0.5].index(pulses) + 0.5) / 7

def own_modules(p):
    """Every patch is SynTHBloK modules only (plus Rack's Audio module, the way out to the speakers): the other
    plugins' modules go, and the MTX COMPOSER (MatrixSong) becomes the master clock in place of the BPM clock,
    its clock, run and reset going to everything that the BPM clock and the drum clock drove."""
    keep = lambda m: m['plugin'] == 'SynTHBloK' or (m['plugin'] == 'Core' and m['model'].startswith('AudioInterface'))
    gone = {m['id'] for m in p['modules'] if not keep(m)}
    p['modules'] = [m for m in p['modules'] if keep(m)]
    for m in p['modules']:
        for side in ('leftModuleId', 'rightModuleId'):
            if m.get(side) in gone: del m[side]
    p['cables'] = [c for c in p['cables'] if c['outputModuleId'] not in gone and c['inputModuleId'] not in gone]
    song, drum, quad, loop, echo = (mod(p, s) for s in ('MatrixSong', 'Drum', 'MtxQuad', 'Loop', 'Echo'))
    # the clock now comes from the song, so the drum's clock no longer feeds the others or the song
    p['cables'] = [c for c in p['cables'] if not (c['outputModuleId'] == drum['id'] and c['outputId'] == 27)]
    CLK, RUN, RST = 0, 1, 2   # MatrixSong outputs
    for m, clk, run, rst in ((drum, 0, 2, 1), (quad, 4, 15, 2), (loop, 8, 14, 15)):
        unplug(p, m, [clk, run, rst])
        plug(p, song, CLK, m, clk, '#9437ff'); plug(p, song, RUN, m, run, '#00b56e'); plug(p, song, RST, m, rst, '#3695ef')
    unplug(p, echo, [2]); plug(p, song, CLK, echo, 2, '#9437ff')

def synth_sweep(p):
    """The MTX QUAD through the DUAL FILTER: a 24 dB low-pass swept by an ADSR on every grid note and a slow,
    bar-long LFO, the high-pass on B taking out the mud, a touch of phaser."""
    df, quad, mix, drum = mod(p, 'DualFilter'), mod(p, 'MtxQuad'), mod(p, 'Mix12'), mod(p, 'Drum')
    unplug(p, mix, [26, 27])
    plug(p, quad, 7, df, IN_L, '#ff9300'); plug(p, quad, 8, df, IN_R, '#ff9300')
    plug(p, df, OUT_L, mix, 26, '#00b56e'); plug(p, df, OUT_R, mix, 27, '#3695ef')
    plug(p, quad, 14, df, GATE, '#ffb437'); plug(p, mod(p, 'MatrixSong'), 0, df, SYNC_IN, '#9437ff')
    setp(df, {ROUTE: 0, A_CUT: 0.42, A_RES: 0.55, A_MODE: 0, A_SLOPE: 1, A_DRIVE: 0.2,
              B_CUT: 0.2, B_RES: 0.1, B_MODE: 3, B_SLOPE: 0,
              ENV_SRC: 1, ENV_A: 0.05, ENV_D: 0.45, ENV_S: 0.2, ENV_R: 0.4, ENV_AMT: 0.45,
              LFO_SHAPE: 0, LFO_DEST: 1, LFO_AMT: 0.15, SYNC: 1, LFO_RATE: sync_div(16),
              PH_MIX: 0.25, PH_DEPTH: 0.5, PH_RATE: 0.3, MIX: 1})

def drum_bus(p):
    """The drum master through the DUAL FILTER: a driven 24 dB high-pass into a resonant low-pass that a saw LFO
    opens every half bar, with the follower kicking it open on every hit."""
    df, mix, drum = mod(p, 'DualFilter'), mod(p, 'Mix12'), mod(p, 'Drum')
    unplug(p, mix, [22, 23])
    plug(p, drum, 0, df, IN_L, '#ff9300'); plug(p, drum, 1, df, IN_R, '#ff9300')
    plug(p, df, OUT_L, mix, 22, '#00b56e'); plug(p, df, OUT_R, mix, 23, '#3695ef')
    plug(p, mod(p, 'MatrixSong'), 0, df, SYNC_IN, '#9437ff')
    setp(df, {ROUTE: 0, A_CUT: 0.25, A_RES: 0.3, A_MODE: 3, A_SLOPE: 1, A_DRIVE: 0.3,
              B_CUT: 0.72, B_RES: 0.45, B_MODE: 0, B_SLOPE: 1,
              ENV_SRC: 0, ENV_AMT: 0.25,
              LFO_SHAPE: 1, LFO_DEST: 2, LFO_AMT: 0.3, SYNC: 1, LFO_RATE: sync_div(8),
              EQ_LO: 0.25, EQ_HI: 0.2, MIX: 1})

def loop_phase(p):
    """The LOOP's mix through the DUAL FILTER in STEREO: two resonant band-passes, a random LFO stepping their
    cutoffs every beat, a deep slow phaser, blended in at 70%."""
    df, mix, loop, drum = mod(p, 'DualFilter'), mod(p, 'Mix12'), mod(p, 'Loop'), mod(p, 'Drum')
    unplug(p, mix, [28, 29])
    plug(p, loop, 8, df, IN_L, '#ff9300'); plug(p, loop, 9, df, IN_R, '#ff9300')
    plug(p, df, OUT_L, mix, 28, '#00b56e'); plug(p, df, OUT_R, mix, 29, '#3695ef')
    plug(p, mod(p, 'MatrixSong'), 0, df, SYNC_IN, '#9437ff')
    setp(df, {ROUTE: 2, A_CUT: 0.55, A_RES: 0.6, A_MODE: 1, A_SLOPE: 0, B_CUT: 0.62, B_RES: 0.6, B_MODE: 1, B_SLOPE: 0,
              LFO_SHAPE: 3, LFO_DEST: 0, LFO_AMT: 0.2, SYNC: 1, LFO_RATE: sync_div(4),
              PH_MIX: 0.6, PH_DEPTH: 0.8, PH_FDBK: 0.6, PH_RATE: 0.25, MIX: 0.7})

# --- the rest of the series: each patch brings in more SynTHBloK modules, placed on a new row under the track ---
VERSION = json.load(open(os.path.join(REPO, 'plugin.json'), encoding='utf-8'))['version']
HP = {'Kick': 9, 'SnareHat': 9, 'Euclid': 9, 'Poly': 9, 'Stereo': 12,
      'FX': 12, 'NoizeeFlagshipFilter': 13, 'BlokMaster': 44, 'GrainForge': 40, 'MatrixFx': 44}

def remove(p, *models):
    """takes modules out with their cables"""
    gone = {m['id'] for m in p['modules'] if m['model'] in models}
    p['modules'] = [m for m in p['modules'] if m['id'] not in gone]
    for m in p['modules']:
        for side in ('leftModuleId', 'rightModuleId'):
            if m.get(side) in gone: del m[side]
    p['cables'] = [c for c in p['cables'] if c['outputModuleId'] not in gone and c['inputModuleId'] not in gone]

def add(p, model, params=None, plugin='SynTHBloK', version=None):
    """adds a module (its own defaults, plus params) at the end of the row under the track"""
    row = max(m['pos'][1] for m in p['modules']) + (0 if p.get('_row') else 1)
    x = p.get('_x', min(m['pos'][0] for m in p['modules']))
    m = {'id': random.randrange(1 << 50, 1 << 52), 'plugin': plugin, 'model': model, 'version': version or VERSION,
         'params': [], 'pos': [x, row]}
    p['_row'], p['_x'] = row, x + HP[model]
    setp(m, params or {})
    p['modules'].append(m)
    return m

def master_chain(p):
    """MIX-12's master through BloKMaster on its way to the speakers"""
    mix, audio = mod(p, 'Mix12'), next(m for m in p['modules'] if m['model'].startswith('AudioInterface'))
    bm = add(p, 'BlokMaster')
    unplug(p, audio, [0, 1])
    plug(p, mix, 0, bm, 0, '#ff9300'); plug(p, mix, 1, bm, 1, '#ff9300')
    plug(p, bm, 0, audio, 0, '#00b56e'); plug(p, bm, 1, audio, 1, '#3695ef')

def clocked(p, m, clk, rst=None, run=None):
    song = mod(p, 'MatrixSong')
    plug(p, song, 0, m, clk, '#9437ff')
    if rst is not None: plug(p, song, 2, m, rst, '#3695ef')
    if run is not None: plug(p, song, 1, m, run, '#00b56e')

def euclid_kit(p):
    """A synth drum kit in place of the MTX-DRUM: EUCLID plays KICK (four on the floor) and SNARE/HAT (backbeat,
    off-beat closed hats, an open hat at the end of the bar), mixed on channels 1-3, through BloKMaster."""
    remove(p, 'Drum', 'DualFilter')
    mix = mod(p, 'Mix12')
    eu = add(p, 'Euclid', {0: 16, 1: 4, 2: 0,  3: 16, 4: 2, 5: 4,  6: 16, 7: 8, 8: 1,  9: 16, 10: 1, 11: 14})
    clocked(p, eu, 0, rst=1)
    kick, sh = add(p, 'Kick'), add(p, 'SnareHat')
    plug(p, eu, 0, kick, 0, '#ff2b2b'); plug(p, eu, 1, sh, 0, '#ffb437'); plug(p, eu, 2, sh, 1, '#ffe14a'); plug(p, eu, 3, sh, 2, '#ffe14a')
    plug(p, kick, 0, mix, 6, '#ff2b2b'); plug(p, sh, 0, mix, 8, '#ffb437'); plug(p, sh, 1, mix, 10, '#ffe14a')
    master_chain(p)

def poly_pad(p):
    """The MTX QUAD's chord grid played by POLY (one voice per chord note) into the CHARACTER DELAY, clocked to the
    song, on channel 11."""
    remove(p, 'DualFilter')
    quad, mix = mod(p, 'MtxQuad'), mod(p, 'Mix12')
    poly, dl = add(p, 'Poly'), add(p, 'FX')
    unplug(p, mix, [26, 27])
    plug(p, quad, 13, poly, 0, '#ffb437'); plug(p, quad, 14, poly, 1, '#ffe14a')
    plug(p, poly, 0, dl, 9, '#ff9300'); plug(p, poly, 1, dl, 10, '#ff9300')
    clocked(p, dl, 8)
    plug(p, dl, 0, mix, 26, '#00b56e'); plug(p, dl, 1, mix, 27, '#3695ef')

def morph_quad(p):
    """The MTX QUAD through the MORPH FILTER, the song's section CV turning the morph, then the STEREO width stage, on
    channel 11."""
    remove(p, 'DualFilter')
    quad, mix = mod(p, 'MtxQuad'), mod(p, 'Mix12')
    mf, st = add(p, 'NoizeeFlagshipFilter'), add(p, 'Stereo')
    unplug(p, mix, [26, 27])
    plug(p, quad, 7, mf, 16, '#ff9300'); plug(p, quad, 8, mf, 17, '#ff9300')
    plug(p, mod(p, 'MatrixSong'), 4, mf, 2, '#9437ff')   # the song's section CV turns the morph
    plug(p, mf, 0, st, 0, '#ff9300')
    plug(p, st, 0, mix, 26, '#00b56e'); plug(p, st, 1, mix, 27, '#3695ef')

def grain_loop(p):
    """The MTX-LOOP's mix through GRAIN-FORGE, stepped by the song's clock and reset with it, on channel 12."""
    remove(p, 'DualFilter')
    loop, mix = mod(p, 'Loop'), mod(p, 'Mix12')
    gf = add(p, 'GrainForge')
    unplug(p, mix, [28, 29])
    plug(p, loop, 8, gf, 0, '#ff9300'); plug(p, loop, 9, gf, 1, '#ff9300')
    clocked(p, gf, 2, rst=3)
    plug(p, gf, 0, mix, 28, '#00b56e'); plug(p, gf, 1, mix, 29, '#3695ef')

def mtx_fx_bus(p):
    """Drums, synth and loops all through MTX-FX (inputs 1-3), clocked, reset and run by the song, on channel 9,
    then BloKMaster."""
    remove(p, 'DualFilter')
    drum, quad, loop, mix = (mod(p, s) for s in ('Drum', 'MtxQuad', 'Loop', 'Mix12'))
    fx = add(p, 'MatrixFx')
    unplug(p, mix, list(range(6, 24)) + [26, 27, 28, 29])
    plug(p, drum, 0, fx, 0, '#ff2b2b'); plug(p, quad, 7, fx, 1, '#ff9300'); plug(p, loop, 8, fx, 2, '#ffb437')
    clocked(p, fx, 8, rst=9, run=10)
    plug(p, fx, 0, mix, 22, '#00b56e'); plug(p, fx, 1, mix, 23, '#3695ef')
    master_chain(p)

RECIPES = [('DualFilter_Synth_Sweep', synth_sweep), ('DualFilter_Drum_Bus', drum_bus), ('DualFilter_Loop_Phase', loop_phase),
           ('Euclid_Kick_Snare', euclid_kit), ('Poly_Pad_Delay', poly_pad), ('MorphFilter_Quad', morph_quad),
           ('GrainForge_Loop', grain_loop), ('MatrixFX_Bus', mtx_fx_bus)]

def save_vcv(p, path):
    # recipes name every SynTHBloK module under the plugin "SynTHBloK": move the singles to their own plugins
    from convert_slugs import rename
    rename(p)
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, 'modules'))
        with open(os.path.join(d, 'patch.json'), 'w', encoding='utf-8') as f: json.dump(p, f, indent=2)
        tar = subprocess.run(['tar', '-C', d, '-cf', '-', './modules', './patch.json'], capture_output=True, check=True).stdout
        z = subprocess.run(['zstd', '-q', '-c'], input=tar, capture_output=True, check=True).stdout
    with open(path, 'wb') as f: f.write(z)

def write_all(recipes):
    os.makedirs(OUT, exist_ok=True)
    for name, recipe in recipes:
        random.seed(name)   # the same ids on every run
        p = copy.deepcopy(BASE)
        own_modules(p)
        recipe(p)
        # a DUAL FILTER the recipe didn't patch only takes up room
        df = next((m for m in p['modules'] if m['model'] == 'DualFilter'), None)
        if df and not any(df['id'] in (c['inputModuleId'], c['outputModuleId']) for c in p['cables']):
            remove(p, 'DualFilter')
        for k in [k for k in p if k.startswith('_')]: del p[k]   # the recipes' own notes
        path = os.path.join(OUT, name + '.vcv')
        save_vcv(p, path)
        shutil.copy2(path, os.path.join(RACK_PATCHES, name + '.vcv'))
        print('wrote', path, len(p['modules']), 'modules', len(p['cables']), 'cables')

if __name__ == '__main__':
    write_all(RECIPES)
