# The workbench's OPEN IN RACK button: a small local server (127.0.0.1:8767, this computer only) that opens a saved
# patch in VCV Rack by running tools/open_patch.ps1 (closes Rack cleanly, keeps a copy of what was open, starts
# Rack on the patch, brings it to the front).
#   python3 tools/rack_open_server.py        (tools/open_workbench.ps1 starts it with the workbench)
#   POST /open  {"lib": "SynTHBloK" | "Rack", "file": "<name>.vcv"}   -> {"ok": true, "path": ...}
#   GET  /ping                                                         -> {"ok": true, "rack": <is Rack running>}
# Only .vcv files straight inside patches/ or Rack's patches folder can be opened.
import json, os, re, subprocess, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
LIBS = {'SynTHBloK': os.path.join(os.path.dirname(HERE), 'patches'),
        'Rack': os.path.join(os.environ['LOCALAPPDATA'], 'Rack2', 'patches')}
SCRIPT = os.path.join(HERE, 'open_patch.ps1')
ORIGINS = {'http://127.0.0.1:8766', 'http://localhost:8766'}
NAME = re.compile(r'^[\w .()-]+\.vcv$', re.I)

def rack_running():
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Rack.exe', '/NH'], capture_output=True, text=True)
    return 'Rack.exe' in r.stdout

class H(BaseHTTPRequestHandler):
    def head(self, code):
        self.send_response(code)
        o = self.headers.get('Origin')
        if o in ORIGINS:
            self.send_header('Access-Control-Allow-Origin', o)
            self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
    def reply(self, code, obj):
        self.head(code); self.wfile.write(json.dumps(obj).encode())
    def do_OPTIONS(self):
        self.head(204)
    def do_GET(self):
        if self.path == '/ping': self.reply(200, {'ok': True, 'rack': rack_running()})
        else: self.reply(404, {'ok': False})
    def do_POST(self):
        if self.path != '/open': return self.reply(404, {'ok': False})
        # pages from other sites can't use it: only the workbench's own origin
        if self.headers.get('Origin') not in ORIGINS: return self.reply(403, {'ok': False, 'error': 'not the workbench'})
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
    def log_message(self, fmt, *a):
        sys.stderr.write('rack_open: ' + (fmt % a) + '\n')

ThreadingHTTPServer(('127.0.0.1', 8767), H).serve_forever()
