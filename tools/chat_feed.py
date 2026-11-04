# Feeds the workbench's CHAT window: every 2 s it reads the end of the newest Claude Code transcript for this
# project and writes the last messages (yours and Claude's, text only) to tools/chat_feed.json.
#   python3 tools/chat_feed.py          (leave it running; Ctrl-C stops it)
# tools/chat_feed.json is local only (listed in .git/info/exclude).
import glob, json, os, re, sys, time
from datetime import datetime

PROJ = os.path.expanduser(r'~\.claude\projects\C--msys64-home-nickk-SynTHBloK')
if not os.path.isdir(PROJ):
    PROJ = r'C:\Users\nickk\.claude\projects\C--msys64-home-nickk-SynTHBloK'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'chat_feed.json')
TAIL = 3 << 20      # read the last 3 MB of the transcript
KEEP = 40           # messages kept in the feed
CLIP = 600          # characters kept per message

def stamp(s):
    try: return int(datetime.fromisoformat(s.replace('Z', '+00:00')).timestamp() * 1000)
    except Exception: return 0

def clean(t):
    t = re.sub(r'<(system-reminder|command-[\w-]+|local-command-[\w-]+)>.*?</\1>', '', t, flags=re.S)
    return re.sub(r'\s+', ' ', t).strip()

def read(path):
    with open(path, 'rb') as f:
        f.seek(0, 2); n = f.tell(); f.seek(max(0, n - TAIL))
        lines = f.read().decode('utf-8', 'replace').splitlines()
    if n > TAIL: lines = lines[1:]   # the first line is cut
    items = []
    for ln in lines:
        try: j = json.loads(ln)
        except Exception: continue
        kind, msg = j.get('type'), j.get('message') or {}
        c = msg.get('content')
        if kind == 'user' and isinstance(c, str) and not j.get('isMeta'):
            t, who = clean(c), 'you'
        elif kind == 'assistant' and isinstance(c, list):
            t, who = clean(' '.join(b.get('text', '') for b in c if b.get('type') == 'text')), 'claude'
        else:
            continue
        if not t: continue
        items.append({'who': who, 't': stamp(j.get('timestamp', '')), 'text': t[:CLIP] + ('…' if len(t) > CLIP else '')})
    return items[-KEEP:]

last = None
while True:
    try:
        files = glob.glob(os.path.join(PROJ, '*.jsonl'))
        if files:
            path = max(files, key=os.path.getmtime)
            data = {'session': os.path.basename(path)[:8], 'items': read(path)}
            body = json.dumps(data, ensure_ascii=False)
            if body != last:
                tmp = OUT + '.tmp'
                with open(tmp, 'w', encoding='utf-8') as f: f.write(body)
                os.replace(tmp, OUT)
                last = body
    except Exception as e:
        print('chat_feed:', e, file=sys.stderr)
    if '--once' in sys.argv: break
    time.sleep(2)
