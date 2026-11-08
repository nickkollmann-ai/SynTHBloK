# PC telemetry for the workbench (CLAUDE · THINKING's HARDWARE band and the top bar's PC pill).
# Every 2 s writes tools/pc_stats.json: CPU load per thread + clock, ACPI thermal zones, the NVIDIA GPU (nvidia-smi),
# RAM, C: space, disk + network activity, and Rack's own CPU and RAM. No admin rights, nothing to install.
# Also: the top processes (CPU and memory), every drive's read / write and free space, Tailscale peers and a
# ping to liquidnick.com (every 30 s), boot time and startup programs (every 5 min), and per-core temperatures
# and fans when LibreHardwareMonitor (or OpenHardwareMonitor) is running (they publish to WMI).
# tools/open_workbench.ps1 starts it hidden; a second copy exits at once (named mutex).
#   powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File tools\pc_stats.ps1
param([switch]$Once)   # -Once: one sample, then exit (a test)
$ErrorActionPreference = 'SilentlyContinue'
$created = $false
if ($Once) { $created = $true } else {
$mutex = New-Object System.Threading.Mutex($true, 'Global\SynTHBloK_pc_stats', [ref]$created)
}
if (-not $created) { exit 0 }

$out = Join-Path $PSScriptRoot 'pc_stats.json'
$tmp = "$out.tmp"
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$cs = Get-CimInstance Win32_ComputerSystem
$os = Get-CimInstance Win32_OperatingSystem
$boot = $os.LastBootUpTime
$baseMHz = [double]$cpu.MaxClockSpeed
$smi = (Get-Command nvidia-smi -ErrorAction SilentlyContinue).Source
if (-not $smi -and (Test-Path 'C:\Windows\System32\nvidia-smi.exe')) { $smi = 'C:\Windows\System32\nvidia-smi.exe' }
$counters = @(
    '\Processor(*)\% Processor Time',
    '\Processor Information(_Total)\% Processor Performance',
    '\PhysicalDisk(_Total)\% Disk Time',
    '\Network Interface(*)\Bytes Received/sec',
    '\Network Interface(*)\Bytes Sent/sec',
    '\Thermal Zone Information(*)\High Precision Temperature',
    '\LogicalDisk(*)\Disk Read Bytes/sec',
    '\LogicalDisk(*)\Disk Write Bytes/sec'
)
$procPrev = @{}; $procPrevT = $null
$slowT = [datetime]::MinValue; $verySlowT = [datetime]::MinValue
$drives = @(); $tailscale = $null; $pingMs = $null; $bootInfo = $null
$tsExe = @("$env:ProgramFiles\Tailscale\tailscale.exe", "${env:ProgramFiles(x86)}\Tailscale\tailscale.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
$rackPrev = $null; $rackPrevT = $null

while ($true) {
    $s = (Get-Counter -Counter $counters -SampleInterval 2 -MaxSamples 1).CounterSamples
    $cores = @($s | Where-Object { $_.Path -match '\\processor\((\d+)\)\\' } |
        Sort-Object { [int]([regex]::Match($_.Path, '\\processor\((\d+)\)').Groups[1].Value) } |
        ForEach-Object { [math]::Round([math]::Min(100, $_.CookedValue), 1) })
    $total = ($s | Where-Object { $_.Path -match '\\processor\(_total\)\\' } | Select-Object -First 1).CookedValue
    $perf = ($s | Where-Object { $_.Path -match 'processor performance' } | Select-Object -First 1).CookedValue
    $disk = ($s | Where-Object { $_.Path -match 'disk time' } | Select-Object -First 1).CookedValue
    $rx = ($s | Where-Object { $_.Path -match 'bytes received' } | Measure-Object CookedValue -Sum).Sum
    $tx = ($s | Where-Object { $_.Path -match 'bytes sent' } | Measure-Object CookedValue -Sum).Sum
    # ACPI zones report tenths of a kelvin; HPTZ is HP's board/CPU zone, TZ00 the chassis
    $zones = @($s | Where-Object { $_.Path -match 'thermal zone' } | ForEach-Object {
        $n = [regex]::Match($_.Path, '\((.*)\)').Groups[1].Value -replace '^.*tz\.', ''
        [ordered]@{ name = $n.ToUpper(); c = [math]::Round($_.CookedValue / 10 - 273.15, 1) } })

    # every drive: read / write now (KB/s), free space every 10 s
    $dio = @{}
    $s | Where-Object { $_.Path -match '\\logicaldisk\(([a-z]:)\)\\disk (read|write) bytes' } | ForEach-Object {
        $m = [regex]::Match($_.Path, '\\logicaldisk\(([a-z]:)\)\\disk (read|write)')
        $d = $m.Groups[1].Value.ToUpper(); if (-not $dio[$d]) { $dio[$d] = @{ r = 0; w = 0 } }
        if ($m.Groups[2].Value -eq 'read') { $dio[$d].r = $_.CookedValue } else { $dio[$d].w = $_.CookedValue } }
    $nowT = Get-Date
    if (($nowT - $slowT).TotalSeconds -ge 10) {
        $drives = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | ForEach-Object {
            [ordered]@{ d = $_.DeviceID; label = $_.VolumeName; freeGB = [math]::Round($_.FreeSpace / 1GB, 1); sizeGB = [math]::Round($_.Size / 1GB, 1) } })
    }
    $driveOut = @($drives | ForEach-Object { $io = $dio[$_.d]
        [ordered]@{ d = $_.d; label = $_.label; freeGB = $_.freeGB; sizeGB = $_.sizeGB
                    rKBs = $(if ($io) { [math]::Round($io.r / 1KB, 1) } else { 0 }); wKBs = $(if ($io) { [math]::Round($io.w / 1KB, 1) } else { 0 }) } })
    # the top processes: CPU share since the last sample (grouped by name), and memory
    $procs = Get-Process | Where-Object { $_.Id -ne 0 -and $_.ProcessName -ne 'Idle' }
    $cpuBy = @{}; $memBy = @{}; $cur = @{}
    foreach ($p in $procs) {
        $tps = $null; try { $tps = $p.TotalProcessorTime.TotalSeconds } catch {}
        if ($tps -ne $null) { $cur[$p.Id] = $tps; if ($procPrevT -and $procPrev.ContainsKey($p.Id)) { $cpuBy[$p.ProcessName] += ($tps - $procPrev[$p.Id]) } }
        $memBy[$p.ProcessName] += $p.WorkingSet64
    }
    $span = if ($procPrevT) { ($nowT - $procPrevT).TotalSeconds } else { 0 }
    $procPrev = $cur; $procPrevT = $nowT
    $topCpu = @(); if ($span -gt 0) {
        $topCpu = @($cpuBy.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 5 | ForEach-Object {
            [ordered]@{ n = $_.Key; p = [math]::Round([math]::Max(0, $_.Value / $span / $cpu.NumberOfLogicalProcessors * 100), 1) } }) }
    $topMem = @($memBy.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 5 | ForEach-Object {
        [ordered]@{ n = $_.Key; mb = [math]::Round($_.Value / 1MB) } })
    # every 30 s: Tailscale's peers, and a ping to the website
    if (($nowT - $slowT).TotalSeconds -ge 30) {
        $slowT = $nowT
        if ($tsExe) {
            $tj = & $tsExe status --json 2>$null | Out-String
            if ($tj) { try {
                $st = $tj | ConvertFrom-Json
                $peers = @($st.Peer.PSObject.Properties | ForEach-Object { [ordered]@{ n = ($_.Value.HostName); on = [bool]$_.Value.Online } })
                $tailscale = [ordered]@{ self = $st.Self.HostName; state = $st.BackendState; online = @($peers | Where-Object { $_.on }).Count; total = $peers.Count; peers = $peers }
            } catch {} }
        }
        try { $r = (New-Object System.Net.NetworkInformation.Ping).Send('liquidnick.com', 1500)
              $pingMs = $(if ($r.Status -eq 'Success') { $r.RoundtripTime } else { -1 }) } catch { $pingMs = -1 }
    } elseif ($slowT -eq [datetime]::MinValue) { $slowT = $nowT }
    # every 5 min: boot time and the startup programs
    if (($nowT - $verySlowT).TotalMinutes -ge 5) {
        $verySlowT = $nowT
        $bootSec = $null
        try { $ev = Get-WinEvent -FilterHashtable @{ LogName = 'Microsoft-Windows-Diagnostics-Performance/Operational'; Id = 100 } -MaxEvents 1 -ErrorAction Stop
              $bt = ([xml]$ev.ToXml()).Event.EventData.Data | Where-Object { $_.Name -eq 'BootTime' } | Select-Object -First 1
              if ($bt) { $bootSec = [math]::Round([double]$bt.'#text' / 1000, 1) } } catch {}
        $startup = @(Get-CimInstance Win32_StartupCommand | Select-Object -ExpandProperty Name -Unique)
        $bootInfo = [ordered]@{ at = ([DateTimeOffset]$boot).ToUnixTimeMilliseconds(); sec = $bootSec; count = $startup.Count; items = $startup }
    }
    # per-core temperatures and fans (LibreHardwareMonitor / OpenHardwareMonitor, if running)
    $sensors = $null
    foreach ($ns in @('root/LibreHardwareMonitor', 'root/OpenHardwareMonitor')) {
        $sn = Get-CimInstance -Namespace $ns -ClassName Sensor -ErrorAction SilentlyContinue
        if ($sn) {
            $sensors = [ordered]@{ src = ($ns -replace 'root/', '')
                temps = @($sn | Where-Object { $_.SensorType -eq 'Temperature' } | Sort-Object Name | ForEach-Object { [ordered]@{ n = $_.Name; c = [math]::Round($_.Value, 1) } })
                fans = @($sn | Where-Object { $_.SensorType -eq 'Fan' } | ForEach-Object { [ordered]@{ n = $_.Name; rpm = [math]::Round($_.Value) } }) }
            break
        }
    }

    $o = Get-CimInstance Win32_OperatingSystem
    $c = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='C:'"

    $gpu = $null
    if ($smi) {
        $l = & $smi --query-gpu=name,temperature.gpu,utilization.gpu,memory.used,memory.total,power.draw,power.limit,fan.speed,clocks.gr,clocks.max.gr --format=csv,noheader,nounits 2>$null | Select-Object -First 1
        if ($l) {
            $f = $l -split ',\s*'
            $num = { param($x) $v = 0.0; if ([double]::TryParse($x, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$v)) { $v } else { $null } }
            $gpu = [ordered]@{ name = $f[0]; temp = & $num $f[1]; load = & $num $f[2]; memUsed = & $num $f[3]; memTotal = & $num $f[4]
                               power = & $num $f[5]; powerMax = & $num $f[6]; fan = & $num $f[7]; mhz = & $num $f[8]; mhzMax = & $num $f[9] }
        }
    }

    # Rack: its share of the whole CPU since the last sample, and its working set
    $rack = $null
    $rp = Get-Process Rack -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($rp) {
        $now = Get-Date
        if ($rackPrev -ne $null -and $rackPrevT) {
            $pct = ($rp.TotalProcessorTime.TotalSeconds - $rackPrev) / ($now - $rackPrevT).TotalSeconds / $cpu.NumberOfLogicalProcessors * 100
        } else { $pct = $null }
        $rackPrev = $rp.TotalProcessorTime.TotalSeconds; $rackPrevT = $now
        $rcpu = $null; if ($pct -ne $null) { $rcpu = [math]::Round([math]::Max(0, $pct), 1) }
        $rack = [ordered]@{ cpu = $rcpu; ramMB = [math]::Round($rp.WorkingSet64 / 1MB); title = $rp.MainWindowTitle }
    } else { $rackPrev = $null; $rackPrevT = $null }

    $j = [ordered]@{
        t = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        host = $env:COMPUTERNAME; model = $(if ($cs.Model -like "$($cs.Manufacturer)*") { $cs.Model } else { "$($cs.Manufacturer) $($cs.Model)".Trim() }); os = $os.Caption
        uptime = [math]::Round(((Get-Date) - $boot).TotalSeconds)
        cpu = [ordered]@{ name = ($cpu.Name -replace '\(R\)|\(TM\)|CPU|\s+@.*$', '' -replace '\s+', ' ').Trim(); cores = $cpu.NumberOfCores; threads = $cpu.NumberOfLogicalProcessors
                          load = [math]::Round($total, 1); mhz = [math]::Round($baseMHz * $perf / 100); baseMHz = $baseMHz; perThread = $cores }
        zones = $zones
        gpu = $gpu
        ram = [ordered]@{ totalMB = [math]::Round($o.TotalVisibleMemorySize / 1024); usedMB = [math]::Round(($o.TotalVisibleMemorySize - $o.FreePhysicalMemory) / 1024) }
        disk = [ordered]@{ cFreeGB = [math]::Round($c.FreeSpace / 1GB, 1); cSizeGB = [math]::Round($c.Size / 1GB, 1); busy = [math]::Round([math]::Min(100, $disk), 1) }
        net = [ordered]@{ rxKBs = [math]::Round($rx / 1KB, 1); txKBs = [math]::Round($tx / 1KB, 1) }
        rack = $rack
        procs = [ordered]@{ cpu = $topCpu; mem = $topMem }
        drives = $driveOut
        netx = [ordered]@{ tailscale = $tailscale; pingMs = $pingMs }
        boot = $bootInfo
        sensors = $sensors
    }
    [IO.File]::WriteAllText($tmp, ($j | ConvertTo-Json -Depth 5 -Compress), (New-Object Text.UTF8Encoding $false))
    Move-Item -Force $tmp $out
    if ($Once) { break }
}
