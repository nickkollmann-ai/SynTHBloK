#!/usr/bin/env bash
# Commit and push work in progress. Message from commit_message.txt if present (then deleted), else "Save work".
# Only adds known source/tool files, so the junk (.bak, SurgeXTRack copies, big feeds) stays out.
set -e
cd ~/SynTHBloK

echo "== CI workflow"
cp tools/new-build.yml .github/workflows/build.yml

echo "== adding files"
git add -u
git add singles res patches docs convert_patches.bat save_to_github.bat regen_patches.bat regen_gen.bat build_mtx.bat compile_mtx.bat open_randroyd_test.bat start_pc_stats.bat start_claude_jobs.bat tools/pc_stats.ps1 tools/claude_jobs.ps1 save_to_github.sh make_plugins.bat tools/release.sh tools/publish_release.sh \
  src/SynTHBloKRandroyd.cpp src/SynTHBloKRandroyd_layout.hpp src/SynTHBloKBackdrop.cpp \
  open_backdrop_test.bat install_backdrop_test.bat tools/install_and_open.ps1 tools/restart_feeds.ps1 tools/lhm_install.ps1 tools/workbench_app.ps1 tools/workbench.vbs tools/workbench.ico tools/make_shortcuts.ps1 tools/open_workbench.ps1 tools/rack_open_server.py tools/design_watch.html tools/make_backdrop_panel.py tools/make_backdrop_art.py tools/make_case_textures.py install_backdrop_cases.bat \
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

if [ -f commit_message.txt ]; then git commit -q -F commit_message.txt; rm -f commit_message.txt
else git commit -q -m "Save work"; fi

echo "== pushing"
git pull --rebase --autostash
git -c credential.helper= -c 'credential.helper=!"/c/Program Files/GitHub CLI/gh.exe" auth git-credential' push
git log -1 --format='SAVED %h %s'
echo "PUSH DONE"
