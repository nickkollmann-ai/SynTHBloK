# SynTHBloK: notes for Claude

These notes are shared between Nick's PCs through this repo. Read them at the start of a session. When you learn
something that matters next time (a fix, a gotcha, a decision), add it here, then commit and push. On the other PC,
`git pull` picks it up. This repo is **public**: no passwords, private IPs, personal emails or account details.

## What this is
SynTHBloK (brand NoiZee ToyZ, 29 modules, GPL-3.0-or-later), sold as 16 VCV Rack 2 plugins. Branch: `master`.
- The **MTX pack** is the repo root (plugin slug `SynTHBloK`, 11 modules that work together through COMPOSER,
  JAM and RANDROYD). In the VCV Library process: thread https://github.com/VCVRack/library/issues/1015.
- **Singles** (16 plugins, 19 modules) live in `singles/<slug>/` (e.g. `SynTHBloK-Kick`), each its own plugin
  built from the shared `src/` (via VPATH, see `singles/common.mk`).
- `tools/lineup.json` says which module goes where (and the VCV store bundles); `tools/modules.json` holds each
  module's name/description/tags. After editing either: `python3 tools/make_plugins.py` (writes every plugin.json,
  the registrations and the Makefiles). RANDROYD's `locate()` table and `tools/convert_slugs.py` list the same
  singles: keep the three in step. All plugins share one version; `tools/release.ps1` bumps them together.
- BLANK (`src/SynTHBloKBlank.cpp`, Nov 2026) ships in BOTH the MTX pack and its own single `SynTHBloK-Blanks`
  (lineup.json `shared`, which make_plugins.py allows in more than one plugin). It draws its own face matching the
  modules' panels (#111, header strip + orange rule, #161616 card, grey footer), looks BADGE / BACKLIT (light on Rack's
  light layer) / CIRCUIT, 1-32 HP (drag the right edge or the Width menu), four corner screws (1-2 HP: one centred top and bottom). BACKDROP is in lineup.json
  now too (it had been hand-added, so make_plugins.py would have dropped it); its VCV name is plain "BACKDROP".
- Singles' RES lists come from scanning sources + plugin.hpp: after the SocketScrew change make_plugins.py had not been
  re-run, so the singles shipped without res/SocketScrewBlack.svg (no screws in them). Re-run it after touching plugin.hpp.
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
- WORKBENCH header OPEN RACK button: POST /rack/launch on the helper starts Rack.exe (Rack2Pro, else Rack2Free) from the
  helper (a normal user process, so the real plugins folder) or brings a running Rack to the front; GET /ping says if it runs.
- `tools/open_workbench.ps1`: the workbench on 127.0.0.1:8766 (plus the Tailscale address for the phone), with the
  chat, patch-feed and open-in-Rack helpers.
- POWERSHELL tab console modes: PS, CMD (cmd.exe) and ADMIN. ADMIN starts `tools/admin_worker.ps1` through the Windows
  admin prompt (Nick clicks Yes once); it runs hidden, takes commands from `tools/ps_runs/admin/`, and quits after 15 min
  idle or on END ADMIN. After changing `rack_open_server.py`, run `tools/restart_feeds.ps1` (RUN THESE item) to load it.
