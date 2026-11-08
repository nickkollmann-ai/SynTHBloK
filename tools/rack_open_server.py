# The workbench's OPEN IN RACK button: a small local server (127.0.0.1:8767, this computer only) that opens a saved
# patch in VCV Rack by running tools/open_patch.ps1 (closes Rack cleanly, keeps a copy of what was open, starts
# Rack on the patch, brings it to the front).
#   python3 tools/rack_open_server.py        (tools/open_workbench.ps1 starts it with the workbench)
#   POST /open  {"lib": "SynTHBloK" | "Rack", "file": "<name>.vcv"}   -> {"ok": true, "path": ...}
#   GET  /ping                                                         -> {"ok": true, "rack": <is Rack running>}
#   POST /gen   {"style": "<genre>" | ""}   -> {"ok": true, "file": ...}: the PATCHES pop-up's GEN button. One new
#        patch (tools/make_series_patches.py --gen) into Rack's patches folder, for the pop-up to show; Rack is left alone.
#   MSYS: the workbench's terminal panel. One MSYS2 bash command at a time, in the SynTHBloK folder (cd carries over):
#   POST /sh       {"cmd": "..."}  -> {"ok": true, "id": n}     (needs the token from tools/.shell_token)
#   GET  /sh/out?id=n&from=k       -> {"text": ..., "next": k2, "done": bool, "code": int, "cwd": ...}
#   POST /sh/kill                  -> stops the running command
#   Only the workbench's own page can use it: its Origin, and the token this server writes at start (other
#   sites can't read either). No interactive input (no vim / less): commands that finish, like make, git, ls.
# Only .vcv files straight inside patches/ or Rack's patches folder can be opened.
import json, os, re, subprocess, sys, threading, secrets, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
LIBS = {'SynTHBloK': os.path.join(os.path.dirname(HERE), 'patches'),
        'Rack': os.path.join(os.environ['LOCALAPPDATA'], 'Rack2', 'patches')}
SCRIPT = os.path.join(HERE, 'open_patch.ps1')
ORIGINS = {'http://127.0.0.1:8766', 'http://localhost:8766'}
NAME = re.compile(r'^[\w .()-]+\.vcv$', re.I)
GEN = os.path.join(HERE, 'make_series_patches.py')
STYLE = re.compile(r'^[\w &-]{0,24}$')   # a genre name from tools/patch_genres.py, or empty for any
# the generator saves with MSYS2's tar and zstd
GEN_ENV = dict(os.environ, PATH=r'C:\msys64\usr\bin;C:\msys64\mingw64\bin;' + os.environ.get('PATH', ''))

# ---- the MSYS terminal ----
REPO = os.path.dirname(HERE)
BASH = r'C:\msys64\usr\bin\bash.exe'
TOKEN = secrets.token_hex(16)
try:
    with open(os.path.join(HERE, '.shell_token'), 'w') as f: f.write(TOKEN)
except Exception:
    pass
