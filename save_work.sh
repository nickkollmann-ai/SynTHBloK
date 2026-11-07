#!/usr/bin/env bash
# Commit and push everything since the last push: the MTX pack + singles split, slug rename, RANDROYD.
# Only adds known source/tool files, so the junk (.bak, SurgeXTRack copies, big feeds) stays out.
set -e
cd ~/SynTHBloK

echo "== new CI workflow for the split"
cp tools/new-build.yml .github/workflows/build.yml

echo "== adding files"
git add -u
git add singles res patches docs convert_patches.bat save_work.bat save_work.sh \
  src/SynTHBloKRandroyd.cpp src/SynTHBloKRandroyd_layout.hpp \
  tools/lineup.json tools/modules.json tools/make_plugins.py tools/convert_slugs.py \
  tools/make_randroyd_panel.py tools/new-build.yml 2>/dev/null || true

echo "== safety check (nothing over 5 MB, no .bak)"
bad=0
while IFS= read -r f; do
  [ -f "$f" ] || continue
  case "$f" in *.bak) echo "  unstaging $f"; git reset -q -- "$f"; continue;; esac
  sz=$(stat -c %s "$f")
  if [ "$sz" -gt 5000000 ]; then echo "  TOO BIG, unstaging: $f ($sz bytes)"; git reset -q -- "$f"; bad=1; fi
done < <(git diff --cached --name-only)

echo "== what will be committed"
git diff --cached --stat | tail -40

if git diff --cached --quiet; then echo "Nothing to commit."; exit 0; fi

git commit -q -m "Split into MTX pack + 15 singles with VCV bundles; drop SynTHBloK prefix from module slugs; add RANDROYD

- tools/lineup.json + make_plugins.py generate every plugin (MTX pack at root, singles/<slug>/)
- 6 bundles defined for the VCV store
- slugs renamed per VCV review; convert_patches.bat updates old patches
- RANDROYD track instrument (RACK mode, MODE switch, GROW)
- CI builds the MTX pack and every single"

echo "== pushing"
git pull --rebase --autostash
git -c credential.helper= -c 'credential.helper=!"/c/Program Files/GitHub CLI/gh.exe" auth git-credential' push
git log -1 --format='SAVED %h %s'
echo "PUSH DONE"
