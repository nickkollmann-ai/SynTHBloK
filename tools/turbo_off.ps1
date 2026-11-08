# Turns turbo boost OFF in the current Windows power plan ("Processor performance boost mode" = Disabled).
# The i7-8700 then stays at or below its 3192 MHz base clock instead of boosting to ~4.6 GHz, which runs much cooler.
# The setting is hidden, so powercfg /query may not show it: we set it, then check the CPU speed instead.
# Undo: tools\turbo_off_undo.ps1
powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PERFBOOSTMODE 0
if ($LASTEXITCODE) { "powercfg could not set boost mode (code $LASTEXITCODE)."; exit 1 }
powercfg /setdcvalueindex SCHEME_CURRENT SUB_PROCESSOR PERFBOOSTMODE 0
powercfg /setactive SCHEME_CURRENT
"Boost mode set to 0 (off). Checking the CPU speed for 10 seconds..."
$samples = Get-Counter '\Processor Information(_Total)\% Processor Performance' -SampleInterval 2 -MaxSamples 5
$mhz = [math]::Round(3192 * ($samples.CounterSamples | Measure-Object CookedValue -Maximum).Maximum / 100)
"Highest CPU speed seen: $mhz MHz (base 3192)"
if ($mhz -gt 3300) { "Turbo still looks ON. Windows or HP firmware is overriding it." } else { "Turbo is OFF." }
Set-Content -Path (Join-Path $PSScriptRoot 'turbo_off.txt') -Value "$(Get-Date -Format s)  boost mode -> 0, highest CPU $mhz MHz"
