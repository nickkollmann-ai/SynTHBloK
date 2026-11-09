# The workbench's OPEN IN RACK button: a small local server (127.0.0.1:8767, this computer only) that opens a saved
# patch in VCV Rack by running tools/open_patch.ps1 (closes Rack cleanly, keeps a copy of what was open, starts
# Rack on the patch, brings it to the front).
#   python3 tools/rack_open_server.py        (tools/open_workbench.ps1 starts it with the workbench)
#   POST /open  {"lib": "SynTHBloK" | "Rack", "file": "<name>.vcv"}   -> {"ok": true, "path": ...}
#   POST /rack/launch               opens VCV Rack (as it last was), or brings it to the front if it's running
#   GET  /ping                                                         -> {"ok": true, "rack": <is Rack running>}
#   POST /gen   {"style": "<genre>" | ""}   -> {"ok": true, "file": ...}: the PATCHES pop-up's GEN button. One new
#        patch (tools/make_series_patches.py --gen) into Rack's patches folder, for the pop-up to show; Rack is left alone.
#   MSYS: the workbench's terminal panel. One MSYS2 bash command at a time, in the SynTHBloK folder (cd carries over):
#   POST /sh       {"cmd": "..."}  -> {"ok": true, "id": n}     (needs the token from tools/.shell_token)
#   GET  /sh/out?id=n&from=k       -> {"text": ..., "next": k2, "done": bool, "code": int, "cwd": ...}
#   POST /sh/kill                  -> stops the running command
#   POST /cleaner  {"kind": "msg" | "approve" | "hold", "text": "...", "item": "<step id>"}: the CLEANER tab's COMM window.
#        Appends to tools/cleaner_inbox.json (local only) for Claude to read; changes nothing on the PC by itself.
#   GET  /cleaner/apps              (token) installed programs from the Windows uninstall list (read-only)
#   POST /cleaner/uninstall {id}    (token) starts THAT program's own uninstaller on Nick's screen (Windows asks to approve)
#   POST /cleaner/settings          (token) opens Settings > Apps
#   GET  /cleaner/running           (token) uninstallers running right now (process name + window title), read-only
#   POST /runq/open {id}            (token) the CORE tab's RUN THESE window: opens ONE .bat that Claude listed in
#        tools/run_queue.json, only from the Desktop or the SynTHBloK folder (Windows still asks for admin if the .bat needs it)
#   POWERSHELL tab (all token-protected, this computer only):
#   POST /ps        {"cmd": "..."}  one PowerShell command at a time in the SynTHBloK folder (cd carries over)
#   GET  /ps/out?from=k            its output;  POST /ps/kill stops it
#   POST /ps/task   {"id": "..."}  runs a RUN THESE item's script (tools\*.ps1 named in tools/run_queue.json, or the
#        item's .bat from the Desktop / SynTHBloK, hidden: the tab's CMD LIVE panel shows it and takes its input) with its
#   POST /ps/task/in {"run", "text"}  types a line into a running .bat (Enter included);  POST /ps/task/stop {"run"}  stops it
#        output captured to tools/ps_runs/<run>.log; items marked "admin" start through the normal Windows admin prompt
#        (Nick clicks Yes) - nothing here gets around that prompt
#   GET  /ps/tasks                 the runs (state, exit code);  GET /ps/task/out?run=..&from=k  a run's output
#   POST /cmd {"cmd": "..."}, GET /cmd/out?from=k, POST /cmd/kill: the same console as Command Prompt (cmd.exe)
#   ADMIN console: POST /admin/start shows the normal Windows admin prompt (UAC); after Nick clicks Yes a hidden admin
#        PowerShell (tools/admin_worker.ps1) runs the commands typed in ADMIN mode: POST /admin {"cmd"}, GET /admin/out?from=k,
#        POST /admin/kill, POST /admin/stop, GET /admin/status. It closes itself after 15 min with no command, or on END.
#   SITE tab: POST /site {"text", "page"}  a message for Claude about liquidnick.com (tools/site_inbox.json, local only)
#        GET /site/check?url=...  fetches a liquidnick.com page (only that site) and returns its status, a hash of the
#        page (to spot changes) and whether the site lets other pages show it in a frame
#        POST /backdrop {"text"}  the same for the BACKDROP LAB BUILDER (tools/backdrop_inbox.json)
#        GET /site/local/<path>   serves the local working copy (Desktop\liquidnick_site_work) for the BUILDER preview
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
CLEAN_LOCK = threading.Lock()
# ---- SITE tab ----
SITE_DIR = os.path.join(os.environ.get('USERPROFILE', r'C:\Users\nickk'), 'Desktop', 'liquidnick_site_work')
SITE_HOST = re.compile(r'^(?:[\w-]+\.)*liquidnick\.com$', re.I)
SITE_LOCK = threading.Lock()
MIME = {'.html': 'text/html; charset=utf-8', '.htm': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript',
        '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
        '.gif': 'image/gif', '.webp': 'image/webp', '.ico': 'image/x-icon', '.woff': 'font/woff', '.woff2': 'font/woff2',
        '.mp3': 'audio/mpeg', '.wav': 'audio/wav', '.ogg': 'audio/ogg', '.mp4': 'video/mp4', '.txt': 'text/plain; charset=utf-8'}
