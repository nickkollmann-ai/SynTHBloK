# Start a Claude session here (quickstart)

## Devices
- **nsasecureserver** — main Windows build PC. Runs the Claude desktop app + Computer use; this is the workhorse.
- **NSASERVER** — Yoga 2-in-1 laptop, Windows 11 **Home** (can't host Remote Desktop). Reach it via AnyDesk or
  RustDesk over Tailscale, or install the Claude desktop app on it for direct control.
- **Phone** — chat client. The workbench loads on the Tailscale address.

## To start a session on a PC
1. Open the **Claude desktop app** on that PC (awake, signed in, **Computer use ON** in Settings).
2. Start or continue the chat there — or open it on the phone, which links to the active PC.
3. Approve the **app-access** prompt when Claude asks (VS Code, File Explorer, etc.), about 10 seconds.
   Grants are per-session and are **not** stored; the knowledge and scripts below are.

## What auto-loads
- `CLAUDE.md` — build paths, MSYS2 / Rack-SDK setup, gotchas, and the one-click scripts.
- SessionStart hook (`.claude/settings.local.json`) runs `tools/open_workbench.ps1` → workbench on
  `127.0.0.1:8766` (+ the Tailscale address for the phone).

## One-click scripts (double-click in the repo root — no terminal typing)
- `build_and_install.bat` — `make -j4 devinstall`.
- `clean_build.bat` — `make clean` + full rebuild + devinstall.
- `start_workbench.bat` — launch the workbench.
- `sync_push.bat` — `git pull --rebase` then push.

## Notes
- Builds Claude runs may land in the Claude app's sandboxed `%LOCALAPPDATA%`; run
  `Install SynTHBloK into Rack.cmd` on the Desktop so your own Rack sees the new build.
- This file and `CLAUDE.md` are the source of truth, committed to GitHub, so every device gets them with `git pull`.
