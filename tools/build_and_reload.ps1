# Builds SynTHBloK, then installs it into Rack and restarts Rack if it was open.
#   1. Build (make all). On a failed build Rack is left alone.
#   2. If Rack is open, close it the way the window's X button does, so Rack saves its autosave and
#      writes "END" at the end of log.txt. Rack shows its "Rack crashed during the last session" warning
#      at the next start when log.txt does not end with END, so Rack is never killed here.
#   3. Copy the plugin in (make devinstall). Windows locks a loaded plugin DLL, so this runs after Rack exits.
#   4. Screenshot every SynTHBloK module as Rack draws it into web-images/screenshots (tools/rack_shots.ps1,
#      about 10 s, on a private Rack folder). -NoShots skips it.
#   5. Reopen Rack if it was open (it reloads the autosaved patch). -Open starts Rack even if it was closed.
#      tools/rack_view.ps1 then brings Rack to the front and saves pictures of it for the phone's RACK tab.
# When OBS is open with its WebSocket server on, OBS records the workbench from the start of the build until
# a few seconds after it ends (tools/obs_record.ps1). -NoRecord skips it.
param(
    [switch]$Open,
    [switch]$NoShots,
    [switch]$NoRecord,
    [int]$RecordTail = 8,
    [int]$CloseTimeout = 30,
    [string]$Rack = 'C:\Program Files\VCV\Rack2Pro\Rack.exe'
)

$bash = 'C:\msys64\usr\bin\bash.exe'
$log = Join-Path $env:LOCALAPPDATA 'Rack2\log.txt'
$env:MSYSTEM = 'MINGW64'
$env:CHERE_INVOKING = '1'

function Make([string]$target) {
    # Out-Host shows make's output live; without it the output would become this function's return value.
    & $bash -lc "cd ~/SynTHBloK && export RACK_DIR=~/Rack-SDK && make -j4 $target 2>&1 | tee build.log; exit `${PIPESTATUS[0]}" | Out-Host
    return $LASTEXITCODE -eq 0
}

$obs = Join-Path $PSScriptRoot 'obs_record.ps1'
if (-not $NoRecord) { & $obs start -Mine }
try {

if (-not (Make 'all')) { Write-Host 'BUILD FAILED - see build.log. Rack was not touched.'; exit 1 }

$procs = @(Get-Process Rack -ErrorAction SilentlyContinue)
$wasOpen = $procs.Count -gt 0
foreach ($p in $procs) {
    Write-Host "Closing Rack (pid $($p.Id))..."
    if (-not $p.CloseMainWindow()) { Write-Host 'Rack has no window to close. Close it yourself, then run this again.'; exit 2 }
    if (-not $p.WaitForExit($CloseTimeout * 1000)) {
        # Still open: most likely a dialog (unsaved patch, module prompt) is waiting on the user.
        Write-Host "Rack did not close within $CloseTimeout s. Check Rack for an open dialog. It was not killed."
        exit 2
    }
}
if ($wasOpen) {
    $last = (Get-Content $log -Tail 1 -ErrorAction SilentlyContinue)
    if ($last -ne 'END') { Write-Host 'Warning: log.txt does not end with END, so Rack may show its crash warning.' }
}

if (-not (Make 'devinstall')) { Write-Host 'INSTALL FAILED - see build.log.'; exit 1 }
Write-Host 'BUILD OK - plugin installed.'
if (-not $NoShots) { & (Join-Path $PSScriptRoot 'rack_shots.ps1') -Rack $Rack }

if ($wasOpen -or $Open) {
    Start-Process -FilePath $Rack -WorkingDirectory (Split-Path $Rack)
    Write-Host 'Rack reopened.'
    # Rack to the front (for AnyDesk) and pictures of it for the phone workbench's RACK tab
    Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$(Join-Path $PSScriptRoot 'rack_view.ps1')`""
}

} finally {
    # Stopped in the background so the build returns now while the recording catches Rack coming back up.
    if (-not $NoRecord) {
        Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$obs`"", 'stop', '-Mine', '-Delay', $RecordTail
    }
}
