"""Converts Rack patches (.vcv) to SynTHBloK's current plugins and module slugs.

Two changes made old patches show SynTHBloK modules as missing (Rack finds a module by plugin + slug):
  1. for the VCV Library the module slugs lost their "SynTHBloK" prefix (SynTHBloKDrum -> Drum, SynTHBloK -> Quad);
  2. SynTHBloK was split for sale: the MTX pack keeps the plugin slug "SynTHBloK", the other modules moved to
     their own plugins (Kick -> SynTHBloK-Kick, ...; tools/lineup.json).
This rewrites plugin + slug in patch.json and leaves everything else in the patch as it was (module data folders too).
Before changing a patch it copies it to <folder>/old-slugs-backup/.
    python3 tools/convert_slugs.py <patch.vcv | folder> [...]      (a folder: every .vcv in it, not subfolders)
A folder holding a plain patch.json (Rack's autosave folder) has that patch.json converted, with a copy kept as
patch.json.old-slugs.
A .vcv is a zstd-compressed tar (MSYS2's zstd, as tools/make_patches.py uses). Patches already converted are skipped.
RANDROYD's saved tracks inside a patch need nothing: RANDROYD translates old slugs when it loads a track."""
import io, json, os, shutil, subprocess, sys, tarfile

RENAMED = {
    "SynTHBloK": "Quad", "SynTHBloKMtxQuad": "MtxQuad", "SynTHBloKDualFilter": "DualFilter", "SynTHBloKPoly": "Poly",
    "SynTHBloKStereo": "Stereo", "SynTHBloKFX": "FX", "SynTHBloKResonator": "Resonator", "SynTHBloKMix24": "Mix24",
    "SynTHBloKMix12": "Mix12", "SynTHBloKMix8": "Mix8", "SynTHBloKPlayer": "Player", "SynTHBloKKick": "Kick",
    "SynTHBloKPlayerView": "PlayerView", "SynTHBloKSnareHat": "SnareHat", "SynTHBloKEuclid": "Euclid",
    "SynTHBloKJamProbe": "JamProbe", "SynTHBloKJam": "Jam", "SynTHBloKMatrix8": "Matrix8",
    "SynTHBloKGrainForge": "GrainForge", "SynTHBloKMatrixForge": "MatrixForge", "SynTHBloKDuo": "Duo",
    "SynTHBloKDrum": "Drum", "SynTHBloKEcho": "Echo", "SynTHBloKLoop": "Loop", "SynTHBloKMatrixFx": "MatrixFx",
    "SynTHBloKMatrixSong": "MatrixSong", "SynTHBloKRandroyd": "Randroyd",
}
# module -> the single plugin it moved to (the MTX pack's modules stay in "SynTHBloK"); same as tools/lineup.json
SINGLES = {
    "Quad": "SynTHBloK-Quad", "Poly": "SynTHBloK-Poly", "Stereo": "SynTHBloK-StereoPhaser",
    "DualFilter": "SynTHBloK-DualFilter", "NoizeeFlagshipFilter": "SynTHBloK-MorphFilter", "Resonator": "SynTHBloK-Resonator",
    "FX": "SynTHBloK-CharacterDelay", "GrainForge": "SynTHBloK-GrainForge", "BlokMaster": "SynTHBloK-BloKMaster",
    "Kick": "SynTHBloK-Kick", "SnareHat": "SynTHBloK-SnareHat", "Euclid": "SynTHBloK-Euclid", "JamProbe": "SynTHBloK-JamProbe",
    "Mix24": "SynTHBloK-MixFire", "Mix12": "SynTHBloK-MixFire", "Mix8": "SynTHBloK-MixFire",
    "Player": "SynTHBloK-PlayerPlus", "PlayerView": "SynTHBloK-PlayerPlus",
}
ZSTD = os.environ.get('ZSTD', 'zstd')


def rename(patch):
    n = 0
    for mod in patch.get('modules', []):
        if mod.get('plugin') != 'SynTHBloK':
            continue
        model = RENAMED.get(mod.get('model'), mod.get('model'))
        plugin = SINGLES.get(model, 'SynTHBloK')
        if (plugin, model) != ('SynTHBloK', mod.get('model')):
            mod['plugin'], mod['model'] = plugin, model
            n += 1
    return n


def convert_json(path):   # a plain patch.json, as in Rack's autosave folder
    patch = json.load(open(path, encoding='utf-8'))
    n = rename(patch)
    if not n:
        return 'already up to date'
    if not os.path.exists(path + '.old-slugs'):
        shutil.copy2(path, path + '.old-slugs')
    json.dump(patch, open(path, 'w', encoding='utf-8'), indent=2, ensure_ascii=False)
    return f'{n} module(s) renamed'


def convert(path):
    if os.path.basename(path).lower() == 'patch.json':
        return convert_json(path)
    raw = open(path, 'rb').read()
    tar_bytes = subprocess.run([ZSTD, '-q', '-d', '-c'], input=raw, capture_output=True, check=True).stdout
    src = tarfile.open(fileobj=io.BytesIO(tar_bytes))
    members = [(m, src.extractfile(m).read() if m.isfile() else None) for m in src.getmembers()]
    pj = next((i for i, (m, _) in enumerate(members) if m.name.lstrip('./') == 'patch.json'), None)
    if pj is None:
        return 'no patch.json, skipped'
    patch = json.loads(members[pj][1].decode('utf-8'))
    n = rename(patch)
    if not n:
        return 'already up to date'
    backup = os.path.join(os.path.dirname(path) or '.', 'old-slugs-backup')
    os.makedirs(backup, exist_ok=True)
    keep = os.path.join(backup, os.path.basename(path))
    if not os.path.exists(keep):
        shutil.copy2(path, keep)
    data = json.dumps(patch, indent=2, ensure_ascii=False).encode('utf-8')
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode='w', format=tarfile.GNU_FORMAT) as dst:
        for i, (m, body) in enumerate(members):
            if i == pj:
                m.size = len(data)
                body = data
            dst.addfile(m, io.BytesIO(body) if body is not None else None)
    z = subprocess.run([ZSTD, '-q', '-c'], input=out.getvalue(), capture_output=True, check=True).stdout
    with open(path, 'wb') as f:
        f.write(z)
    return f'{n} module(s) renamed'


def main(args):
    if not args:
        print(__doc__)
        return 1
    files = []
    for a in args:
        if os.path.isdir(a):
            if os.path.isfile(os.path.join(a, 'patch.json')):   # Rack's autosave folder
                files.append(os.path.join(a, 'patch.json'))
            files += sorted(os.path.join(a, f) for f in os.listdir(a) if f.lower().endswith('.vcv'))
        elif not os.path.exists(a):
            print(f'{a}: not found, skipped')
        else:
            files.append(a)
    bad = 0
    for f in files:
        try:
            print(f'{os.path.relpath(f) if len(f) < 60 else os.path.basename(f)}: {convert(f)}')
        except Exception as e:   # a damaged or non-zstd file: report it and go on
            bad += 1
            print(f'{os.path.basename(f)}: FAILED ({e})')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
