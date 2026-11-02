# Native Instruments drum samples on this machine (scanned 2026-11-01)

All of the content is on C: under `C:\Users\Public\Documents\<Library>\`; the registry's `ContentDir` values
point there. D: (Recovery) and F: hold no NI libraries.

NI libraries put their samples in one standard layout, which makes them easy to index:

`<Library>\Samples\Drums\<Category>\<Category> <Family> <n>.wav`, for example
`Battery 4 Factory Library\Samples\Drums\Kick\Kick 909 1.wav`.

Everything is plain WAV. There are no NCW files (Kontakt-compressed) anywhere in the drum folders.

## How many

| Library | Drum WAVs | Notes |
|---|---:|---|
| Battery 4 Factory Library | ~10,300 | Kick 1.6k, Snare 2.6k, HiHat 1.9k, Tom 0.9k, Cymbal 0.8k, Clap 0.4k, Shaker, Metallic, Mallet, Wooden, Hand Drum |
| Maschine 2 Factory Library | ~10,000 | Kick 1.4k, Hihat 1.4k, Percussion 1.4k, Snare 1.2k, Tom 0.9k, Cymbal 0.9k, Clap 0.6k, Shaker |
| 34 Maschine expansions | 350–780 each, ~18,000 in total | Carbon Decay, Dark Pressure, Motor Impact, Body Mechanik, Ignition Code, Infinite Escape, Deep Matter, Grey Forge, Pulswerk, Static Friction, Decoded Forms, Void Eclipse, True School, Rhythm Source, Neo Boogie, … all with the same `Samples\Drums\…` layout |
| Shades of Techno, ISR Rudeboy Dubstep, Hyper Dubstep, Platinum Breaks | ~320 | third-party Maschine kits, 16 samples per kit folder |
| **Total** | **~40,000 drum one-shots** | |

## Ready-made kits

Within a library, the second word of a file name is a kit family, for example `Kick 909 1`, `Snare 909 1` and `HiHat 909 Closed`.

- **Battery 4** has 138 families with at least a kick, a snare and a hat. They include drum machines (606, 707, 7T8, 808, 909, DMX, DrumTraks, LinnDrum, RZ1, SP12, CR-78 style), genre kits (House, Garage, Dub, HipHop, Funk, Elektro, Glitch, TerrorCore) and artist kits (Liebing, Apparat, Funkstorung, Krypt, Toksin).
- **Maschine 2** has 317 more families, including 808/909, CR78, LM1, LinnDrum, DMX, Abbey Road vintage kits (AR50s, AR60s, AR70s), Bodzin, Huntemann, ChrisLiebing, Dubstep, JazzBrush, Junglecore and Trapoholic.
- **Each expansion** is one genre in itself (Dark Pressure: dark techno; Pulswerk: minimal; True School: boom bap; and so on), so a kit can also be built as "a random kick, snare, hat … from one expansion".

That is about 450 named kits, plus every expansion as a pool to draw from.

## Licensing: what we can't do

These samples are licensed to you for making music. **They can't be copied into the plugin (`res/`) or shipped with it.** MATRIX-DRUM has to read them from your disk at run time, saving only paths in the patch. That's fine for your own use. Anyone else would need their own NI libraries; it would be up to their machine to find them, and the module would fall back to the synth models when they can't be found.

## How it could plug into MATRIX-DRUM

1. **A SAMPLE voice model** (a 9th model next to KICK … NOISE). A row plays a WAV and the 8 macros keep their meaning:
   - TUNE: pitch, ±12 semitones
   - DECAY: amp envelope
   - TONE: tilt / low-pass
   - SNAP: start offset and transient boost
   - MOD: reverse or bit-crush amount
   - DRIVE, LEVEL and PAN: as now
   - accents: as now
2. **A kit index** built once by a scan, like the one above, and saved under Rack's user folder:
   - one kit = 8 sample paths, matched to the rows' roles: kick, snare, clap, closed hat, open hat, tom, perc or metal, and FM or shaker
   - families come from the file names, and expansions are offered as pools
3. **A KIT selector** on the voice OLED (a library and kit name; click or scroll to step through them) and a right-click browser.
4. **RAND loads a different kit with each groove.** Each of the 8 patterns gets a kit that suits its style, for example a 909 or Liebing kit for house/techno, a breaks kit for breakbeat, an 808 or Elektro kit for electro, and a Garage or Glitch kit for broken beat. The kit is stored per pattern, as the voice knobs are now.
5. **WAV loading:** `dr_wav.h` (public domain, a single header) in `src/`. Samples are loaded and resampled to the engine rate off the audio thread. Only the 8 rows × 8 patterns in use are kept in memory, at a few MB each.
