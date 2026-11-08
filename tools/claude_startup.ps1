# Starts Claude (the desktop app) and the SynTHBloK Workbench when Nick signs in to Windows.
# Claude: a shortcut in the Startup folder. Workbench: tools\make_shortcuts.ps1 -Startup (opens 90 s after sign-in).
# Remove: delete "Claude.lnk" / "SynTHBloK Workbench (delayed).lnk" from shell:startup.  Log: tools\claude_startup.txt
$log = Join-Path $PSScriptRoot 'claude_startup.txt'
$out = New-Object System.Collections.Generic.List[string]
$startup = [Environment]::GetFolderPath('Startup'); $lnk = Join-Path $startup 'Claude.lnk'
$ws = New-Object -ComObject WScript.Shell
$exe = @("$env:LOCALAPPDATA\AnthropicClaude\claude.exe", "$env:LOCALAPPDATA\Programs\Claude\Claude.exe", "$env:ProgramFiles\Claude\Claude.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
$l = $ws.CreateShortcut($lnk)
if ($exe) { $l.TargetPath = $exe; $l.WorkingDirectory = Split-Path $exe; $out.Add("Claude: $exe") }
else {
    $app = Get-StartApps | Where-Object { $_.Name -eq 'Claude' } | Select-Object -First 1
    if (-not $app) { $out.Add('Claude app not found - no shortcut made'); $out | Set-Content $log; $out; exit 1 }
    $l.TargetPath = "$env:WINDIR\explorer.exe"; $l.Arguments = "shell:AppsFolder\$($app.AppID)"; $out.Add("Claude (Store app): $($app.AppID)")
}
$l.Description = 'Claude, started at sign-in'; $l.Save(); $out.Add("made $lnk")
& (Join-Path $PSScriptRoot 'make_shortcuts.ps1') -Startup | ForEach-Object { $out.Add("workbench: $_") }
$out | Set-Content $log; $out