- SITE tab (liquidnick.com): LIVE shows the site + WATCH (the helper's GET /site/check hashes the page every minute) +
  CHANGES/COMM; BUILDER shows Claude's live site work. Nick writes to `tools/site_inbox.json` ("check SITE" in chat);
  Claude answers in `tools/site.json` ({messages[{t,text}], changes[{title,state: idea|planned|building|live,note}]}) and,
  while working on the site, keeps `tools/site_build.json` current ({updated, mode: idle|think|build|deploy, task, rev, page,
  now{title,detail,why}, plan[], files[{path,add,del,note}], log[{t,kind,text}]}; bump rev to reload the preview). The
  BUILDER preview serves the local copy `Desktop\liquidnick_site_work` via the helper's GET /site/local/<path>.
- BACKDROP LAB > BUILDER: Claude's live BACKDROP work from `tools/backdrop_build.json` (mode idle|think|build|compile|shoot,
  task, now, plan, files, log, preview, shots[{src,name}], messages[]), the INFLUENCES board from `tools/backdrop_influences.json`
  (Nick's Midjourney themes; local only), and COMM (`tools/backdrop_inbox.json` via the helper's POST /backdrop; "check BACKDROP").
  Art: `python3 tools/make_backdrop_art.py [names | psy]` renders res/backdrops/*.jpg; new looks/cases/themes go at the END of
  their lists (patches save looks, colours and side materials by number).
  COMM read receipts: after reading the inbox set `read` in backdrop_build.json to the highest inbox id (the board then shows
  READ BY CLAUDE; unread notes show a "say check BACKDROP" banner). PICKS: `tools/backdrop_picks.json` ({round, note, sent,
  picks[{n,id,k,w,h,date,prompt}]}, Midjourney job id + image index) shows a KEEP/DROP grid; Nick's answer arrives as a COMM note.
  Midjourney's API from the signed-in built-in browser: fetch('/api/imagine?user_id=<id>&page_size=10000', {headers:
  {'x-csrf-protection':'1'}}); images at cdn.midjourney.com/<id>/0_<k>_384_N.webp (thumb) or 0_<k>.png.
  Species woods: `python3 tools/make_wood_textures.py [slugs]` -> res/cases/wood_*.png (full colour, not overlays); the
  SIDE MATERIAL entries with a `tex` use them, with WOOD FINISH (OILED/SATIN/GLOSS, json `sideFinish`).
  OVERVIEW's BUILDER · LIVE card mirrors the BUILDER preview (CASE ROUNDS now live only in COMPARE).
  BACKDROP's MY IMAGES / MY PRESETS: folders `<Rack user>/SynTHBloK/backdrop-images` and `backdrop-presets` (presets are the
  module's JSON minus the title), stepped on the OLED (BG page: MY IMAGES; THEME page: MY PRESET, SAVE PRESET) and in the menu.
  Round 6 art (`make_backdrop_art.py r6`): 25 "Dream" pieces drawn from Nick's 50 approved Midjourney picks; themes can now
  set a side material + wood finish (Theme.sideMat / sideFinish, SW_* names for the species woods).
  SIDE PANELS (attachments, Nick's meaning of "side panels"): separate solid slabs on the outside of each case end
  (kPanelMat: NONE, 24 woods, 6 man-made: diamond plate, carbon fiber, 3D printed PLA, brushed alu, neon acrylic, tolex;
  shapes SQUARE 2 / SEMI-ROUNDED 6 / ROUNDED 16 px (kPanelShape, SQUARE appended at index 2); one width, SLIM 36 px; OILED/SATIN/GLOSS). Flush with the case top and
  the full case height (they follow the case size); as deep as the case (swept to the lower right by depth*0.18 like
  drawCase; pass 0 shadow before the case, pass 1 depth + face after it; the left panel's depth clipped behind the case
  front); the material's texture on every face, shaded toward the back. NoiZee ToyZ plates (kPanelNoizee = 31: BADGE, BACKLIT, CIRCUIT)
  draw the matching ear's art (drawNoizeeEar) on the slim panel; the name follows earName (NAME ON PANEL). CASE MATERIAL textures the case's sides too;
  SIDES page: PANELS adds CASE ON / OFF: RAILS ONLY (noCase: panels alone hold the rails) and BACKGROUND ON/OFF;
  EARS adds BACKGROUND ON/OFF (off: bare rails on the ears over Rack's own background; same flag as showBg); own shadow, NO screws on the face
  (real panels screw on from the inside). CASE MATERIAL builds the frame itself in any kPanelMat material. The old CASE ENDS
  material/finish option is removed (Nov 2026; saved sideMat/sideFinish are ignored). Themes put their wood on the panels.
  Side panels XOR rack ears: picking one turns the other off. With ears there is NO case: an open rack, Rack's rails screwing
  straight onto the ears (ears at the row ends; a round hole + our socket screw, 23 px = 10-32 cap, only at the very top and bottom rails). With side
  panels on, the case's own ends are hidden (drawn 2 px, square); they come back when the panels go. Ears are 92 px wide,
  STRAIGHT or CONTOURED (earShape: rounded top and bottom outer corners, 18 px). Each ear's wing (the L's side) runs
  back as deep as DEPTH (swept like the case; shows below the rows / plates), and the plate shows a sheet-metal edge. NoiZee ToyZ ears (isNoizee: index >= 10):
  NOIZEE TOYZ = big wordmark + orange rule, then BADGE (hex Z logo), ENGRAVED (etched raw alu), BACKLIT (slots + name lit
  orange, a steady glow), CIRCUIT (the logo's line + lightning Z); earName hides the name only. END SHAPE / END WIDTH are gone: cases keep their ends.
  Man-made textures: `make_wood_textures.py mats` -> res/cases/mat_*.png. RACK EARS (kEarFin): L-shaped brackets, a lip
  down each case end (outside any side panels), a plate bent out with two oval slots; 9 plain finishes (aluminium, black,
  gold, copper, bronze, green, primary red/blue/yellow) + NOIZEE TOYZ (black, orange rule, name optional: earName).
  BACKDROP is 12 HP since Nov 2026 (was 6; `tools/make_backdrop_panel.py`). OLED (simplified): 4 tabs LOOK / CASE / SIDES /
  NAME (kNamePage = 3), 3-5 rows each; SIDES is one NONE/PANELS/EARS switch; rows are < > steppers or option boxes (Row.opts);
  OLED v3 (Nov 2026): tabs, the live preview, a row of CATEGORY keys, and the chosen category's
  choices below as a grid of square keys in numbered pages (‹ 1 2 .. ›; the wheel turns pages; no list stepping). LOOK: THEME /
  BG / COLOUR (swatches) / IMAGE (NONE, LOAD FILE, MY FOLDER, the 50 IMAGE BANK thumbnails, then MY IMAGES) / PRESET. CASE:
  CASE / MATERIAL / DEPTH / SHADOW. SIDES: OFF / PANELS / EARS act at once, then MATERIAL-or-FINISH / SHAPE / (FINISH) / MORE.
  NAME: the field, SHOW / HIDE, USE FILE NAME, COLLAPSE. IMAGE BANK: res/bank/bank_NN.jpg (full size, the background) + thumb_NN.jpg
  (192 px, the OLED keys): 69 pictures (Nov 2026): 44 Nick chose + 25 more from his Midjourney
  in the same style (3 full pages of the IMAGE grid); letterbox bars trimmed, numbered darkest first; bankImg 1..69.
  LOOK tabs: BG / COLOUR / IMAGE / THEME / PRESET (grid() maps them to its old numbers). lookOn: the BG look shows;
  BG > OFF (the first key on every BG page) shows a loaded picture in its place; picking a picture turns the look off,
  a theme or a look turns it back on (the picture stays loaded). Old patches without lookOn: on unless a picture was loaded.
  Collapsed, the face fills the gap by itself (growStep): from 7 HP up to its full 12 HP over the free space on its right;
  while any module is dragged or added it drops to 7 HP so one can go there (foldHp; folded tracks the state, not the width).
  COLLAPSE / EXPAND is one small key (BackdropExpand, 18 x 2.8 mm) in the gap under the OLED well (y 122.25 mm), centred,
  in the same place folded or not, labelled with what it does. The NAME page keeps the fill choice as a small row at its
  very bottom (COLLAPSE FILL: BLANK RIGHT / BLANK LEFT / EMPTY, fillRect); the COLLAPSE key left the OLED grid.
  The four top tabs are square keys with 4 px rounded corners (tabRect/tabSide).
  COLLAPSE: 7 HP, the modules' face with a tall strip of the current background and an EXPAND key (expanding pushes
  neighbours with setModulePosForce); presets keep the module's fold. FILL (blankFill): collapsing makes a 5 HP BLANK in the void,
  right of BACKDROP or left (BACKDROP shifts right); that BLANK has lockedTo = the BACKDROP's id (no resizing; freed if the
  BACKDROP goes); expanding removes it (removeAction) and BACKDROP takes its old place back.
  Themes (remade): mostly case only; 6 wear side panels, 6 wear rack ears (Theme.ear / earShape). a LIVE PREVIEW strip (art, case colour, panels, ears), label-over-value rows with arrows, a hint line.
  No NAME key on the panel: the NAME tab starts typing and carries the status LED (amber = this one dresses the rack, red =
  another does). No inputs since Nov 2026 (the L/R jacks and the meter came off;
  the OLED runs down to just above the bottom screws); the tabs and option boxes are square keys packed tight, all softly backlit, the chosen one brighter.
  Four screws at 3.5 mm like every module.
  ORDER (Nov 2026): every list (themes, BG looks, colours, cases, materials, ears, image bank) SHOWS darkest to lightest,
  NoiZee ToyZ builds first; stored numbers never move. `python3 tools/make_backdrop_order.py` writes src/BackdropOrder.hpp
  (art/bank brightness measured from res/): re-run it after adding any of those, or the static_assert stops the build.
  Screws: since Nov 2026 every module has four SocketScrews, 3.5 mm in from each corner (y 3.5 and 125 mm). Before that
  15 modules had only the two top ones (since before v2.1.0); the bottom pair sits over the cards' corners, as it always
  did on the four-screw modules (Quad, filters, Player...).
  Rails: BACKDROP never draws rails. Each case row is left open edge to edge (a shorter row's empty end too), so Rack's own
  rails show across the whole case between the side panels; the LAB ASSEMBLY copies Rack's rail look.
- `tools/build_and_reload.ps1`: build, close Rack cleanly, devinstall, screenshots, reopen Rack.
- `tools/rack_shots.ps1`: module screenshots into `web-images/screenshots`. Its default Rack path is Rack2Pro; on a
  PC with Rack Free pass `-Rack 'C:\Program Files\VCV\Rack2Free\Rack.exe'`.
- `tools/build_parts_db.py`: PATCH GEN's parts database, written into the site repo (`patchgen/parts`).
- Maschine JAM: the Controller Editor templates (`*.ncmj`) are here, see `SETUP.md`. The Ableton script is in the
  `controller-scripts` repo.
