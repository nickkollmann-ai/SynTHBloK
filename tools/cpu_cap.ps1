# Caps the CPU at 95% in the current Windows power plan (plugged in + battery).
# Below 100% Windows stops turbo boost, which is where most of the 207-210 F heat comes from.
# Undo: tools\cpu_cap_undo.ps1 (sets it back to 100%).
$cap = 95
$before = (powercfg /query SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX | Select-String 'Current AC Power Setting Index') -replace '.*:\s*0x',''
"Maximum processor state before: $([Convert]::ToInt32($before,16))%"
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX $cap
powercfg /setdcvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX $cap
powercfg /setactive SCHEME_CURRENT
$after = (powercfg /query SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMAX | Select-String 'Current AC Power Setting Index') -replace '.*:\s*0x',''
$after = [Convert]::ToInt32($after,16)
"Maximum processor state now:    $after%"
if ($after -ne $cap) { "Windows did not take the new setting."; exit 1 }
Set-Content -Path (Join-Path $PSScriptRoot 'cpu_cap.txt') -Value "$(Get-Date -Format s)  capped at $cap% (was $([Convert]::ToInt32($before,16))%)"
"Done. Watch the CPU temperature on the LIBRE tab; it should drop within a minute."
