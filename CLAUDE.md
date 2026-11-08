# SynTHBloK: notes for Claude

These notes are shared between Nick's PCs through this repo. Read them at the start of a session. When you learn
something that matters next time (a fix, a gotcha, a decision), add it here, then commit and push. On the other PC,
`git pull` picks it up. This repo is **public**: no passwords, private IPs, personal emails or account details.

## What this is
SynTHBloK (brand NoiZee ToyZ, 29 modules, GPL-3.0-or-later), sold as 16 VCV Rack 2 plugins. Branch: `master`.
- The **MTX pack** is the repo root (plugin slug `SynTHBloK`, 11 modules that work together through COMPOSER,
  JAM and RANDROYD). In the VCV Library process: thread https://github.com/VCVRack/library/issues/1015.
- **Singles** (15 plugins, 18 modules) live in `singles/<slug>/` (e.g. `SynTHBloK-Kick`), each its own plugin
  built from the shared `src/` (via VPATH, see `singles/common.mk`).
- `tools/lineup.json` says which module goes where (and the VCV store bundles); `tools/modules.json` holds each
  module's name/description/tags. After editing either: `python3 tools/make_plugins.py` (writes every plugin.json,
  the registrations and the Makefiles). RANDROYD's `locate()` table and `tools/convert_slugs.py` list the same
  singles: keep the three in step. All plugins share one version; `tools/release.ps1` bumps them together.
- RANDROYD builds with what's installed: RACK mode lists missing singles, BUILD mode skips recipes needing them.

## Build (Windows)
- MSYS2 MINGW64 shell, Rack SDK 2.6.4 at `~/Rack-SDK` (`export RACK_DIR=~/Rack-SDK`). The repo lives at
  `~/SynTHBloK` (`C:\msys64\home\nickk\SynTHBloK`); scripts assume these paths.
- `make` builds, `make dist` makes `dist/SynTHBloK-<ver>-win-x64.vcvplugin`, `make devinstall` copies into Rack's
  plugins folder (`%LOCALAPPDATA%\Rack2\plugins-win-x64`). The same in `singles/<slug>/` for a single;
  `build_and_install.bat` does the MTX pack and every single.
- `SYNTHBLOK_DEV=1` (set by the .bat files) adds the PATCH GEN "module pictures" menu item, which closes Rack and
  runs a helper: development only, never in release builds (GitHub Actions doesn't set it).
- The Makefile links winpthread statically on Windows. Without it, new MSYS2 compilers make the plugin need
  `nanosleep64`, which Rack 2.6's bundled libwinpthread lacks, and Rack fails to load it with "code 127".
- PowerShell runs `.ps1` files only with `powershell -ExecutionPolicy Bypass -File <script>`.
- In MSYS2 git keep `core.autocrlf=true`, or every file shows as modified.
- Called from PowerShell, MSYS2 git's https push needs `C:\msys64\usr\bin` on PATH and GitHub CLI credentials:
  `git -c credential.helper= -c 'credential.helper=!"/c/Program Files/GitHub CLI/gh.exe" auth git-credential' push`.
  `tools/release.ps1` already does both.

## Claude desktop app gotcha (Windows)
The Claude desktop app is an MSIX package. Files Claude's commands (and programs they start, such as Rack) write
under `%LOCALAPPDATA%` are redirected into `%LOCALAPPDATA%\Packages\Claude_<id>\LocalCache\Local\...`. Only
Claude's processes see them; the user's own Rack and Firefox don't. So after `make devinstall` from Claude, the
user's Rack doesn't have the new build. Fix: the user double-clicks `Install SynTHBloK into Rack.cmd` on their
Desktop, which copies it with robocopy into the real folder (or copies the folder by hand in File Explorer).
Symptom: PATCH GEN on liquidnick.com counts one plugin too few.

## One-click scripts (repo root, double-click — no terminal typing)
These `.bat` launchers let a Claude computer-use session run things by double-clicking in File Explorer, since
it can't type into the MSYS2 / PowerShell terminal (those are granted click-only). Each wraps the real commands.
- `build_and_install.bat`: `make -j4 devinstall` in MSYS2; full output to `build.log`.
- `clean_build.bat`: `make clean` then a full `make -j4 devinstall` from scratch; output to `build.log`.
- `start_workbench.bat`: runs `tools/open_workbench.ps1` (serves the workbench on 127.0.0.1:8766 + the Tailscale
  address, starts the feeds, opens it in Firefox).
- `sync_push.bat`: `git pull --rebase --autostash` then push with the gh credential helper (via `git_sync.sh`);
  output to `git_push.log`. Use after committing, or to recover a push rejected as non-fast-forward.

## Releases (automated)
- Every push builds win-x64, lin-x64, mac-x64 and mac-arm64 in GitHub Actions (`.github/workflows/build.yml`).
- Release with `bash tools/release.sh 2.x.y` in MSYS2 (double-click a one-line .bat like `release_v2.1.0.bat`): it bumps every
  plugin.json, commits, tags and pushes. `tools/release.ps1`'s GitHub sign-in breaks in Windows PowerShell 5 (the space in
  "Program Files" splits the credential helper). If a tag's "GitHub Release" job fails, `bash tools/publish_release.sh 2.x.y <run id>`
  publishes it from that run's packages. Post on the VCV thread with `gh issue comment 1015 --repo VCVRack/library`.
