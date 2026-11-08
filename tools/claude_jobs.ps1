# CLAUDE JOBS: lets Claude run the repo's own scripts without taking over the screen.
# Claude drops jobs\<id>.job (one line: a script name, e.g. save_to_github.bat or tools\pc_probe.ps1); this runs it
# hidden, writes everything it prints to jobs\<id>.log, then jobs\<id>.done with the exit code.
# Only .bat / .sh / .ps1 files that already sit in the SynTHBloK folder or tools\ can run: no paths outside, no
# arguments, no commands typed into the job. A 'pause' in a .bat returns at once (stdin is empty).
# One copy at a time (named mutex). tools/open_workbench.ps1 starts it; start_claude_jobs.bat by hand.
$ErrorActionPreference = 'SilentlyContinue'
$created = $false
$mutex = New-Object System.Threading.Mutex($true, 'Global\SynTHBloK_claude_jobs', [ref]$created)
if (-not $created) { exit 0 }

$repo = Split-Path -Parent $PSScriptRoot
$jobs = Join-Path $repo 'jobs'
New-Item -ItemType Directory -Force $jobs | Out-Null
$bash = 'C:\msys64\usr\bin\bash.exe'
"$(Get-Date -Format s) runner up (pid $PID)" | Set-Content (Join-Path $jobs 'runner.txt')

while ($true) {
    foreach ($f in Get-ChildItem $jobs -Filter *.job | Sort-Object LastWriteTime) {
        $id = $f.BaseName
        $log = Join-Path $jobs "$id.log"; $done = Join-Path $jobs "$id.done"
        $name = ((Get-Content $f.FullName -TotalCount 1) -as [string]).Trim()
        Remove-Item $f.FullName -Force
        $code = 1
        # allowlist: a bare file name, or tools\<file>, of a script that exists in the repo
        if ($name -notmatch '^(tools[\\/])?[\w.\-]+\.(bat|sh|ps1)$') { "REFUSED: '$name' is not a repo script name" | Set-Content $log }
        else {
            $path = Join-Path $repo ($name -replace '/', '\')
            if (-not (Test-Path $path -PathType Leaf)) { "REFUSED: $name not found in $repo" | Set-Content $log }
            else {
                "== $(Get-Date -Format s) $name" | Set-Content $log
                $ext = [IO.Path]::GetExtension($path).ToLower()
                $psi = New-Object Diagnostics.ProcessStartInfo
                $psi.WorkingDirectory = Split-Path $path; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
                $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.RedirectStandardInput = $true
                switch ($ext) {
                    '.bat' { $psi.FileName = 'cmd.exe'; $psi.Arguments = "/c `"$path`"" }
                    '.ps1' { $psi.FileName = 'powershell.exe'; $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$path`"" }
                    '.sh'  { $psi.FileName = $bash; $psi.Arguments = "-lc `"cd '$(($psi.WorkingDirectory) -replace '\\','/')' && bash '$(Split-Path $path -Leaf)'`""
                             $psi.EnvironmentVariables['MSYSTEM'] = 'MINGW64'; $psi.EnvironmentVariables['CHERE_INVOKING'] = '1' }
                }
                $p = [Diagnostics.Process]::Start($psi)
                $p.StandardInput.Close()   # 'pause' and prompts return at once
                $err = $p.StandardError.ReadToEndAsync()
                $out = $p.StandardOutput.ReadToEnd()
                $p.WaitForExit(); $code = $p.ExitCode
                Add-Content $log $out; if ($err.Result) { Add-Content $log "== stderr`r`n$($err.Result)" }
            }
        }
        "$code" | Set-Content $done
    }
    Start-Sleep -Milliseconds 700
}