def site_check(url):
    import hashlib, time as _t, urllib.request, urllib.error
    u = urllib.parse.urlparse(url)
    if u.scheme not in ('http', 'https') or not SITE_HOST.match(u.hostname or ''): return {'ok': False, 'error': 'only liquidnick.com pages'}
    t0 = _t.time()
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'SynTHBloK-workbench/1.0', 'Cache-Control': 'no-cache'})
        with urllib.request.urlopen(req, timeout=12) as r:
            body = r.read(4 << 20); st = r.status; h = r.headers
    except urllib.error.HTTPError as e:
        body = e.read(1 << 16) if hasattr(e, 'read') else b''; st = e.code; h = e.headers
    except Exception as e:
        return {'ok': False, 'error': str(e)[:200]}
    xfo = (h.get('X-Frame-Options') or '').strip(); csp = h.get('Content-Security-Policy') or ''
    fa = re.search(r'frame-ancestors([^;]*)', csp, re.I)
    blocked = bool(xfo) or bool(fa and "*" not in fa.group(1) and '127.0.0.1' not in fa.group(1))
    return {'ok': True, 'status': st, 'hash': hashlib.sha1(body).hexdigest()[:16], 'bytes': len(body), 'ms': int((_t.time() - t0) * 1000),
            'frameBlocked': blocked, 'xfo': xfo, 'frameAncestors': (fa.group(1).strip() if fa else '')}
def site_local(rel):
    rel = urllib.parse.unquote(rel.split('?')[0]).lstrip('/') or 'index.html'
    base = os.path.realpath(SITE_DIR); path = os.path.realpath(os.path.join(base, rel))
    if not (path == base or path.startswith(base + os.sep)): return None, None
    if os.path.isdir(path): path = os.path.join(path, 'index.html')
    if not os.path.isfile(path):
        if os.path.isfile(path + '.html'): path += '.html'
        else: return None, None
    with open(path, 'rb') as f: return f.read(), MIME.get(os.path.splitext(path)[1].lower(), 'application/octet-stream')
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

