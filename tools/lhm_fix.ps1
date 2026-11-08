# Gets LibreHardwareMonitor feeding the workbench again and keeps it that way after restarts:
#  - turns on its Remote Web Server (port 8085, what LIBRE/CORE read), start minimized, minimize to tray
#  - makes a Windows task that starts it (as admin, needed for CPU temps) every time Nick signs in
#  - starts it now and checks that port 8085 answers
# Runs as admin (from the POWERSHELL tab). Log: tools\lhm_fix.txt.  Remove the autostart: Unregister-ScheduledTask LibreHardwareMonitor
$log = Join-Path $PSScriptRoot 'lhm_fix.txt'
function Say($m) { $m; Add-Content -Path $log -Value ("{0:HH:mm:ss} {1}" -f (Get-Date), $m) }
Set-Content -Path $log -Value "LibreHardwareMonitor fix $(Get-Date -Format s)"
$exe = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter 'LibreHardwareMonitor.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) { $exe = Get-ChildItem 'C:\Users\nickk\AppData\Local\Microsoft\WinGet\Packages' -Recurse -Filter 'LibreHardwareMonitor.exe' -ErrorAction SilentlyContinue | Select-Object -First 1 }
if (-not $exe) { Say 'LibreHardwareMonitor.exe not found.'; exit 1 }
Say "found $($exe.FullName)"
$p = Get-Process LibreHardwareMonitor -ErrorAction SilentlyContinue
Say ("was it running? " + $(if ($p) { 'yes' } else { 'NO (that is why LIBRE froze)' }))
if ($p) { $p | Stop-Process -Force; Start-Sleep 2 }   # it rewrites its settings file when it closes, so close it before editing

# settings file next to the exe
$cfg = Join-Path $exe.DirectoryName 'LibreHardwareMonitor.config'
if (Test-Path $cfg) { Copy-Item $cfg "$cfg.bak" -Force; [xml]$x = Get-Content $cfg -Raw }
else { [xml]$x = '<?xml version="1.0" encoding="utf-8"?><configuration><appSettings /></configuration>' }
$as = $x.configuration.SelectSingleNode('appSettings'); if (-not $as) { $as = $x.configuration.AppendChild($x.CreateElement('appSettings')) }
foreach ($kv in @(@('runWebServerMenuItem', 'true'), @('listenerPort', '8085'), @('startMinMenuItem', 'true'), @('minTrayMenuItem', 'true'), @('minCloseMenuItem', 'true'))) {
    $n = $as.SelectSingleNode("add[@key='$($kv[0])']")
    if (-not $n) { $n = $as.AppendChild($x.CreateElement('add')); $n.SetAttribute('key', $kv[0]) }
    $n.SetAttribute('value', $kv[1])
}
$x.Save($cfg); Say 'settings: web server ON (port 8085), start minimized, minimize/close to tray'

# start at every sign-in, as admin, no prompt
$act = New-ScheduledTaskAction -Execute $exe.FullName -WorkingDirectory $exe.DirectoryName
$trg = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$prn = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
$set = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit 0
Register-ScheduledTask -TaskName 'LibreHardwareMonitor' -Action $act -Trigger $trg -Principal $prn -Settings $set -Force | Out-Null
Say 'autostart: Windows task "LibreHardwareMonitor" starts it at every sign-in'

# start it now and test the connection the workbench uses
Start-ScheduledTask -TaskName 'LibreHardwareMonitor'
$ok = $false
for ($i = 0; $i -lt 15 -and -not $ok; $i++) {
    Start-Sleep 2
    try { $t = Invoke-RestMethod -Uri 'http://127.0.0.1:8085/data.json' -TimeoutSec 2; $ok = $true } catch {}
}
if ($ok) { Say 'connection OK: port 8085 is answering. LIBRE should update within 30 seconds.' }
else { Say 'port 8085 is still NOT answering. If Windows Firewall asked, click Allow; otherwise open LibreHardwareMonitor (tray icon) > Options > Remote Web Server > Run.' ; exit 1 }
