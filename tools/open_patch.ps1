# Opens a patch in Rack: closes Rack the way its X button does (so it writes its autosave and log END, like
# build_and_reload.ps1), keeps a copy of the patch that was open, then starts Rack on the given patch BEHIND your
# open windows (it never takes the focus) and takes pictures for the phone's RACK tab (rack_view.ps1).
#   .\tools\open_patch.ps1 patches\DualFilter_Synth_Sweep.vcv
# The copies of what was open: %LOCALAPPDATA%\Rack2\autosave-before-open\patch-<date-time>.json, one per run.
param(
    [Parameter(Mandatory = $true)][string]$Patch,
    [int]$CloseTimeout = 30,
    [string]$Rack = 'C:\Program Files\VCV\Rack2Pro\Rack.exe'
)
$path = (Resolve-Path $Patch).Path
$user = Join-Path $env:LOCALAPPDATA 'Rack2'

foreach ($p in @(Get-Process Rack -ErrorAction SilentlyContinue)) {
    Write-Host "Closing Rack (pid $($p.Id))..."
    if (-not $p.CloseMainWindow()) { Write-Host 'Rack has no window to close. Close it yourself, then run this again.'; exit 2 }
    if (-not $p.WaitForExit($CloseTimeout * 1000)) { Write-Host "Rack did not close within $CloseTimeout s. Check Rack for an open dialog. It was not killed."; exit 2 }
}
$keep = Join-Path $user 'autosave-before-open'
New-Item -ItemType Directory -Force $keep | Out-Null
Copy-Item (Join-Path $user 'autosave\patch.json') (Join-Path $keep ("patch-{0:yyyyMMdd-HHmmss}.json" -f (Get-Date))) -ErrorAction SilentlyContinue
Write-Host "Kept the patch that was open in $keep"

# Rack opens BEHIND the windows that are already open: started minimized without taking the focus (window
# style 7), then rack_view.ps1 sets it down behind everything and hands the focus back to the window you were in.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class OP { [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow(); }
'@
$prev = [OP]::GetForegroundWindow()
$sh = New-Object -ComObject WScript.Shell
$sh.CurrentDirectory = Split-Path $Rack
$sh.Run("`"$Rack`" `"$path`"", 7, $false) | Out-Null
Write-Host "Rack opening $path (behind your windows)"
Start-Process powershell -WindowStyle Hidden -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$(Join-Path $PSScriptRoot 'rack_view.ps1')`"", '-Prev', ([int64]$prev)
