# Opens the SynTHBloK workbench when a Claude Code session starts (SessionStart hook in .claude/settings.local.json).
# Starts whatever isn't running yet, each in its own hidden process so it outlives this script:
#   - the workbench server on 127.0.0.1:8766, and on the Tailscale address for the phone
#   - tools/chat_feed.py (CHAT), tools/patch_feed.py (PATCH MAP + PATCHES) and tools/rack_open_server.py (OPEN IN RACK)
#   - tools/pc_stats.ps1 (THIS PC: CPU, GPU, temps for THINKING and the top bar)
#   - tools/claude_jobs.ps1 (Claude runs the repo's scripts hidden, via jobs\)
# then opens the workbench in Firefox unless a Firefox window already shows it.
$ErrorActionPreference = 'SilentlyContinue'
$repo = Split-Path -Parent $PSScriptRoot
$py = 'C:\msys64\mingw64\bin\python3.exe'
$done = @()

function Up($url) { try { (Invoke-WebRequest $url -UseBasicParsing -TimeoutSec 2).StatusCode -eq 200 } catch { $false } }
function Serve($ip) {
    if (-not (Up "http://${ip}:8766/tools/design_watch.html")) {
        Start-Process $py -ArgumentList '-m', 'http.server', '8766', '--bind', $ip, '--directory', $repo -WindowStyle Hidden
        $script:done += "server $ip"
    }
}

Serve '127.0.0.1'
$ts = 'C:\Program Files\Tailscale\tailscale.exe'
if (Test-Path $ts) { $tip = (& $ts ip -4 | Select-Object -First 1); if ($tip) { Serve $tip.Trim() } }

$running = (Get-CimInstance Win32_Process -Filter "Name like 'python%'").CommandLine -join "`n"
foreach ($feed in 'chat_feed.py', 'patch_feed.py', 'rack_open_server.py') {
    if ($running -notmatch [regex]::Escape($feed)) {
        Start-Process $py -ArgumentList (Join-Path $repo "tools\$feed") -WorkingDirectory $repo -WindowStyle Hidden
        $done += $feed
    }
}

# PC telemetry for THINKING's HARDWARE band and the top bar's PC pill (a second copy exits by itself)
$psRunning = (Get-CimInstance Win32_Process -Filter "Name='powershell.exe'").CommandLine -join "`n"
if ($psRunning -notmatch 'pc_stats\.ps1') {
    Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', (Join-Path $PSScriptRoot 'pc_stats.ps1') -WindowStyle Hidden
    $done += 'pc_stats.ps1'
}

# CLAUDE JOBS: Claude runs the repo's scripts hidden through jobs\ (no windows over your work)
if ($psRunning -notmatch 'claude_jobs\.ps1') {
    Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', (Join-Path $PSScriptRoot 'claude_jobs.ps1') -WindowStyle Hidden
    $done += 'claude_jobs.ps1'
}

$ff = Get-Process firefox | Where-Object Path | Select-Object -First 1
if (-not (Get-Process firefox | Where-Object { $_.MainWindowTitle -like 'SynTHBloK Workbench*' })) {
    $exe = if ($ff) { $ff.Path } else { 'C:\Program Files\Mozilla Firefox\firefox.exe' }
    Start-Sleep -Milliseconds 600
    & $exe 'http://127.0.0.1:8766/tools/design_watch.html'
    $done += 'opened in Firefox'
}
if ($done) { "Workbench: " + ($done -join ', ') } else { 'Workbench already open and running.' }
