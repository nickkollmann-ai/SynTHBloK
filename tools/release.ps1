# Release a new SynTHBloK version in one go:
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1            (2.0.0 -> 2.0.1)
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Minor     (2.0.1 -> 2.1.0)
#   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version 2.3.0
# It sets the version in plugin.json, commits, tags v<version> and pushes. GitHub then builds Windows, Linux and
# both Macs and publishes a GitHub Release (.github/workflows/build.yml). Last, it offers to tell the VCV Library
# about the new version, by a comment on the plugin's issue there (the number is kept in tools/vcv-library-issue.txt).
param([string]$Version, [switch]$Minor, [switch]$NoLibrary)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$git = 'C:\msys64\usr\bin\git.exe'
$gh = 'C:\Program Files\GitHub CLI\gh.exe'
function G { & $git -C $repo @args; if ($LASTEXITCODE) { throw "git $args failed" } }

if (G status --porcelain --untracked-files=no) { throw 'Commit or undo your changes first (git status shows uncommitted work).' }
$pj = Join-Path $repo 'plugin.json'
$text = [IO.File]::ReadAllText($pj)
$old = ($text | ConvertFrom-Json).version
if (-not $Version) {
    $p = $old.Split('.') | ForEach-Object { [int]$_ }
    if ($Minor) { $Version = "$($p[0]).$($p[1] + 1).0" } else { $Version = "$($p[0]).$($p[1]).$($p[2] + 1)" }
}
if ($Version -notmatch '^2\.\d+\.\d+$') { throw "Rack 2 plugin versions look like 2.x.y, not $Version" }
if (G tag --list "v$Version") { throw "v$Version already exists" }

$new = [regex]::Replace($text, '("version"\s*:\s*")[^"]*(")', "`${1}$Version`${2}", 1)
[IO.File]::WriteAllText($pj, $new, (New-Object Text.UTF8Encoding $false))
G add plugin.json
G commit -q -m "Release v$Version"
G tag -a "v$Version" -m "SynTHBloK v$Version"
G push -q origin HEAD "v$Version"
$commit = (G rev-parse HEAD).Trim()
Write-Host "Pushed v$Version ($old -> $Version, commit $commit)." -ForegroundColor Cyan
Write-Host 'GitHub is building it now: https://github.com/nickkollmann-ai/SynTHBloK/actions'

$issueFile = Join-Path $PSScriptRoot 'vcv-library-issue.txt'
if ($NoLibrary -or -not (Test-Path $issueFile)) { return }
$issue = (Get-Content $issueFile -TotalCount 1).Trim()
$body = "Version $Version, commit $commit"
Write-Host "`nVCV Library comment for issue #${issue}:  $body"
if ((Read-Host 'Post it now? (y/n)') -eq 'y') {
    & $gh issue comment $issue --repo VCVRack/library --body $body
}
