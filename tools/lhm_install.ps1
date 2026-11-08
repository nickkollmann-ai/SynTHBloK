# Installs LibreHardwareMonitor (+ the PawnIO driver it needs for Intel CPU temps and board fans), starts it,
# and restarts tools/pc_stats.ps1 so the workbench's CORE tab gets fan RPMs and CPU temps.
# Runs elevated (started by "CORE - install LibreHardwareMonitor.bat" on the Desktop). Log: tools\lhm_install.txt
$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'lhm_install.txt'
function Say($m) { $m; Add-Content -Path $log -Value ("{0:HH:mm:ss} {1}" -f (Get-Date), $m) }
Set-Content -Path $log -Value "LibreHardwareMonitor setup $(Get-Date)"
$wg = @('install', '-e', '--accept-source-agreements', '--accept-package-agreements', '--silent', '--id')
Say 'installing PawnIO driver...'
winget @wg namazso.PawnIO 2>&1 | Select-Object -Last 3 | ForEach-Object { Say "  $_" }
Say 'installing LibreHardwareMonitor...'
winget @wg LibreHardwareMonitor.LibreHardwareMonitor 2>&1 | Select-Object -Last 3 | ForEach-Object { Say "  $_" }

$exe = Get-ChildItem -Path "$env:LOCALAPPDATA\Microsoft\WinGet\Packages", "$env:ProgramFiles", "${env:ProgramFiles(x86)}" -Recurse -Filter 'LibreHardwareMonitor.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match 'LibreHardwareMonitor' } | Select-Object -First 1
if (-not $exe) { Say 'LibreHardwareMonitor.exe not found - install did not finish'; Start-Sleep 15; exit 1 }
Say "found $($exe.FullName)"
if (-not (Get-Process LibreHardwareMonitor -ErrorAction SilentlyContinue)) { Start-Process $exe.FullName -WindowStyle Minimized; Say 'started LibreHardwareMonitor (as admin)' }

# wait for its sensors to show up in WMI
$ok = $false
for ($i = 0; $i -lt 20 -and -not $ok; $i++) { Start-Sleep 2; $ok = [bool](Get-CimInstance -Namespace root/LibreHardwareMonitor -ClassName Sensor -ErrorAction SilentlyContinue | Select-Object -First 1) }
if ($ok) {
    $sn = Get-CimInstance -Namespace root/LibreHardwareMonitor -ClassName Sensor
    $fans = @($sn | Where-Object { $_.SensorType -eq 'Fan' }); $temps = @($sn | Where-Object { $_.SensorType -eq 'Temperature' })
    Say "sensors live: $($temps.Count) temperatures, $($fans.Count) fans"
    $fans | ForEach-Object { Say ("  fan  {0}  {1:N0} rpm  ({2})" -f $_.Name, $_.Value, $_.Identifier) }
    $temps | Where-Object { $_.Identifier -match 'cpu' } | Select-Object -First 3 | ForEach-Object { Say ("  temp {0}  {1:N0} C" -f $_.Name, $_.Value) }
} else { Say 'no WMI sensors (normal for LibreHardwareMonitor 0.9.x) - CORE reads its web server instead' }
try {
    $tree = Invoke-RestMethod -Uri 'http://127.0.0.1:8085/data.json' -TimeoutSec 3
    $all = New-Object System.Collections.ArrayList
    $walk = { param($n) foreach ($k in @($n.Children)) { if ($k.SensorId) { [void]$all.Add($k) }; & $walk $k } }; & $walk $tree
    $f = @($all | Where-Object { $_.SensorId -match '/fan/' }); $t = @($all | Where-Object { $_.SensorId -match '/temperature/' })
    Say "web server OK (port 8085): $($t.Count) temperatures, $($f.Count) fans"
    $f | ForEach-Object { Say ("  fan  {0}  {1}  ({2})" -f $_.Text, $_.Value, $_.SensorId) }
} catch { Say 'web server NOT answering on port 8085 - in LibreHardwareMonitor: Options > Remote Web Server > Run (allow it if Windows Firewall asks)' }

# restart only the PC stats feed (the helper server is left alone)
Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -match 'pc_stats\.ps1' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Start-Sleep -Milliseconds 600
Start-Process powershell -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', (Join-Path $PSScriptRoot 'pc_stats.ps1') -WindowStyle Hidden
Say 'restarted pc_stats.ps1 - open the CORE tab'
Start-Sleep 8
