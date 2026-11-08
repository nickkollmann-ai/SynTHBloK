#!/usr/bin/env bash
# Release every SynTHBloK plugin at one version (the MTX pack and each single): bumps each plugin.json, commits,
# tags v<version> and pushes. GitHub then builds Windows, Linux and both Macs and publishes the GitHub Release.
#   bash tools/release.sh 2.1.0
# Runs in MSYS2 with the GitHub CLI sign-in, like save_to_github.sh (release.ps1's sign-in breaks in PowerShell 5).
set -e
cd ~/SynTHBloK
V="$1"
[[ "$V" =~ ^2\.[0-9]+\.[0-9]+$ ]] || { echo "Rack 2 plugin versions look like 2.x.y, not '$V'"; exit 1; }
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "Commit or undo your changes first:"; git status --short --untracked-files=no; exit 1; }
[ -z "$(git tag --list "v$V")" ] || { echo "v$V already exists"; exit 1; }
OLD=$(grep -m1 '"version"' plugin.json | sed -E 's/.*"version"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/')
for f in plugin.json singles/*/plugin.json; do
  sed -i -E '0,/"version"[[:space:]]*:[[:space:]]*"[^"]*"/s//"version": "'"$V"'"/' "$f"
  git add "$f"
done
echo "== version $OLD -> $V in $(git diff --cached --name-only | wc -l) plugin.json files"
git commit -q -m "Release v$V"
git tag -a "v$V" -m "SynTHBloK v$V"
git -c credential.helper= -c 'credential.helper=!"/c/Program Files/GitHub CLI/gh.exe" auth git-credential' push -q origin HEAD "v$V"
echo "RELEASED v$V  commit $(git rev-parse HEAD)"
