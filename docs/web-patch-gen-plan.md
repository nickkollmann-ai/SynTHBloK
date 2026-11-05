# SynTHBloK PATCH GEN — web app plan

A page on liquidnick.com where anyone builds musical VCV Rack patches from the plugins they already have.
They pick their Rack plugins folder (or paste a list), pick a genre, press ROLL, and download a `.vcv`.
Everything runs in the browser; nothing is uploaded. No code yet: this is the plan.

Mockup (desktop + phone, private): https://claude.ai/artifact/3osC2noxNjoza2XHpWwJG5 — sign it off before any UI work.

## What exists today

| Piece | What it does | What changes for the web |
|---|---|---|
| `tools/make_series_patches.py` | Rolls a patch from a seed: COMPOSER clock, drum part, main voice, layers, FX chains, mixer, send, master, modulation. Every cable goes through `Patch.w()`, which checks the jack is on the panel. | Port to JS. Keep the seed, the parts and the panel check. Replace the hard-wired module picks with "roles" filled from the user's library. |
| `tools/patch_genres.py` | 16 genres: tempo, key, scale, 4-chord progression, drum grooves, tone and FX amounts. | Port as-is (pure data and small functions). This is the musical heart and needs no library data. |
| `tools/portnames/portnames.cpp` | Loads one plugin's DLL, builds each module and its panel, prints jack names, jack positions (null = not on panel), width and params. | Runs offline only, in a sandbox, to build the parts database. Never in the browser and never on the web server. |
| `tools/patch_parts.json` | Cache of the above for the modules the generator uses. | Becomes the seed of the parts database. |
| `make_patches.save_vcv` | tar + zstd via MSYS2 tools. | Tar and zstd written in JS. |
| `tools/patches_base.json` | Your saved SynTHBloK module states (patterns). | Not shipped. The web version uses clean default states made for it, so none of your own patterns go public by accident. |

**The rule that shapes everything:** Rack drops a cable to a jack that isn't on the panel. So a module can only be cabled
if we know its jacks *and* which ones are on the panel. That data only comes from running the plugin's code (portnames).
`plugin.json` lists modules, names and tags, but no jacks.

## 1. Library input

**Main path — pick the plugins folder (read-only).**
- Chrome / Edge: `showDirectoryPicker({mode: 'read'})`. The page walks one level of folders and opens only
  `<plugin>/plugin.json`. It never opens `.dll` / `.so` / `.dylib`, presets or samples.
- Firefox / Safari (no directory picker): `<input type="file" webkitdirectory>`. The browser lists every file in the
  folder, but the page reads only the `plugin.json` files and drops the rest without opening them. Say this plainly
  next to the button. (You use Firefox, so this path must be first-class.)
- The page tells the user where the folder is: `%LOCALAPPDATA%\Rack2\plugins-win-x64`, `~/Library/Application Support/Rack2/plugins-mac-*`,
  `~/.local/share/Rack2/plugins-lin-x64`.
- From each `plugin.json` keep only: `slug`, `version`, and per module `slug`, `name`, `tags`, `hidden`. Everything else
  (author, email, URLs, descriptions) is ignored and never stored.
- The result is a small "library" object kept in memory. Optional "remember my library on this device" saves it to
  IndexedDB; a FORGET button wipes it.

**Fallback — paste or upload a list.** For phones and locked-down machines:
- paste lines of `slug version` (or `slug` alone), or
- drop any `.vcv` patch: the page reads the module list (plugin, model, version) from its `patch.json` and nothing else.
  This is also a quick way to say "use what's in my favourite patch".

## 2. Parts database

Jack and param data for public plugins, built offline, shipped as static JSON.

**Build (offline, sandboxed):**
1. Read the VCV Library manifest list (the public `VCVRack/library` repo: slug, version, source repo, commit).
2. For each free plugin version, get the build: build from source at the listed commit (preferred: no account needed,
   licence is visible), or a downloaded binary only if VCV's terms allow it (see Legal).
3. Run a Linux port of portnames (`dlopen` instead of `LoadLibraryA`, same output) in a throwaway container:
   no network, read-only filesystem except an output folder, non-root, CPU / memory / 60 s time limits, one model per run
   (as today: one crashing panel only takes itself down). Plugin code is untrusted code; it never runs anywhere
   that holds secrets.
