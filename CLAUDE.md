# SynTHBloK: notes for Claude

These notes are shared between Nick's PCs through this repo. Read them at the start of a session. When you learn
something that matters next time (a fix, a gotcha, a decision), add it here, then commit and push. On the other PC,
`git pull` picks it up. This repo is **public**: no passwords, private IPs, personal emails or account details.

## What this is
A VCV Rack 2 plugin (slug `SynTHBloK`, brand NoiZee ToyZ, 28 modules), GPL-3.0-or-later. Branch: `master`.
In the VCV Library process: thread https://github.com/VCVRack/library/issues/1015 (submitted at v2.0.0).

## Build (Windows)
- MSYS2 MINGW64 shell, Rack SDK 2.6.4 at `~/Rack-SDK` (`export RACK_DIR=~/Rack-SDK`). The repo lives at
  `~/SynTHBloK` (`C:\msys64\home\nickk\SynTHBloK`); scripts assume these paths.
- `make` builds, `make dist` makes `dist/SynTHBloK-<ver>-win-x64.vcvplugin`, `make devinstall` copies into Rack's
  plugins folder (`%LOCALAPPDATA%\Rack2\plugins-win-x64`).
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

## Releases (automated)
- Every push builds win-x64, lin-x64, mac-x64 and mac-arm64 in GitHub Actions (`.github/workflows/build.yml`).
- `powershell -ExecutionPolicy Bypass -File tools\release.ps1` (or `-Minor`, `-Version 2.x.y`) bumps
  `plugin.json`, commits, tags `v<ver>` and pushes. The tag makes a GitHub Release with all four packages. Then it
  offers to post "Version X, commit Y" on the VCV thread (`tools/vcv-library-issue.txt`). Only post after that
  build is green.
- VCV rules: no other brands' names in module or effect names (the ladder filter is "LADDER LP"). Naming the
  Maschine JAM as the supported controller is fine.

## Tools
- `tools/open_workbench.ps1`: the workbench on 127.0.0.1:8766 (plus the Tailscale address for the phone), with the
  chat, patch-feed and open-in-Rack helpers.
- `tools/build_and_reload.ps1`: build, close Rack cleanly, devinstall, screenshots, reopen Rack.
- `tools/rack_shots.ps1`: module screenshots into `web-images/screenshots`. Its default Rack path is Rack2Pro; on a
  PC with Rack Free pass `-Rack 'C:\Program Files\VCV\Rack2Free\Rack.exe'`.
- `tools/build_parts_db.py`: PATCH GEN's parts database, written into the site repo (`patchgen/parts`).
- Maschine JAM: the Controller Editor templates (`*.ncmj`) are here, see `SETUP.md`. The Ableton script is in the
  `controller-scripts` repo.
