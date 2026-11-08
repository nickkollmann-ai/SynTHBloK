# Makes "SynTHBloK Workbench" a program: shortcuts in the Start menu and on the desktop (pin either to the taskbar).
# Each runs tools\workbench.vbs, which starts the workbench server, its feeds and the app window with no console.
# -Startup also adds it to Windows sign-in, 90 s after logging in so it never slows the boot itself.
# -Remove takes every shortcut away again.
param([switch]$Startup, [switch]$Remove)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$vbs = Join-Path $here 'workbench.vbs'
$ico = Join-Path $here 'workbench.ico'
$places = @{
    'Start menu' = Join-Path ([Environment]::GetFolderPath('Programs')) 'SynTHBloK Workbench.lnk'
    'Desktop'    = Join-Path ([Environment]::GetFolderPath('Desktop')) 'SynTHBloK Workbench.lnk'
}
$startLnk = Join-Path ([Environment]::GetFolderPath('Startup')) 'SynTHBloK Workbench (delayed).lnk'
if ($Remove) {
    foreach ($p in @($places.Values + $startLnk)) { if (Test-Path $p) { Remove-Item $p; "removed $p" } }
    exit 0
}
$ws = New-Object -ComObject WScript.Shell
function Make($path, $arguments) {
    $l = $ws.CreateShortcut($path)
    $l.TargetPath = "$env:WINDIR\System32\wscript.exe"
    $l.Arguments = $arguments
    $l.WorkingDirectory = Split-Path -Parent $here
    $l.IconLocation = "$ico,0"
    $l.Description = 'SynTHBloK Workbench: the sandbox, CLAUDE THINKING, the PC readout and the MSYS2 terminal'
    $l.Save()
}
foreach ($k in $places.Keys) { Make $places[$k] "`"$vbs`""; "made the $k shortcut: $($places[$k])" }
if ($Startup) {
    # a tiny delayed start: wait 90 s after sign-in, then open (so the boot itself stays as fast as it can be)
    $delay = Join-Path $here 'workbench_delayed.vbs'
    Set-Content -Path $delay -Encoding ASCII -Value @(
        "' Opens the SynTHBloK Workbench 90 s after sign-in (the Startup folder shortcut points here).",
        'WScript.Sleep 90000',
        'Set sh = CreateObject("WScript.Shell")',
        'dir = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName)',
        'sh.Run "wscript.exe """ & dir & "\workbench.vbs""", 0, False')
    Make $startLnk "`"$delay`""
    "made the sign-in shortcut (90 s delay): $startLnk"
}
