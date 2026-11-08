# The POWERSHELL tab's ADMIN console. Started by the workbench helper (rack_open_server.py) through the normal Windows
# admin prompt, so it only runs after Nick clicks Yes. It runs hidden, as admin, and watches its folder for the next
# command: cmd.json names a script the helper wrote (cmd_<n>.ps1, which logs to out_<n>.txt). Only scripts inside its
# own folder are run. Ctrl+C in the tab drops a "kill" file; END drops a "stop" file. It also quits after 15 minutes
# with no command, and writes state.json (alive time, busy) every few hundred ms so the tab can show it's on.
param([Parameter(Mandatory)][string]$Dir)
$ErrorActionPreference = 'Continue'
$state = Join-Path $Dir 'state.json'; $cmdf = Join-Path $Dir 'cmd.json'
$kill = Join-Path $Dir 'kill'; $stop = Join-Path $Dir 'stop'
$psx = "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe"
$idleMax = [TimeSpan]::FromMinutes(15)
$last = 0; $idle = Get-Date
function Beat($busy) {
    $j = '{"alive":' + [DateTimeOffset]::Now.ToUnixTimeMilliseconds() + ',"busy":' + $(if ($busy) { 'true' } else { 'false' }) + ',"pid":' + $PID + '}'
    try { [IO.File]::WriteAllText($state, $j) } catch {}
}
try {
    while ($true) {
        if (Test-Path $stop) { Remove-Item $stop -Force -ErrorAction SilentlyContinue; break }
        if ((Get-Date) - $idle -gt $idleMax) { break }
        Beat $false
        $job = $null
        if (Test-Path $cmdf) { try { $job = Get-Content $cmdf -Raw | ConvertFrom-Json } catch {} }
        if ($job -and [int]$job.id -gt $last) {
            $last = [int]$job.id
            $sc = [IO.Path]::GetFullPath([string]$job.script)
            if ($sc.StartsWith([IO.Path]::GetFullPath($Dir) + '\', 'OrdinalIgnoreCase') -and $sc.EndsWith('.ps1') -and (Test-Path $sc)) {
                Remove-Item $kill -Force -ErrorAction SilentlyContinue
                $p = Start-Process -FilePath $psx -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-NonInteractive', '-WindowStyle', 'Hidden', '-File', $sc -WindowStyle Hidden -PassThru
                while (-not $p.HasExited) {
                    Beat $true
                    if ((Test-Path $kill) -or (Test-Path $stop)) {
                        & taskkill.exe /T /F /PID $p.Id 2>&1 | Out-Null
                        Remove-Item $kill -Force -ErrorAction SilentlyContinue
                        $log = $sc -replace 'cmd_(\d+)\.ps1$', 'out_$1.txt'
                        ("stopped (Ctrl+C)`n" + [char]1 + 'END:1') | Out-File -LiteralPath $log -Append -Encoding utf8
                        break
                    }
                    Start-Sleep -Milliseconds 300
                }
                Remove-Item $sc -Force -ErrorAction SilentlyContinue
            }
            $idle = Get-Date
        }
        Start-Sleep -Milliseconds 300
    }
} finally {
    try { [IO.File]::WriteAllText($state, '{"alive":0,"busy":false}') } catch {}
}