4. Validate each output against a JSON schema (types, lengths, sane numbers) before it enters the DB.
5. Write one file per plugin version: `parts/<slug>/<version>.json` = per model `{in, out, inOk, outOk, hp, params}`,
   plus a jack-list hash. Add an index `parts/index.json`. Sign the index (hash list) so the page can check what it loads.

**Matching the user's library:**
- Exact slug + version → use it.
- Same slug, other version → use the nearest version whose jack-list hash is the same for that model. If jacks differ,
  that model is left out of cabling.
- Unknown plugin or model → left out of cabling. The page lists these ("12 modules we don't know yet") so the user
  understands why they never appear. They are never guessed at.
- Commercial / paid plugins: not in the DB unless the developer sends their data or gives permission (see Legal).

**Roles.** Today the generator names exact modules. The web version asks for a *role* and fills it from what the user has:
`clock`, `drums`, `voice`, `layer`, `insert-fx`, `send-fx`, `mixer`, `master`, `mod`.
- Curated roles (hand-written knob settings, like `fx_knobs` today) for SynTHBloK, Fundamental, VCV-Drums,
  AudibleInstruments, Valley, Befaco and Surge XT.
- Other DB modules get a role from their `tags` (Oscillator, Filter, Reverb, Delay, Mixer, LFO, Drum…) and their jacks
  by name, using the same matching as `io()` / `find()` today. Their knobs stay at the plugin's defaults, which are
  always safe. Only curated modules get musical knob settings.
- If the user has no SynTHBloK, the clock falls back to a Fundamental-only set (e.g. `SEQ3` / `Clock` from the DB) and the
  genre still sets tempo, key and grooves where the modules allow it.

## 3. Generator in the browser

- Port `make_series_patches.py` + `patch_genres.py` to plain JS modules (no framework). Same structure: `Patch` with
  `add`, `w`, `find`, `ok`, `free`; `drum_part`, `main_voice`, `layer`, `chain`, `modulate`, `build`.
- Seeded RNG so a patch number + genre + library always gives the same patch (share-by-seed later costs nothing).
  Python's `random.Random(str)` can't be matched exactly; use a small seeded PRNG (e.g. sfc32) in both if parity matters.
- UI: genre picker (16 + RANDOM), patch count, ROLL, a plain-box preview of the rack (rows by hp, cables as lines),
  and DOWNLOAD `.vcv` (one file, or a `.zip` of several).
- `.vcv` writer: build `patch.json`, wrap it in a ustar tar with the empty `modules/` folder (~60 lines of JS),
  then zstd. Use a small zstd WASM build pinned by hash, or write a valid zstd frame with raw (uncompressed) blocks
  (~30 lines, no dependency). Test both against Rack 2 before choosing.
- Golden tests: run the Python and the JS on the same fixed library and compare module lists, cable lists and the
  panel check (not byte-for-byte RNG).

## 4. Safety and permissions

**Default mode: local-first, no account.**
- No login, no cookies, no analytics, no third-party scripts. All code and data served from liquidnick.com.
- Strict Content-Security-Policy (`default-src 'self'`, no `unsafe-eval`, `connect-src 'self'`), Subresource Integrity on
  scripts, `Permissions-Policy` turning off camera / mic / geolocation.
- Folder access is read-only, asked for by a button press, and only for that page visit unless the user chooses "remember".
  The page never writes to the user's disk except the download they click.
- The library never leaves the device. The network tab shows only static JSON fetches from `parts/` (one per plugin).

**Every generated patch is checked before download:**
- Schema check of `patch.json`: known keys only, numbers finite and in each param's min/max, ids unique, every cable's
  ends are modules in the patch and jacks with `inOk` / `outOk` true, one cable per input.
