# Feeds the workbench's PATCH MAP: every 3 s it checks Rack's autosave (%LOCALAPPDATA%\Rack2\autosave\patch.json,
# which Rack rewrites every ~15 s and on close) and writes the modules, their rack positions and every cable,
# with jack names, to tools/patch_feed.json.
#   python3 tools/patch_feed.py          (leave it running; Ctrl-C stops it)
# Jack names come from tools/portnames/portnames.exe, run once per plugin (again when its plugin.dll changes)
# and cached in tools/port_names.json. All three files are local only (listed in .git/info/exclude).
import json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
RACK = os.path.join(os.environ['LOCALAPPDATA'], 'Rack2')
PATCH = os.path.join(RACK, 'autosave', 'patch.json')
PLUGINS = os.path.join(RACK, 'plugins-win-x64')
OUT = os.path.join(HERE, 'patch_feed.json')
CACHE = os.path.join(HERE, 'port_names.json')
EXE = os.path.join(HERE, 'portnames', 'portnames.exe')
DLLPATH = [r'C:\msys64\mingw64\bin', r'C:\Program Files\VCV\Rack2Pro', r'C:\msys64\home\nickk\Rack-SDK']

# Rack's own modules can't be built outside Rack, so their jacks are named here
CORE = {
    'AudioInterface2': {'name': 'Audio 2', 'in': ['To device 1', 'To device 2'], 'out': ['From device 1', 'From device 2']},
    'AudioInterface': {'name': 'Audio 8', 'in': ['To device %d' % i for i in range(1, 9)], 'out': ['From device %d' % i for i in range(1, 9)]},
    'AudioInterface16': {'name': 'Audio 16', 'in': ['To device %d' % i for i in range(1, 17)], 'out': ['From device %d' % i for i in range(1, 17)]},
    'MIDIToCVInterface': {'name': 'MIDI to CV', 'in': [], 'out': ['V/oct', 'Gate', 'Velocity', 'Aftertouch', 'Pitch wheel', 'Mod wheel', 'Retrigger', 'Clock', 'Clock divider', 'Start', 'Stop', 'Continue']},
    'MIDI-Map': {'name': 'MIDI Map', 'in': [], 'out': []},
}

def load_json(p, default):
    try:
        with open(p, encoding='utf-8') as f: return json.load(f)
    except Exception: return default

def write(p, obj):
    tmp = p + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f: json.dump(obj, f, ensure_ascii=False, separators=(',', ':'))
    os.replace(tmp, p)

cache = load_json(CACHE, {})

def plugin_info(slug):
    """Jack names and display names for one plugin, from the cache or a fresh portnames run."""
    if slug == 'Core':
        return {'name': 'Core', 'models': {k: v for k, v in CORE.items()}}
    d = os.path.join(PLUGINS, slug)
    try:
        st = os.stat(os.path.join(d, 'plugin.dll')); stamp = '%d-%d' % (st.st_mtime, st.st_size)
    except OSError:
        return {'name': slug, 'models': {}}
    hit = cache.get(slug)
    if hit and hit.get('stamp') == stamp: return hit
    models = {}
    try:
        env = dict(os.environ, PATH=';'.join(DLLPATH + [os.environ.get('PATH', '')]))
        r = subprocess.run([EXE, slug], capture_output=True, text=True, encoding='utf-8', errors='replace', env=env, timeout=60)
        line = next((l for l in reversed(r.stdout.splitlines()) if l.startswith('{')), '{}')
        models = json.loads(line)
    except Exception as e:
        print('patch_feed: portnames %s: %s' % (slug, e), file=sys.stderr)
    man = load_json(os.path.join(d, 'plugin.json'), {})
    for m in man.get('modules', []):
        if m.get('slug') in models: models[m['slug']]['name'] = m.get('name', m['slug'])
    hit = {'stamp': stamp, 'name': man.get('name', slug), 'models': models}
    cache[slug] = hit
    write(CACHE, cache)
    return hit

widths = {}
def panel_hp(slug, model):
    """Width in HP from the module's panel SVG (res/<model>.svg, any case), or None."""
    k = (slug, model)
    if k not in widths:
        widths[k] = None
        res = os.path.join(PLUGINS, slug, 'res')
        try:
            names = {}
            for root, _, files in os.walk(res):
                for f in files: names.setdefault(f.lower(), os.path.join(root, f))
            path = names.get(model.lower() + '.svg')
            if path:
                head = open(path, encoding='utf-8', errors='replace').read(2000)
                m = re.search(r'<svg[^>]*?\swidth="([\d.]+)(mm|px)?"', head, re.S)
                if m:
                    v = float(m.group(1))
                    widths[k] = round(v / 5.08 if m.group(2) == 'mm' else v / 15)
        except OSError:
            pass
    return widths[k]

def build(p):
    mods, out = {}, []
    for m in p.get('modules', []):
        slug, model = m.get('plugin', '?'), m.get('model', '?')
        info = plugin_info(slug)
        mi = info['models'].get(model, {})
        x, y = (m.get('pos') or [0, 0])[:2]
        e = {'id': m['id'], 'slug': slug, 'plugin': info.get('name', slug), 'model': model, 'name': mi.get('name', model),
             'x': x, 'y': y, 'in': mi.get('in', []), 'out': mi.get('out', [])}
        mods[m['id']] = e; out.append(e)
    # width from the gap to the next module in the same row (the patch file doesn't store it)
    rows = {}
    for e in out: rows.setdefault(e['y'], []).append(e)
    for r in rows.values():
        r.sort(key=lambda e: e['x'])
        for a, b in zip(r, r[1:] + [None]):
            gap = b['x'] - a['x'] if b else None
            hp = panel_hp(a['slug'], a['model'])
            a['w'] = max(3, min(hp, gap) if hp and gap else hp or min(gap or 12, 60))
    def port(e, kind, i):
        names = e[kind] if e else []
        return names[i] if i < len(names) and names[i] else ('%s %d' % ('in' if kind == 'in' else 'out', i + 1))
    cables = []
    for c in p.get('cables', []):
        a, b = mods.get(c.get('outputModuleId')), mods.get(c.get('inputModuleId'))
        if not a or not b: continue
        cables.append({'from': a['id'], 'fo': c['outputId'], 'fn': port(a, 'out', c['outputId']),
                       'to': b['id'], 'ti': c['inputId'], 'tn': port(b, 'in', c['inputId']), 'color': c.get('color', '#00e5ff')})
    for e in out:
        e['nin'], e['nout'] = len(e.pop('in')), len(e.pop('out'))
    return {'modules': out, 'cables': cables}

last_m = None
while True:
    try:
        mt = os.path.getmtime(PATCH)
        if mt != last_m:
            p = load_json(PATCH, {})
            if p.get('modules'):          # empty or unreadable: Rack is mid-write, try again next pass
                data = build(p)
                data['t'] = int(mt * 1000)
                write(OUT, data)
                last_m = mt
    except Exception as e:
        print('patch_feed:', e, file=sys.stderr)
    if '--once' in sys.argv: break
    time.sleep(3)
