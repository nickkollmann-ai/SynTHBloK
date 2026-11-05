"""Makes PATCH GEN's SynTHBloK panel pictures (liquidnick.com/patchgen/panels/<Model>.webp) from the Rack screenshots
that build_and_reload.ps1 saves in web-images/screenshots, so the patch map always shows the current panels.
Each picture is checked against the module's width in the parts database (a stale or odd picture is reported).
    python3 tools/make_patchgen_panels.py [out dir]"""
import json, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(os.path.dirname(HERE), 'web-images', 'screenshots')
SITE = r'C:\Users\nickk\Desktop\liquidnick_site_work\patchgen'
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SITE, 'panels')
HEIGHT = 450   # px for the 128.5 mm panel: sharp when zoomed in, small to load

models = json.load(open(os.path.join(SITE, 'parts', 'SynTHBloK.json'), encoding='utf-8'))['models']
os.makedirs(OUT, exist_ok=True)
total = 0
for model, m in sorted(models.items()):
    src = os.path.join(SHOTS, model + '.png')
    if not os.path.exists(src): print('no screenshot', model); continue
    im = Image.open(src).convert('RGB')
    hp = im.width / im.height * 128.5 / 5.08
    if abs(hp - m['hp']) > 0.6: print(f'{model}: picture is {hp:.1f} HP, module is {m["hp"]} HP')
    im = im.resize((round(im.width * HEIGHT / im.height), HEIGHT), Image.LANCZOS)
    dst = os.path.join(OUT, model + '.webp')
    im.save(dst, 'WEBP', quality=80, method=6)
    total += os.path.getsize(dst)
print(f'{len(os.listdir(OUT))} panels, {total / 1024:.0f} KB in {OUT}')
