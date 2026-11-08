# Restarts the workbench's helpers so new code takes effect: tools/rack_open_server.py (OPEN IN RACK, GEN, the
# MSYS2 terminal) and tools/pc_stats.ps1 (THIS PC). Both start hidden, as tools/open_workbench.ps1 starts them.
# Run it through the job runner (jobs\NNNN-feeds.job holding "tools/restart_feeds.ps1").
$ErrorActionPreference = 'SilentlyContinue'
$repo = Split-Path -Parent $PSScriptRoot
$py = 'C:\msys64\mingw64\bin\python3.exe'

Get-CimInstance Win32_Process -Filter "Name like 'python%'" | Where-Object { $_.CommandLine -match 'rack_open_server\.py' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "stopped rack_open_server.py (pid $($_.ProcessId))" }
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -match 'pc_stats\.ps1' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "stopped pc_stats.ps1 (pid $($_.ProcessId))" }
Start-Sleep -Milliseconds 800

Start-Process $py -ArgumentList (Join-Path $repo 'tools\rack_open_server.py') -WorkingDirectory $repo -WindowStyle Hidden
Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', (Join-Path $PSScriptRoot 'pc_stats.ps1') -WindowStyle Hidden
Start-Sleep -Seconds 4
$ok1 = (Get-CimInstance Win32_Process -Filter "Name like 'python%'" | Where-Object { $_.CommandLine -match 'rack_open_server\.py' }) -ne $null
$ok2 = (Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -match 'pc_stats\.ps1' }) -ne $null
"rack_open_server.py running: $ok1"
"pc_stats.ps1 running: $ok2"
"token file: $(Test-Path (Join-Path $PSScriptRoot '.shell_token'))"