- Module `data` comes only from our curated defaults. Before writing, strip any string that looks like a file path
  (drive letters, `/`, `\`, `~`) or a URL. Sample players get no file paths at all.
- Size limits: ≤ 64 modules, ≤ 256 cables, `patch.json` ≤ 1 MB.
- Uploaded `.vcv` (fallback input) is opened with limits too: ≤ 5 MB compressed, ≤ 20 MB unpacked, only `patch.json` read,
  no paths followed from the tar, and it is used only for its module list.

**If accounts and sharing come later:**
- Sign-in with OAuth (e.g. GitHub, Google) only; no passwords stored by us. Store the minimum: an id and a display name.
- Everything private by default; sharing is a per-patch choice with an unlisted link first, public listing second.
- Server re-validates every uploaded patch with the same rules (never trust the browser), stores only `patch.json`
  (no library, no paths), and re-strips `data`.
- Rate limits on uploads, sign-ins and shares; size caps per user; report button and simple moderation for public patches.
- Right to delete: delete one patch, or the whole account and all its patches, from the account page, done at once.
  Export my data as one zip. A short plain-language privacy page.

## 5. Legal

- **No third-party panel art.** The preview draws plain boxes with the module's name and hp. Only SynTHBloK may use its
  own look.
- **VCV Library terms.** Read the VCV Library / website terms and `robots.txt` before any automated fetching. Prefer
  the public manifest repo and building from source. If downloads or scraping aren't allowed, ask VCV or limit the DB
  to plugins whose developers say yes.
- **Plugin licences and developers.** Jack names and counts are facts, but be polite: credit each plugin, link to its
  library page, and keep an opt-out list honoured by the build. Invite developers to send their own parts file.
- **Trademarks.** "VCV" and "VCV Rack" are VCV's marks: use them only to say what the app works with
  ("makes patches for VCV Rack"), never in the app's name or logo, with a "not affiliated with VCV" line. Plugin and
  module names are shown as plain text only, never logos.
- `.vcv` output contains the user's own modules' slugs; no third-party code or art is shipped.

## 6. Hosting

- A page on liquidnick.com next to ToyBoX: `liquidnick.com/patchgen/`, in the SynTHBloK look only (dark `#0d0d14`,
  cyan `#00e5ff`, magenta `#ff00aa`, orange `#ff7a00`, OLED-style cells, ToyBoX-style pop-ups).
- MVP is fully static: HTML + JS + `parts/*.json`. It deploys like the rest of the site (push the site repo, then
  cPanel Git Version Control). No server code, nothing to patch, nothing to breach.
- The parts DB build runs on your machine or a throwaway cloud VM, never on the web host; it outputs JSON that is
  committed to the site repo.
- Accounts / sharing need a backend; decide then (cPanel PHP + MySQL is the simplest fit; a small separate service if
  the host can't do OAuth cleanly).

## 7. Milestones (rough effort, focused work)

| # | Milestone | Scope | Effort |
|---|---|---|---|
| 0 | Mockup sign-off | the mockup above approved | done when you say so |
| 1 | **MVP** | Library input (folder + paste + `.vcv`), parts for SynTHBloK + Fundamental + VCV-Drums + AudibleInstruments + Valley + Befaco + Surge XT (from your local `patch_parts.json` / portnames), JS port of generator + genres, `.vcv` writer, validation, download only, page on liquidnick.com | 2–3 weeks |
| 2 | **Full parts DB** | Linux portnames, sandboxed batch build over the free VCV Library, schema check, version matching, tag-based roles, opt-out list, terms checked first | 3–4 weeks, then a monthly rebuild |
| 3 | **Accounts + sharing** | OAuth, private by default, unlisted links, server validation, rate limits, delete / export, privacy page | 3–5 weeks |

## Decisions (2026-11-05)

1. **Name:** PATCH GEN, at `liquidnick.com/patchgen/`.
2. **Plugin set:** whatever the user's detected library holds that the parts DB knows. There's no fixed list.
   So the DB has to be broad from day one. The MVP DB is built from the 182 plugins installed on the dev machine
   (portnames, locally), then grows with the milestone 2 batch build. Milestones 1 and 2 overlap.
3. **SynTHBloK is not required.** Patches are built from the user's own VCV Rack library. When SynTHBloK is installed it
   takes the clock / drum / voice / mixer roles as today. Without it, those roles fall back to the best-fitting modules the user has
   (Fundamental SEQ3 / Clock, VCV Mix, drum and voice modules by tag).
4. **VCV terms: checked ourselves (2026-11-05).**
   - `vcvrack.com/robots.txt` allows everything (`Disallow:` empty).
   - The public `VCVRack/library` repo (manifests, source repos, commit hashes) has no stated licence or usage limit.
   - The plugin licensing page says open-source plugins are GPLv3+ or other free licences, so building them from
     their public source is allowed by their own licences.
   - No terms page restricting tools was found (`/terms`, `/legal` are 404).

   **Plan:** build only open-source plugins, from source, at the manifest's commit. Don't download VCV-hosted binaries
   (they sit behind a login). Leave out closed-source freeware and commercial plugins unless the developer sends data.
   Say "for VCV Rack" only, and add a not-affiliated line.
