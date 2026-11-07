# Screenshots every SynTHBloK module as Rack draws it (the panel SVG plus everything the C++ draws) into
# web-images/screenshots/<slug>.png, where the workbench shows them next to the panel SVGs.
#   Rack's screenshot mode (-t) shoots every installed plugin, which takes minutes with the full library. So it
#   runs on a private Rack user folder (-u) that holds only SynTHBloK: a few seconds, and the normal Rack
#   folder (settings, autosave, log.txt) is never touched. It can run while the normal Rack is open.
#   Zoom 3 matches the screenshots already there. build_and_reload.ps1 runs this after each install.
param(
    [string]$Rack = 'C:\Program Files\VCV\Rack2Pro\Rack.exe',
    [int]$Zoom = 3,
    [int]$Timeout = 120
)

$repo = Split-Path $PSScriptRoot
$user = Join-Path $env:LOCALAPPDATA 'Rack2-shots'
$plugins = Join-Path $user 'plugins-win-x64'
$installedRoot = Join-Path $env:LOCALAPPDATA 'Rack2\plugins-win-x64'
$installed = Join-Path $installedRoot 'SynTHBloK'
$shotsRoot = Join-Path $user 'screenshots'   # one folder per plugin: SynTHBloK (MTX pack), SynTHBloK-Kick, ...
$dest = Join-Path $repo 'web-images\screenshots'

if (-not (Test-Path (Join-Path $installed 'plugin.dll'))) { Write-Host 'SHOTS SKIPPED - SynTHBloK is not installed in Rack.'; exit 1 }
New-Item -ItemType Directory -Force $plugins, $dest | Out-Null
# only the SynTHBloK plugins (MTX pack + singles) in the private folder (Rack unpacks Fundamental into it at each
# start: removed here, so it is never more than that)
Get-ChildItem $plugins | ForEach-Object { Remove-Item $_.FullName -Recurse -Force }
$ours = @(Get-ChildItem $installedRoot -Directory | Where-Object { $_.Name -eq 'SynTHBloK' -or $_.Name -like 'SynTHBloK-*' })
foreach ($d in $ours) {
    Copy-Item $d.FullName (Join-Path $plugins $d.Name) -Recurse
    $old = Join-Path $shotsRoot $d.Name
    if (Test-Path $old) { Remove-Item $old -Recurse -Force }
}

$t0 = Get-Date
$p = Start-Process -FilePath $Rack -ArgumentList '-u', "`"$user`"", '-t', $Zoom -WorkingDirectory (Split-Path $Rack) -WindowStyle Minimized -PassThru
if (-not $p.WaitForExit($Timeout * 1000)) {
    Stop-Process -Id $p.Id -Force   # the private Rack only; its folder holds nothing to save
    Write-Host "SHOTS FAILED - Rack screenshot mode did not finish in $Timeout s."
    exit 1
}

$n = 0
foreach ($f in @($ours | ForEach-Object { Get-ChildItem (Join-Path $shotsRoot $_.Name) -Filter *.png -ErrorAction SilentlyContinue })) {
    Copy-Item $f.FullName (Join-Path $dest $f.Name) -Force
    $n++
}
if ($n -eq 0) { Write-Host "SHOTS FAILED - no screenshots made (see $user\log.txt)."; exit 1 }
Write-Host ("SHOTS OK - {0} modules in web-images/screenshots ({1:0} s)." -f $n, ((Get-Date) - $t0).TotalSeconds)
