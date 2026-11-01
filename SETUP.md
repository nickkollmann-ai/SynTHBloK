# Maschine JAM + Ableton Live 12: setup

This uses your JAM's **Template 1** numbers (the ones in Controller Editor), with two changes:
- **Matrix and touch strip page switching is off.** Scene buttons 1–8 and group buttons A–H now send MIDI
  instead of flipping pages, the grid stays on Matrix Page 1, and the strips stay on Page A (CC 48–55).
- **Encoder push is off.** It sent the same notes (42–45) as four grid pads.

## Install
1. **Template:** in Controller Editor, go to File → Open Template → `JAM Template 1 Live.ncmj`.
   Click it in the Templates list so it's active. Leave **Host Transport Control unticked**.
   To check it's active: press scene button 1. It should no longer switch matrix pages.
2. **Script:** the `Remote Scripts\JAM_Live12` folder goes in
   `Documents\Native Instruments\User Library\Remote Scripts\` (already installed).
3. Restart Live. Go to Settings → Link, Tempo & MIDI, then under Control Surface choose **JAM_Live12**.
   For both Input and Output, pick **Maschine Jam - 1**.
4. Put the JAM in MIDI mode with **SHIFT + MIDI (headphones button)**.

## What each control does
| Control | MIDI | Action |
|---|---|---|
| 8×8 grid | notes 0–63 | Launch clips (columns = tracks, rows = scenes) |
| Scene buttons 1–8 | CC 0–7 | Launch scenes |
| Group A–H | CC 8–15 | Select track |
| Hold LOCK / TUNE / SWING / CLEAR + group | CC 34 / 35 / 36 / 19 | Mute / solo / arm / stop that track |
| Touch strips A–H | CC 48–55 | Track volumes (default) |
| LEVEL / MACRO / AUX / CONTROL | CC 27 / 26 / 28 / 29 | Strips = volume / pan / send A (AUX again for B) / device knobs |
| D-pad | CC 21–24 | Move the 8×8 box around the Live set |
| SONG | CC 16 | Stop all clips |
| STEP | CC 17 | Metronome |
| PAD MODE | CC 18 | Loop on/off |
| DUPLICATE | CC 20 | Duplicate selected scene |
| NOTE REPEAT | CC 25 | Tap tempo |
| AUTO | CC 30 | Session record |
| BROWSE | CC 31 | Colour test (see below) |
| PERFORM | CC 32 | Capture MIDI |
| NOTES | CC 33 | Arrangement overdub |
| Encoder on MST / GRP / IN1 / CUE page | CC 42 / 43 / 44 / 45 | Master volume / tempo / selected track volume / cue volume |
| PLAY | note 94 | Play / stop |
| REC | note 95 | Arrangement record |
| ◀ ▶ | notes 91 / 92 | Back / forward 1 bar (in CONTROL mode: device bank) |
| TEMPO (labelled "Stop") | note 93 | Stop |

## If something's off
- **Wrong colours:** press BROWSE. The grid shows LED values 0–63 (press again for 64–127, a third time to exit).
  The top-left pad is 0, +1 per step across and +8 per step down. Put the values you like into the `COLOR_*` lines in `JAM_Live12.py`.
- **Script not in the list:** Live writes errors to `%APPDATA%\Ableton\Live 12.4.6\Preferences\Log.txt`.
  Search that file for `JAM_Live12`.

After you change the script, restart Live or reselect the control surface.
`make_template1_live.ps1` rebuilds the template from Controller Editor's current Template 1.
