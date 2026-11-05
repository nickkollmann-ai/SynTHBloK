"""Builds PATCH GEN's parts database (liquidnick.com/patchgen/parts/) from the plugins installed here.
For each plugin: one portnames --pos run per module (jack names, which jacks are on the panel, width, knobs), plus the
module's name and tags from its plugin.json. Only open-source plugins (a free licence in plugin.json) and SynTHBloK go
in, since the database is published; the others are only printed.
    python3 tools/build_parts_db.py [--force] [out dir] [plugin ...]
Out dir defaults to the site repo's patchgen/parts. Re-running only re-reads plugins whose version changed (--force: all)."""
import json, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGINS = os.path.join(os.environ['LOCALAPPDATA'], 'Rack2', 'plugins-win-x64')
EXE = os.path.join(HERE, 'portnames', 'portnames.exe')
ENV = dict(os.environ, PATH=';'.join([r'C:\msys64\mingw64\bin', r'C:\Program Files\VCV\Rack2Pro',
                                       r'C:\msys64\home\nickk\Rack-SDK', os.environ.get('PATH', '')]))
FORCE = '--force' in sys.argv   # re-read every plugin, even when its version hasn't changed
ARGS = [a for a in sys.argv[1:] if a != '--force']
OUT = ARGS[0] if ARGS else r'C:\Users\nickk\Desktop\liquidnick_site_work\patchgen\parts'
ONLY = set(ARGS[1:])
FREE = re.compile(r'gpl|mit|bsd|cc0|cc-by|apache|mpl|isc|zlib|unlicense|eupl|wtfpl|public domain|pddc', re.I)
SLUG = re.compile(r'^[A-Za-z0-9_.-]{1,64}$')
# some plugins write their settings into the working folder as they load: give them a throwaway one
WORK = tempfile.mkdtemp(prefix='partsdb-')

def one(slug, model):
    try:
        r = subprocess.run([EXE, slug, '--pos', model], capture_output=True, text=True, encoding='utf-8',
                           errors='replace', env=ENV, timeout=60, cwd=WORK)
        line = next((l for l in reversed(r.stdout.splitlines()) if l.startswith('{')), '{}')
        e = json.loads(line).get(model)
    except Exception:
        return None
    if not e or not e.get('wmm'): return None
    num = lambda v: round(v, 4) if isinstance(v, float) else v
    xy = lambda a: [[round(p[0], 1), round(p[1], 1)] if p is not None else None for p in a]
    return {'in': e['in'], 'out': e['out'], 'hp': max(1, round(e['wmm'] / 5.08)),
            'inOk': [1 if p is not None else 0 for p in e.get('inPos', [])],
            'outOk': [1 if p is not None else 0 for p in e.get('outPos', [])],
            'inPos': xy(e.get('inPos', [])), 'outPos': xy(e.get('outPos', [])),   # mm from the panel's top left, for the preview
            'params': [[p[0][:40], num(p[1]), num(p[2]), num(p[3])] if p else None for p in e.get('params', [])]}

def plugin(slug):
    pj = json.load(open(os.path.join(PLUGINS, slug, 'plugin.json'), encoding='utf-8'))
    lic = pj.get('license', '')
    if slug != 'SynTHBloK' and not FREE.search(lic): return slug, None, lic
    path = os.path.join(OUT, slug + '.json')
    if os.path.exists(path):
        old = json.load(open(path, encoding='utf-8'))
        if old.get('version') == pj['version'] and not FORCE: return slug, old, lic
    mods = {}
    with ThreadPoolExecutor(6) as ex:
        info = [m for m in pj.get('modules', []) if SLUG.match(m.get('slug', ''))]
        for m, e in zip(info, ex.map(lambda m: one(slug, m['slug']), info)):
            if e: mods[m['slug']] = dict(e, name=m.get('name', m['slug'])[:40], tags=[t.lower() for t in m.get('tags', [])][:12],
                                         **({'hidden': 1} if m.get('hidden') or m.get('deprecated') else {}))
    d = {'slug': slug, 'name': pj.get('name', slug)[:60], 'version': pj['version'], 'license': lic[:40], 'models': mods}
    json.dump(d, open(path, 'w', encoding='utf-8'), separators=(',', ':'))
    return slug, d, lic

if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    slugs = sorted(s for s in os.listdir(PLUGINS) if os.path.isfile(os.path.join(PLUGINS, s, 'plugin.json')) and (not ONLY or s in ONLY))
    idx_path = os.path.join(OUT, 'index.json')
    index = json.load(open(idx_path, encoding='utf-8')) if ONLY and os.path.exists(idx_path) else {'plugins': {}}
    skipped = {}
    for s in slugs:
        slug, d, lic = plugin(s)
        if d is None: skipped[slug] = lic; print('skip', slug, lic); continue
        index['plugins'][slug] = {'version': d['version'], 'models': len(d['models'])}
        print(slug, d['version'], len(d['models']), 'models', flush=True)
    json.dump(index, open(idx_path, 'w', encoding='utf-8'), separators=(',', ':'), sort_keys=True)
    print(len(skipped), 'skipped (not published):', ', '.join(sorted(skipped)))