# ---- the POWERSHELL tab: a console like MSYS2's, plus scripts from the RUN THESE list with live output ----
PSX = r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe'
PS = {'id': 0, 'proc': None, 'buf': '', 'done': True, 'code': 0, 'cwd': REPO, 'lock': threading.Lock()}
def ps_start(cmd):
    with PS['lock']:
        if PS['proc'] and PS['proc'].poll() is None: return None
        PS['id'] += 1; PS['buf'] = ''; PS['done'] = False; PS['code'] = 0
        script = ("[Console]::OutputEncoding = [Text.Encoding]::UTF8; $ProgressPreference = 'SilentlyContinue'; "
                  "Set-Location -LiteralPath $env:PS_CWD; try { " + cmd + " | Out-String -Width 160 -Stream } catch { $_ | Out-String } ; "
                  "$c = if ($?) { 0 } else { 1 }; [Console]::Out.Write(\"`n\" + [char]1 + 'CWD:' + (Get-Location).Path); exit $c")
        env = dict(os.environ, PS_CWD=PS['cwd'])
        p = subprocess.Popen([PSX, '-NoProfile', '-NoLogo', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', script],
                             cwd=PS['cwd'], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        PS['proc'] = p; my = PS['id']
    def pump():
        for raw in iter(lambda: p.stdout.read1(4096) if hasattr(p.stdout, 'read1') else p.stdout.read(1), b''):
            with PS['lock']:
                if PS['id'] == my: PS['buf'] += raw.decode('utf-8', 'replace')
        code = p.wait()
        with PS['lock']:
            if PS['id'] != my: return
            i = PS['buf'].rfind(MARK)
            if i >= 0:
                PS['cwd'] = PS['buf'][i + len(MARK):].strip() or PS['cwd']
                PS['buf'] = PS['buf'][:i].rstrip('\n') + '\n'
            PS['code'] = code; PS['done'] = True
    threading.Thread(target=pump, daemon=True).start()
    return my

# ---- POWERSHELL tab: Command Prompt mode (cmd.exe, one command at a time, cd carries over) ----
CMD = {'id': 0, 'proc': None, 'buf': '', 'done': True, 'code': 0, 'cwd': REPO, 'lock': threading.Lock()}
def cmd_start(cmd):
    with CMD['lock']:
        if CMD['proc'] and CMD['proc'].poll() is None: return None
        CMD['id'] += 1; CMD['buf'] = ''; CMD['done'] = False; CMD['code'] = 0
        os.makedirs(os.path.join(HERE, 'ps_runs'), exist_ok=True)
        bat = os.path.join(HERE, 'ps_runs', 'cmd_console.cmd')
        with open(bat, 'w', encoding='utf-8', newline='\r\n') as f:
            f.write('@echo off\nchcp 65001 >nul\ncd /d "%s"\n%s\nset "__c=%%errorlevel%%"\necho(\necho \x01CWD:%%cd%%\nexit /b %%__c%%\n' % (CMD['cwd'], cmd))
        p = subprocess.Popen(['cmd.exe', '/d', '/c', bat], cwd=CMD['cwd'], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        CMD['proc'] = p; my = CMD['id']
    def pump():
        for raw in iter(lambda: p.stdout.read1(4096) if hasattr(p.stdout, 'read1') else p.stdout.read(1), b''):
            with CMD['lock']:
                if CMD['id'] == my: CMD['buf'] += raw.decode('utf-8', 'replace')
        code = p.wait()
        with CMD['lock']:
            if CMD['id'] != my: return
            i = CMD['buf'].rfind(MARK)
            if i >= 0:
                CMD['cwd'] = CMD['buf'][i + len(MARK):].strip() or CMD['cwd']
                CMD['buf'] = CMD['buf'][:i].rstrip('\r\n') + '\n'
            CMD['code'] = code; CMD['done'] = True
    threading.Thread(target=pump, daemon=True).start()
    return my

# ---- POWERSHELL tab: ADMIN mode. tools/admin_worker.ps1 runs elevated (started through the Windows admin prompt, so Nick
# has to click Yes) and watches tools/ps_runs/admin/ for the next command; each command runs from a small script this
# server writes, with its output appended to a log that /admin/out reads. The worker quits after 15 min idle or on END.
ADIR = os.path.join(HERE, 'ps_runs', 'admin')
ADM = {'id': 0, 'cwd': REPO, 'starting': 0, 'lock': threading.Lock()}
AEND = '\x01END:'
def admin_status():
    import time as _t
    try:
        with open(os.path.join(ADIR, 'state.json'), encoding='utf-8-sig') as f: st = json.load(f)
    except Exception: st = {}
    alive = _t.time() * 1000 - (st.get('alive') or 0) < 6000
    if alive: ADM['starting'] = 0
    return {'alive': alive, 'busy': alive and bool(st.get('busy')), 'starting': not alive and _t.time() - ADM['starting'] < 90}
def admin_start():
    import time as _t
    if admin_status()['alive']: return True
    os.makedirs(ADIR, exist_ok=True)
    for n in ('stop', 'kill', 'cmd.json'):
        try: os.remove(os.path.join(ADIR, n))
        except Exception: pass
    ADM['starting'] = _t.time()
    worker = os.path.join(HERE, 'admin_worker.ps1')
    args = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', worker, '-Dir', ADIR]
    q = "Start-Process -FilePath '%s' -Verb RunAs -WindowStyle Hidden -ArgumentList %s" % (PSX, ','.join("'%s'" % a.replace("'", "''") for a in args))
    subprocess.Popen([PSX, '-NoProfile', '-Command', q], creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    return True
def admin_log(i): return os.path.join(ADIR, 'out_%d.txt' % i)
def admin_read():
    try:
        with open(admin_log(ADM['id']), 'rb') as f: txt = f.read().decode('utf-8-sig', 'replace').replace('﻿', '')
    except Exception: txt = ''
    done, code = False, 0
    e = txt.find(AEND)
    if e >= 0:
        done = True
        try: code = int(txt[e + len(AEND):].strip().split()[0])
        except Exception: code = 1
        c = txt.find(MARK)
        if 0 <= c < e: ADM['cwd'] = txt[c + len(MARK):e].strip() or ADM['cwd']; txt = txt[:c]
        else: txt = txt[:e]
    else:
        c = txt.find(MARK)
        if c >= 0: txt = txt[:c]
    if not ADM['id']: done = True
    return txt, done, code
def admin_send(cmd):
    st = admin_status()
    if not st['alive']: return None, 'the admin console is not running: click ADMIN and approve the Windows prompt'
    with ADM['lock']:
        if ADM['id'] and not admin_read()[1]: return None, 'a command is still running (Ctrl+C stops it)'
        ADM['id'] += 1; i = ADM['id']
        wrap = os.path.join(ADIR, 'cmd_%d.ps1' % i); log = admin_log(i)
        lq = log.replace("'", "''")
        body = ("$ErrorActionPreference = 'Continue'; [Console]::OutputEncoding = [Text.Encoding]::UTF8; $ProgressPreference = 'SilentlyContinue'\n"
                "$log = '%s'\n'' | Out-File -LiteralPath $log -Encoding utf8\n"
                "Set-Location -LiteralPath '%s'\n$c = 0\n"
                "try { & {\n%s\n} *>&1 | Out-String -Width 160 -Stream | ForEach-Object { $_ | Out-File -LiteralPath $log -Append -Encoding utf8 } }\n"
                "catch { \"$_\" | Out-File -LiteralPath $log -Append -Encoding utf8; $c = 1 }\n"
                "([char]1 + 'CWD:' + (Get-Location).Path) | Out-File -LiteralPath $log -Append -Encoding utf8\n"
                "([char]1 + 'END:' + $c) | Out-File -LiteralPath $log -Append -Encoding utf8\n") % (lq, ADM['cwd'].replace("'", "''"), cmd)
        with open(wrap, 'w', encoding='utf-8-sig') as f: f.write(body)
        tmp = os.path.join(ADIR, 'cmd.json.tmp')
        with open(tmp, 'w', encoding='utf-8') as f: json.dump({'id': i, 'script': wrap}, f)
        os.replace(tmp, os.path.join(ADIR, 'cmd.json'))
    return i, None
def admin_flag(name):
    try: open(os.path.join(ADIR, name), 'w').close()
    except Exception: pass

RUNS_DIR = os.path.join(HERE, 'ps_runs')
RUNS = {}   # run id -> {id, item, name, admin, started, log, state}
def ps_task(rid):
    import time as _t
    try: items = json.load(open(os.path.join(HERE, 'run_queue.json'), encoding='utf-8-sig')).get('items', [])
    except Exception: return None, 'no run queue'
    it = next((x for x in items if x.get('id') == rid), None)
    if not it: return None, 'not in the run queue'
    if it.get('script'):
        kind = 'ps1'; sc = os.path.abspath(os.path.join(REPO, it['script']))
        if not sc.lower().startswith(os.path.join(REPO, 'tools').lower() + os.sep) or not sc.lower().endswith('.ps1') or not os.path.isfile(sc):
            return None, 'only .ps1 scripts in SynTHBloK\\tools can run here'
    else:   # a .bat from the queue: runs hidden with its output captured (CMD LIVE in the tab); "pause" just continues
        kind = 'bat'; sc = os.path.abspath(os.path.expandvars(it.get('path', '')))
        roots = [os.path.abspath(os.path.join(os.environ.get('USERPROFILE', ''), 'Desktop')), os.path.abspath(REPO)]
        if not sc.lower().endswith('.bat') or not os.path.isfile(sc) or not any(os.path.dirname(sc).lower().startswith(r.lower()) for r in roots):
            return None, 'only .bat files on the Desktop or in SynTHBloK'
    if any(r['item'] == rid and r['state'] in ('waiting', 'running', 'input') for r in RUNS.values()): return None, 'already running'
    os.makedirs(RUNS_DIR, exist_ok=True)
    run = _t.strftime('%Y%m%d-%H%M%S') + '-' + re.sub(r'[^\w-]', '', rid)[:30]
    log = os.path.join(RUNS_DIR, run + '.log'); wrap = os.path.join(RUNS_DIR, run + '.ps1')
    if kind == 'bat' and not it.get('admin'): return bat_start(run, rid, it, sc, log)
    with open(wrap, 'w', encoding='utf-8-sig') as f:
        f.write("$ErrorActionPreference = 'Continue'\n$log = '%s'\n" % log.replace("'", "''") +
                "'##START ' + (Get-Date -Format s) | Out-File -LiteralPath $log -Encoding utf8\n" +
                ("try { & '%s' *>&1 | ForEach-Object { \"$_\" | Out-File -LiteralPath $log -Append -Encoding utf8 } ; $c = if ($?) { 0 } else { 1 } }\n" % sc.replace("'", "''") if kind == 'ps1' else
                 "try { $null | & cmd.exe /d /c '%s' 2>&1 | ForEach-Object { \"$_\" | Out-File -LiteralPath $log -Append -Encoding utf8 } ; $c = [int]$LASTEXITCODE }\n" % sc.replace("'", "''")) +
                "catch { \"$_\" | Out-File -LiteralPath $log -Append -Encoding utf8; $c = 1 }\n"
                "'##END ' + $c | Out-File -LiteralPath $log -Append -Encoding utf8\n")
    open(log, 'w').close()
    args = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', wrap]
    if it.get('admin'):
        # the normal Windows admin prompt (UAC): Nick clicks Yes on his screen, or nothing runs
        q = "Start-Process -FilePath '%s' -Verb RunAs -WindowStyle Hidden -ArgumentList %s" % (PSX, ','.join("'%s'" % a.replace("'", "''") for a in args))
        subprocess.Popen([PSX, '-NoProfile', '-Command', q], creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    else:
        subprocess.Popen([PSX] + args, cwd=REPO, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    RUNS[run] = {'id': run, 'item': rid, 'kind': kind, 'name': it.get('name', rid), 'admin': bool(it.get('admin')), 'started': int(_t.time() * 1000), 'log': log, 'state': 'waiting'}
    try:
        done = json.load(open(RUND, encoding='utf-8'))
    except Exception: done = {}
    done[rid] = int(_t.time() * 1000)
    with open(RUND, 'w', encoding='utf-8') as f: json.dump(done, f)
    return run, None
# ---- CMD LIVE: a non-admin .bat runs hidden with its input and output connected to the tab, so Nick can answer
# "Press any key", Y/N questions and typed prompts right there (POST /ps/task/in), or stop it (POST /ps/task/stop).
BATP = {}   # run id -> {'proc', 'last' (time of the last output), 'lock'}
def bat_start(run, rid, it, sc, log):
    import time as _t
    p = subprocess.Popen(['cmd.exe', '/d', '/c', sc], cwd=os.path.dirname(sc), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    with open(log, 'w', encoding='utf-8') as f: f.write('##START ' + _t.strftime('%Y-%m-%dT%H:%M:%S') + '\n')
    BATP[run] = {'proc': p, 'last': _t.time(), 'lock': threading.Lock()}
    def pump():
        for raw in iter(lambda: p.stdout.read1(4096) if hasattr(p.stdout, 'read1') else p.stdout.read(1), b''):
            BATP[run]['last'] = _t.time()
            with open(log, 'a', encoding='utf-8', newline='') as f: f.write(raw.decode('utf-8', 'replace').replace('\r\n', '\n'))
        code = p.wait()
        with open(log, 'a', encoding='utf-8') as f: f.write('\n##END %d\n' % code)
    threading.Thread(target=pump, daemon=True).start()
    RUNS[run] = {'id': run, 'item': rid, 'kind': 'bat', 'live': True, 'name': it.get('name', rid), 'admin': False,
                 'started': int(_t.time() * 1000), 'log': log, 'state': 'running'}
    try: done = json.load(open(RUND, encoding='utf-8'))
    except Exception: done = {}
    done[rid] = int(_t.time() * 1000)
    with open(RUND, 'w', encoding='utf-8') as f: json.dump(done, f)
    return run, None
def bat_input(run, text):
    b = BATP.get(run)
    if not b or b['proc'].poll() is not None: return 'that .bat is not running'
    try:
        with b['lock']: b['proc'].stdin.write((text + '\r\n').encode('utf-8')); b['proc'].stdin.flush()
    except Exception as e: return 'could not send it: %s' % e
    return None
def bat_stop(run):
    b = BATP.get(run)
    if b and b['proc'].poll() is None: subprocess.run(['taskkill', '/T', '/F', '/PID', str(b['proc'].pid)], capture_output=True)
def ps_runs():
    out = []
    for r in sorted(RUNS.values(), key=lambda r: -r['started']):
        try:
            with open(r['log'], 'rb') as f: f.seek(max(0, os.path.getsize(r['log']) - 300)); tail = f.read().decode('utf-8-sig', 'replace')
        except Exception: tail = ''
        m = re.search(r'##END (\d+)', tail)
        if m: r['state'] = 'done' if m.group(1) == '0' else 'failed'; r['code'] = int(m.group(1))
        elif os.path.exists(r['log']) and os.path.getsize(r['log']) > 0:
            r['state'] = 'running'
            b = BATP.get(r['id'])
            if b and b['proc'].poll() is None and __import__('time').time() - b['last'] > 1.0 and tail and not tail.endswith('\n'): r['state'] = 'input'
        elif r['admin'] and r['state'] == 'waiting': r['state'] = 'needs-ok'
        out.append({k: v for k, v in r.items() if k != 'log'})
    return out

RACK_EXES = [r'C:\Program Files\VCV\Rack2Pro\Rack.exe', r'C:\Program Files\VCV\Rack2Free\Rack.exe']
def rack_launch():
    """The workbench's OPEN RACK button: start Rack (from this helper, a normal user process, so it uses the real
    plugins folder) or, if it's already running, bring its window to the front."""
    if rack_running():
        ps = "$p = Get-Process Rack -ErrorAction SilentlyContinue | Select-Object -First 1; if ($p) { (New-Object -ComObject WScript.Shell).AppActivate($p.Id) | Out-Null }"
        subprocess.Popen(['powershell', '-NoProfile', '-Command', ps], creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        return {'ok': True, 'did': 'focused'}
    exe = next((e for e in RACK_EXES if os.path.isfile(e)), None)
    if not exe: return {'ok': False, 'error': 'Rack.exe not found in Program Files\\VCV'}
    subprocess.Popen([exe], cwd=os.path.dirname(exe))
    return {'ok': True, 'did': 'started', 'exe': exe}
def rack_running():
    r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Rack.exe', '/NH'], capture_output=True, text=True)
    return 'Rack.exe' in r.stdout


# ---- CLEANER: installed programs + their own uninstallers (Windows only) ----
# ---- CORE: RUN THESE (.bat files Claude asked Nick to run) ----
RUNQ, RUND = os.path.join(HERE, 'run_queue.json'), os.path.join(HERE, 'run_done.json')
def runq_open(rid):
    import time as _t
    try: items = json.load(open(RUNQ, encoding='utf-8-sig')).get('items', [])
    except Exception: return None, 'no run queue'
    it = next((x for x in items if x.get('id') == rid), None)
    if not it: return None, 'not in the run queue'
    path = os.path.abspath(os.path.expandvars(it.get('path', '')))
    roots = [os.path.abspath(os.path.join(os.environ.get('USERPROFILE', ''), 'Desktop')), os.path.abspath(REPO)]
    if not path.lower().endswith('.bat') or not os.path.isfile(path) or not any(os.path.dirname(path).lower().startswith(r.lower()) for r in roots):
        return path, 'only .bat files on the Desktop or in SynTHBloK'
    os.startfile(path)
    try: done = json.load(open(RUND, encoding='utf-8'))
    except Exception: done = {}
    done[rid] = int(_t.time() * 1000)
    with open(RUND, 'w', encoding='utf-8') as f: json.dump(done, f)
    return path, None

UNINST_ROOTS = []
if os.name == 'nt':
    import winreg, ctypes
    UNINST_ROOTS = [('HKLM', winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'),
                    ('HKLM32', winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'),
                    ('HKCU', winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Windows\CurrentVersion\Uninstall')]
def _rv(k, n):
    try: return winreg.QueryValueEx(k, n)[0]
    except OSError: return None
def list_apps():
    out = {}
    for tag, hive, path in UNINST_ROOTS:
        try: root = winreg.OpenKey(hive, path)
        except OSError: continue
        i = 0
        while True:
            try: sub = winreg.EnumKey(root, i)
            except OSError: break
            i += 1
            try: k = winreg.OpenKey(root, sub)
            except OSError: continue
            name = _rv(k, 'DisplayName'); us = _rv(k, 'UninstallString')
            if not name or not us or _rv(k, 'SystemComponent') == 1 or _rv(k, 'ParentKeyName') or re.match(r'^KB\d', name): continue
            key = name.strip().lower()
            if key in out: continue
            out[key] = {'id': tag + '|' + sub, 'name': name.strip(), 'pub': (_rv(k, 'Publisher') or '').strip(), 'ver': str(_rv(k, 'DisplayVersion') or ''),
                        'mb': round((_rv(k, 'EstimatedSize') or 0) / 1024, 1), 'date': str(_rv(k, 'InstallDate') or ''), 'msi': bool(re.match(r'(?i)msiexec', us.strip()))}
    return sorted(out.values(), key=lambda a: a['name'].lower())
UNINST_PROC = re.compile(r'^(msiexec|unins\d*|uninst\w*|uninstall\w*|au_|un_a|un_b|setup|vs_installer\w*|vs_setup\w*|installer|cleanup)\.exe$', re.I)
_RUN_CACHE = {'t': 0, 'v': []}
def running_uninstallers():
    import time as _t
    if _t.time() - _RUN_CACHE['t'] < 2: return _RUN_CACHE['v']
    out = []
    try:
        txt = subprocess.run(['tasklist', '/v', '/fo', 'csv', '/nh'], capture_output=True, text=True, timeout=8,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout
        import csv, io
        for row in csv.reader(io.StringIO(txt)):
            if len(row) < 9 or not UNINST_PROC.match(row[0]): continue
            title = row[8].strip()
            if row[0].lower() == 'msiexec.exe' and title in ('', 'N/A'): continue   # the idle Windows Installer service
            out.append({'proc': row[0], 'pid': row[1], 'title': '' if title == 'N/A' else title})
    except Exception:
        pass
    _RUN_CACHE.update(t=_t.time(), v=out)
    return out
def run_uninstall(app_id):
    tag, _, sub = app_id.partition('|')
    for t, hive, path in UNINST_ROOTS:
        if t != tag: continue
        try: k = winreg.OpenKey(hive, path + '\\' + sub)
        except OSError: return None, 'not found (already uninstalled?)'
        name = _rv(k, 'DisplayName'); us = (_rv(k, 'UninstallString') or '').strip()
        if not us: return name, 'no uninstaller listed'
        m = re.match(r'(?i)msiexec(\.exe)?\s.*?(\{[0-9A-F-]+\})', us)
        if m: exe, args = 'msiexec.exe', '/x ' + m.group(2)
        else:
            m = re.match(r'^"([^"]+)"\s*(.*)$', us) or re.match(r'^(.+?\.exe)\s*(.*)$', us, re.I)
            exe, args = (m.group(1), m.group(2)) if m else (us, '')
        # ShellExecute so the uninstaller can ask Windows for permission (a normal launch would fail when it needs admin)
        rc = ctypes.windll.shell32.ShellExecuteW(None, 'open', exe, args or None, None, 1)
        return name, (None if rc > 32 else 'could not start (code %d)' % rc)
    return None, 'unknown id'

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
        if self.path.startswith('/ps/'):
            if self.headers.get('Origin') not in ORIGINS or self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False})
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            frm = int((q.get('from') or ['0'])[0])
            if self.path.startswith('/ps/out'):
                with PS['lock']:
                    buf = PS['buf']; i = buf.find(MARK)
                    if i >= 0: buf = buf[:i]
                    return self.reply(200, {'ok': True, 'id': PS['id'], 'text': buf[frm:], 'next': len(buf), 'done': PS['done'], 'code': PS['code'], 'cwd': PS['cwd']})
            if self.path.startswith('/ps/tasks'): return self.reply(200, {'ok': True, 'runs': ps_runs()})
            if self.path.startswith('/ps/task/out'):
                r = RUNS.get((q.get('run') or [''])[0])
                if not r: return self.reply(404, {'ok': False})
                try:
                    with open(r['log'], 'rb') as f: txt = f.read().decode('utf-8-sig', 'replace').replace('\ufeff', '')
                except Exception: txt = ''
                return self.reply(200, {'ok': True, 'text': txt[frm:], 'next': len(txt), 'state': next((x['state'] for x in ps_runs() if x['id'] == r['id']), '')})
        if self.path.startswith('/cmd/') or self.path.startswith('/admin/'):
            if self.headers.get('Origin') not in ORIGINS or self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False})
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            frm = int((q.get('from') or ['0'])[0])
            if self.path.startswith('/cmd/out'):
                with CMD['lock']:
                    buf = CMD['buf']; i = buf.find(MARK)
                    if i >= 0: buf = buf[:i]
                    return self.reply(200, {'ok': True, 'id': CMD['id'], 'text': buf[frm:], 'next': len(buf), 'done': CMD['done'], 'code': CMD['code'], 'cwd': CMD['cwd']})
            if self.path.startswith('/admin/status'): return self.reply(200, dict(admin_status(), ok=True, cwd=ADM['cwd']))
            if self.path.startswith('/admin/out'):
                txt, done, code = admin_read()
                if not done and not admin_status()['alive']: done, code = True, 1   # the admin console closed mid-command
                return self.reply(200, {'ok': True, 'id': ADM['id'], 'text': txt[frm:], 'next': len(txt), 'done': done, 'code': code, 'cwd': ADM['cwd']})
        if self.path.startswith('/site/local/') or self.path == '/site/local':
            # the BUILDER preview: the local working copy, read-only, this computer only
            data, ctype = site_local(self.path[len('/site/local'):])
            if data is None:
                self.send_response(404); self.send_header('Content-Type', 'text/plain'); self.end_headers(); self.wfile.write(b'not in liquidnick_site_work'); return
            self.send_response(200); self.send_header('Content-Type', ctype); self.send_header('Cache-Control', 'no-store'); self.end_headers(); self.wfile.write(data); return
        if self.path.startswith('/site/check'):
            if self.headers.get('Origin') not in ORIGINS: return self.reply(403, {'ok': False})
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            return self.reply(200, site_check((q.get('url') or [''])[0]))
        if self.path == '/cleaner/running':
            if self.headers.get('Origin') not in ORIGINS or self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False})
            return self.reply(200, {'ok': True, 'running': running_uninstallers()})
        if self.path == '/cleaner/apps':
            if self.headers.get('Origin') not in ORIGINS or self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False})
            try: return self.reply(200, {'ok': True, 'apps': list_apps()})
            except Exception as e: return self.reply(500, {'ok': False, 'error': str(e)})
        self.reply(404, {'ok': False})
    def do_POST(self):
        if self.path not in ('/open', '/gen', '/sh', '/sh/kill', '/cleaner', '/cleaner/uninstall', '/cleaner/settings', '/runq/open', '/ps', '/ps/kill', '/ps/task', '/ps/task/in', '/ps/task/stop', '/site', '/backdrop', '/cmd', '/cmd/kill', '/admin', '/admin/kill', '/admin/start', '/admin/stop', '/rack/launch'): return self.reply(404, {'ok': False})
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
        if self.path == '/rack/launch': return self.reply(200, rack_launch())
        if self.path == '/gen': return self.gen()
        if self.path == '/cleaner': return self.cleaner()
        if self.path == '/site': return self.site_note()
        if self.path == '/backdrop': return self.site_note('backdrop_inbox.json')
        if self.path in ('/ps', '/ps/kill', '/ps/task', '/ps/task/in', '/ps/task/stop'):
            if self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False, 'error': 'no token'})
            if self.path == '/ps/kill':
                p = PS['proc']
                if p and p.poll() is None: subprocess.run(['taskkill', '/T', '/F', '/PID', str(p.pid)], capture_output=True)
                return self.reply(200, {'ok': True})
            try: req = json.loads(self.rfile.read(min(int(self.headers.get('Content-Length') or 0), 8000)) or b'{}')
            except Exception: return self.reply(400, {'ok': False, 'error': 'bad request'})
            if self.path == '/ps/task/in':
                err = bat_input(str(req.get('run') or ''), str(req.get('text') or '')[:2000])
                return self.reply(200 if not err else 409, {'ok': not err, 'error': err})
            if self.path == '/ps/task/stop':
                bat_stop(str(req.get('run') or '')); return self.reply(200, {'ok': True})
            if self.path == '/ps/task':
                run, err = ps_task(str(req.get('id') or ''))
                return self.reply(200 if run else 409, {'ok': bool(run), 'run': run, 'error': err})
            cmd = str(req.get('cmd') or '')
            if not cmd.strip() or len(cmd) > 4000: return self.reply(400, {'ok': False, 'error': 'empty'})
            i = ps_start(cmd)
            if i is None: return self.reply(409, {'ok': False, 'error': 'a command is still running (Ctrl+C stops it)'})
            return self.reply(200, {'ok': True, 'id': i, 'cwd': PS['cwd']})
        if self.path in ('/cmd', '/cmd/kill', '/admin', '/admin/kill', '/admin/start', '/admin/stop'):
            if self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False, 'error': 'no token'})
            if self.path == '/cmd/kill':
                p = CMD['proc']
                if p and p.poll() is None: subprocess.run(['taskkill', '/T', '/F', '/PID', str(p.pid)], capture_output=True)
                return self.reply(200, {'ok': True})
            if self.path == '/admin/kill': admin_flag('kill'); return self.reply(200, {'ok': True})
            if self.path == '/admin/stop': admin_flag('stop'); return self.reply(200, {'ok': True})
            if self.path == '/admin/start': admin_start(); return self.reply(200, dict(admin_status(), ok=True))
            try: req = json.loads(self.rfile.read(min(int(self.headers.get('Content-Length') or 0), 8000)) or b'{}')
            except Exception: return self.reply(400, {'ok': False, 'error': 'bad request'})
            cmd = str(req.get('cmd') or '')
            if not cmd.strip() or len(cmd) > 4000: return self.reply(400, {'ok': False, 'error': 'empty'})
            if self.path == '/cmd':
                i = cmd_start(cmd)
                if i is None: return self.reply(409, {'ok': False, 'error': 'a command is still running (Ctrl+C stops it)'})
                return self.reply(200, {'ok': True, 'id': i, 'cwd': CMD['cwd']})
            i, err = admin_send(cmd)
            if i is None: return self.reply(409, {'ok': False, 'error': err})
            return self.reply(200, {'ok': True, 'id': i, 'cwd': ADM['cwd']})
        if self.path == '/runq/open':
            if self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False, 'error': 'no token'})
            try: rid = str(json.loads(self.rfile.read(min(int(self.headers.get('Content-Length') or 0), 2000)) or b'{}').get('id') or '')
            except Exception: return self.reply(400, {'ok': False, 'error': 'bad request'})
            path, err = runq_open(rid)
            return self.reply(200 if not err else 409, {'ok': not err, 'path': path, 'error': err})
        if self.path in ('/cleaner/uninstall', '/cleaner/settings'):
            if self.headers.get('X-Token') != TOKEN: return self.reply(403, {'ok': False, 'error': 'no token'})
            if self.path == '/cleaner/settings':
                os.startfile('ms-settings:appsfeatures'); return self.reply(200, {'ok': True})
            try: app_id = str(json.loads(self.rfile.read(min(int(self.headers.get('Content-Length') or 0), 2000)) or b'{}').get('id') or '')
            except Exception: return self.reply(400, {'ok': False, 'error': 'bad request'})
            name, err = run_uninstall(app_id)
            self._clean_note({'kind': 'msg', 'item': 'uninstall', 'text': f"UNINSTALL started from CLEANER: {name or app_id}" + (f" - {err}" if err else '')})
            return self.reply(200 if not err else 409, {'ok': not err, 'name': name, 'error': err})
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
    def cleaner(self, forced=None):
        if forced is not None: req = forced
        else:
            try:
                req = json.loads(self.rfile.read(min(int(self.headers.get('Content-Length') or 0), 20000)) or b'{}')
            except Exception:
                return self.reply(400, {'ok': False, 'error': 'bad request'})
        return self._clean_note(req, reply=True)
    def _clean_note(self, req, reply=False):
        kind = req.get('kind') if req.get('kind') in ('msg', 'approve', 'hold') else 'msg'
        text = str(req.get('text') or '')[:2000]; item = str(req.get('item') or '')[:80]
        if not text.strip() and kind == 'msg': return self.reply(400, {'ok': False, 'error': 'empty'})
        path = os.path.join(HERE, 'cleaner_inbox.json')
        with CLEAN_LOCK:
            try:
                with open(path, encoding='utf-8') as f: box = json.load(f)
            except Exception:
                box = {'messages': []}
            import time as _t
            m = {'id': len(box['messages']) + 1, 't': int(_t.time() * 1000), 'kind': kind, 'item': item, 'text': text}
            box['messages'].append(m)
            with open(path + '.tmp', 'w', encoding='utf-8') as f: json.dump(box, f, indent=1)
            os.replace(path + '.tmp', path)
        if reply: self.reply(200, {'ok': True, 'id': m['id']})
    def site_note(self, fname='site_inbox.json'):
        try: req = json.loads(self.rfile.read(min(int(self.headers.get('Content-Length') or 0), 20000)) or b'{}')
        except Exception: return self.reply(400, {'ok': False, 'error': 'bad request'})
        text = str(req.get('text') or '')[:4000]; page = str(req.get('page') or '')[:300]
        if not text.strip(): return self.reply(400, {'ok': False, 'error': 'empty'})
        path = os.path.join(HERE, fname)
        with SITE_LOCK:
            try:
                with open(path, encoding='utf-8') as f: box = json.load(f)
            except Exception: box = {'messages': []}
            import time as _t
            m = {'id': len(box['messages']) + 1, 't': int(_t.time() * 1000), 'text': text, 'page': page}
            box['messages'].append(m)
            with open(path + '.tmp', 'w', encoding='utf-8') as f: json.dump(box, f, indent=1)
            os.replace(path + '.tmp', path)
        self.reply(200, {'ok': True, 'id': m['id']})
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
