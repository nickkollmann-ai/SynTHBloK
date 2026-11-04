# After a build reopens Rack: brings the Rack window to the front (so AnyDesk shows it) and saves pictures of it
# for the phone workbench's RACK tab: tools/rack_live.png plus tools/rack_live.json ({"t": ms, "w", "h"}).
# Runs hidden in the background (build_and_reload.ps1 starts it), so the build returns at once.
#   -Shots  how many pictures (default 4), -Every seconds between them (default 6), the first once Rack's window is up.
# Both files are local only (listed in .git/info/exclude).
param([int]$Shots = 4, [int]$Every = 6, [int]$Wait = 60)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class RV {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
'@
[RV]::SetProcessDPIAware() | Out-Null
$dir = $PSScriptRoot
$png = Join-Path $dir 'rack_live.png'
$deadline = (Get-Date).AddSeconds($Wait)
do {
    $p = Get-Process Rack -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if (-not $p) { Start-Sleep -Milliseconds 500 }
} until ($p -or (Get-Date) -gt $deadline)
if (-not $p) { exit 1 }
Start-Sleep -Seconds 4    # the patch loads
for ($i = 0; $i -lt $Shots; $i++) {
    $h = $p.MainWindowHandle
    if ([RV]::IsIconic($h)) { [RV]::ShowWindow($h, 9) | Out-Null }   # SW_RESTORE
    # an Alt tap lets a background process hand the foreground over
    [RV]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [RV]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    [RV]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 400
    $r = New-Object RV+RECT
    if ([RV]::GetWindowRect($h, [ref]$r)) {
        $w = $r.R - $r.L; $hh = $r.B - $r.T
        if ($w -gt 100 -and $hh -gt 100) {
            $bmp = New-Object System.Drawing.Bitmap $w, $hh
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
            $tmp = "$png.tmp"
            $bmp.Save($tmp, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
            Move-Item $tmp $png -Force
            $ms = [long]((Get-Date).ToUniversalTime() - [datetime]'1970-01-01').TotalMilliseconds
            "{`"t`":$ms,`"w`":$w,`"h`":$hh}" | Set-Content (Join-Path $dir 'rack_live.json') -Encoding ascii
        }
    }
    if ($i -lt $Shots - 1) { Start-Sleep -Seconds $Every }
}
