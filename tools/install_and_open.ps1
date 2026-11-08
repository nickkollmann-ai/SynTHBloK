# Install the MTX pack and open a test patch, in one go: closes Rack the way its X button does (it writes its
# autosave; never killed), builds + installs (make devinstall, output in build.log), then opens the patch with
# tools\open_patch.ps1 (which keeps a copy of what was open).
#   .\tools\install_and_open.ps1 -Patch patches\BACKDROP_alone_test.vcv
param([Parameter(Mandatory = $true)][string]$Patch, [int]$CloseTimeout = 30)
$repo = Split-Path -Parent $PSScriptRoot
foreach ($p in @(Get-Process Rack -ErrorAction SilentlyContinue)) {
    "Closing Rack (pid $($p.Id))..."
    if (-not $p.CloseMainWindow()) { 'Rack has no window to close. Close it yourself, then run this again.'; exit 2 }
    if (-not $p.WaitForExit($CloseTimeout * 1000)) { "Rack did not close within $CloseTimeout s (an open dialog?). Not killed."; exit 2 }
}
$env:MSYSTEM = 'MINGW64'; $env:CHERE_INVOKING = '1'
& 'C:\msys64\usr\bin\bash.exe' -lc 'cd ~/SynTHBloK && export RACK_DIR=~/Rack-SDK SYNTHBLOK_DEV=1 && make -j4 devinstall > build.log 2>&1; r=$?; tail -n 3 build.log; exit $r'
if ($LASTEXITCODE -ne 0) { 'BUILD FAILED - see build.log (Rack left closed)'; exit 1 }
'BUILD OK'
& (Join-Path $PSScriptRoot 'open_patch.ps1') -Patch (Join-Path $repo $Patch)