SH = {'id': 0, 'proc': None, 'buf': '', 'done': True, 'code': 0, 'cwd': REPO, 'lock': threading.Lock()}
MARK = '\x01CWD:'
def sh_start(cmd):
    with SH['lock']:
        if SH['proc'] and SH['proc'].poll() is None: return None
        SH['id'] += 1; SH['buf'] = ''; SH['done'] = False; SH['code'] = 0
        env = dict(os.environ, MSYSTEM='MINGW64', CHERE_INVOKING='1', TERM='dumb',
                   PATH=r'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + os.environ.get('PATH', ''))
        script = 'cd "$(cygpath -u "$SH_CWD")" 2>/dev/null; ' + cmd + '\n__c=$?; printf "\\n\\001CWD:%s" "$(pwd -W)"; exit $__c'
        env['SH_CWD'] = SH['cwd']
        p = subprocess.Popen([BASH, '-lc', script], cwd=SH['cwd'], env=env, stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        SH['proc'] = p
        my = SH['id']
    def pump():
        for raw in iter(lambda: p.stdout.read1(4096) if hasattr(p.stdout, 'read1') else p.stdout.read(1), b''):
            with SH['lock']:
                if SH['id'] == my: SH['buf'] += raw.decode('utf-8', 'replace')
        code = p.wait()
        with SH['lock']:
            if SH['id'] != my: return
            i = SH['buf'].rfind(MARK)
            if i >= 0:
                SH['cwd'] = SH['buf'][i + len(MARK):].strip() or SH['cwd']
                SH['buf'] = SH['buf'][:i].rstrip('\n') + '\n'
            SH['code'] = code; SH['done'] = True
    threading.Thread(target=pump, daemon=True).start()
    return my

def rack_running():
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Rack.exe', '/NH'], capture_output=True, text=True)
    return 'Rack.exe' in r.stdout

class H(BaseHTTPRequestHandler):
    def head(self, code):
        self.send_response(code)
        o = self.headers.get('Origin')
        if o in ORIGINS:
            self.send_header('Access-Control-Allow-Origin', o)
            self.send_header('Access-Control-Allow-Headers', 'Content-Type, X-Token')
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
    def reply(self, code, obj):
        self.head(code); self.wfile.write(json.dumps(obj).encode())
    def do_OPTIONS(self):
        self.head(204)
    def do_GET(self):
        if self.path == '/ping': return self.reply(200, {'ok': True, 'rack': rack_running()})
        if self.path.startswith('/sh/out'):
            if self.headers.get('Origin') not in ORIGINS or self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False})
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            frm = int((q.get('from') or ['0'])[0])
            with SH['lock']:
                buf = SH['buf']
                if SH['done']:
                    i = buf.rfind(MARK)
                    if i >= 0: buf = buf[:i]
                else:
                    i = buf.find(MARK)
                    if i >= 0: buf = buf[:i]   # the cwd marker is never shown
                return self.reply(200, {'ok': True, 'id': SH['id'], 'text': buf[frm:], 'next': len(buf), 'done': SH['done'], 'code': SH['code'], 'cwd': SH['cwd']})
        self.reply(404, {'ok': False})
    def do_POST(self):
        if self.path not in ('/open', '/gen', '/sh', '/sh/kill'): return self.reply(404, {'ok': False})
        # pages from other sites can't use it: only the workbench's own origin
        if self.headers.get('Origin') not in ORIGINS: return self.reply(403, {'ok': False, 'error': 'not the workbench'})
        if self.path.startswith('/sh'):
            if self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False, 'error': 'no token'})
            if self.path == '/sh/kill':
                p = SH['proc']
                if p and p.poll() is None:
                    subprocess.run(['taskkill', '/T', '/F', '/PID', str(p.pid)], capture_output=True)
                return self.reply(200, {'ok': True})
            try:
                cmd = json.loads(self.rfile.read(int(self.headers.get('Content-Length') or 0)) or b'{}').get('cmd') or ''
            except Exception:
                return self.reply(400, {'ok': False, 'error': 'bad request'})
            if not cmd.strip() or len(cmd) > 4000: return self.reply(400, {'ok': False, 'error': 'empty'})
            i = sh_start(cmd)
            if i is None: return self.reply(409, {'ok': False, 'error': 'a command is still running (Ctrl+C stops it)'})
            return self.reply(200, {'ok': True, 'id': i, 'cwd': SH['cwd']})
        if self.path == '/gen': return self.gen()
        try:
            req = json.loads(self.rfile.read(int(self.headers.get('Content-Length') or 0)) or b'{}')
            d, f = LIBS[req['lib']], req['file']
        except Exception:
            return self.reply(400, {'ok': False, 'error': 'bad request'})
        path = os.path.join(d, f)
        if not NAME.match(f) or not os.path.isfile(path):
            return self.reply(404, {'ok': False, 'error': 'no such patch'})
        subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', SCRIPT, path],
                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.reply(200, {'ok': True, 'path': path})
    def gen(self):
        try:
            style = json.loads(self.rfile.read(int(self.headers.get('Content-Length') or 0)) or b'{}').get('style') or ''
        except Exception:
            return self.reply(400, {'ok': False, 'error': 'bad request'})
        if not STYLE.match(style): return self.reply(400, {'ok': False, 'error': 'bad style'})
        args = [sys.executable, GEN, '--gen'] + ([style] if style else [])
        try:
            r = subprocess.run(args, capture_output=True, text=True, timeout=60, cwd=HERE, env=GEN_ENV,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            f = json.loads(r.stdout.strip().splitlines()[-1])['file']
        except Exception as e:
            return self.reply(500, {'ok': False, 'error': 'generator failed'})
        self.reply(200, {'ok': True, 'file': f})
    def log_message(self, fmt, *a):
        sys.stderr.write('rack_open: ' + (fmt % a) + '\n')

ThreadingHTTPServer(('127.0.0.1', 8767), H).serve_forever()
