# Opens the workbench as its own app window: Microsoft Edge in app mode, so no tabs, no address bar, no browser
# toolbar, just the workbench in a window whose title bar takes the workbench's dark colour (theme-color).
# Its own Edge profile (tools\.wb-app), so the window keeps its size and place and never mixes with your browsing.
# Run directly, or through the job runner (jobs\NNNN-app.job holding "tools/workbench_app.ps1").
$ErrorActionPreference = 'SilentlyContinue'
$url = 'http://127.0.0.1:8766/tools/design_watch.html'
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
$open = Get-Process msedge | Where-Object { $_.MainWindowTitle -like 'SynTHBloK Workbench*' } | Select-Object -First 1
if ($open) { "the workbench app is already open"; exit 0 }
if (-not $edge) { "Edge not found: opening in the default browser"; Start-Process $url; exit 0 }
$profile = Join-Path $PSScriptRoot '.wb-app'
Start-Process $edge -ArgumentList "--app=$url", "--user-data-dir=`"$profile`"", '--window-size=1680,1000', '--no-first-run', '--no-default-browser-check'
"opened the workbench app"