- (old) `powershell -ExecutionPolicy Bypass -File tools\release.ps1` (or `-Minor`, `-Version 2.x.y`) bumps
  `plugin.json`, commits, tags `v<ver>` and pushes. The tag makes a GitHub Release with all four packages. Then it
  offers to post "Version X, commit Y" on the VCV thread (`tools/vcv-library-issue.txt`). Only post after that
  build is green.
- VCV rules: no other brands' names in module or effect names (the ladder filter is "LADDER LP"). Naming the
  Maschine JAM as the supported controller is fine.
- VCV review (Nov 2026): module slugs and names must not contain the plugin slug/name, names are short (no
  descriptions), tags only from VCV's list (no duplicates or aliases), no `expander` property in plugin.json. So the
  slugs lost "SynTHBloK" (SynTHBloKDrum -> Drum, SynTHBloK -> Quad; MorphFilter keeps NoizeeFlagshipFilter,
  BloKMaster keeps BlokMaster). Code passes the plugin slug "SynTHBloK" (unchanged) plus the new module slug.
  Old patches: double-click `convert_patches.bat` (repo patches, Rack's autosave, Desktop .vcv; or drop files on it),
  backups go to old-slugs-backup/. RANDROYD translates old slugs in saved tracks itself (newSlug in spawn()).

## Tools
- THINKING view in the workbench (`design_watch.html`, header button): shows what Claude is doing, why, the plan and a
  decision log, from `tools/claude_thinking.json` (local, git-ignored). Claude: update that file as you work (keys: updated,
  mode: think|compile|idle (drives the MIND canvas: embers when idle, alive when thinking, hot while compiling), task, goal, now{title,detail,why}, questions[], plan[{title,state: done|now|next|later|blocked,note}], log[{t,kind:
  think|do|check|fix|ask,text}]).
- `tools/open_workbench.ps1`: the workbench on 127.0.0.1:8766 (plus the Tailscale address for the phone), with the
  chat, patch-feed and open-in-Rack helpers.
- `tools/build_and_reload.ps1`: build, close Rack cleanly, devinstall, screenshots, reopen Rack.
- `tools/rack_shots.ps1`: module screenshots into `web-images/screenshots`. Its default Rack path is Rack2Pro; on a
  PC with Rack Free pass `-Rack 'C:\Program Files\VCV\Rack2Free\Rack.exe'`.
- `tools/build_parts_db.py`: PATCH GEN's parts database, written into the site repo (`patchgen/parts`).
- Maschine JAM: the Controller Editor templates (`*.ncmj`) are here, see `SETUP.md`. The Ableton script is in the
  `controller-scripts` repo.
