# SynTHBloK delay + looper module: design notes

Goal: turn the FINAL-stage DELAY / SPACE effect from MATRIX-DRUM / MATRIX-DUO into a standalone VCV Rack 2
module that's roughly on par with TC Electronic's Flashback X4 delay & looper pedal.

## 1. What the effect does now

Both effects live in `src/Matrix8_dsp.hpp`. MATRIX-8, MATRIX FORGE, MATRIX-DUO and MATRIX-DRUM all share them.

- **`M8Delay`**: a stereo ping-pong delay with a 2.1 s buffer and linear-interpolated taps.
  - Its time is locked to the host clock at a dotted 1/8 (`stepSec * 3`). A one-pole glide (0.0002 per sample) smooths tempo changes.
  - The loop has a one-pole low-pass (coefficient 0.35), so each repeat gets darker.
  - Each side feeds the other at a fixed 0.5 feedback (cross-feed).
  - There are no user controls except the wet amount. The host adds `DELAY * 0.8` of the wet signal.
- **`M8Reverb`**: a small Freeverb-style reverb with 4 damped combs (fb 0.84, damp 0.3) and 2 allpasses per side, fed in mono at 0.15.
  - The host adds `SPACE` of the wet signal, so SPACE is a send amount.

The two effects run one after the other: the reverb hears the dry signal plus the delay.

## 2. Flashback X4 reference

No pedal is sold as a "Flashback 4". The 4-switch models are the **Flashback X4** (2012) and the
**Flashback 2 X4** (2019).

**Delay types (union of both models)**
| Type | Character |
|---|---|
| 2290 | clean digital (TC 2290 rack), bright, accurate repeats |
| 2290 MOD | 2290 with chorus modulation on the repeats |
| ANALOG | BBD-style: repeats darken and saturate |
| ANALOG MOD | analog with a slow pitch wobble |
| TAPE | wow and flutter, tape saturation, HF loss; time changes bend the pitch |
| TUBE | warm, driven repeats |
| SPACE | multi-head tape echo, repeats smeared into reverb |
| LO-FI | bit and sample-rate reduced, band-limited |
| DYNAMIC | ducking: the wet signal drops while you play and blooms in the gaps |
| REVERSE | each delay-length slice played backwards |
| PING-PONG | repeats alternate L / R |
| SLAPBACK | 20 to 300 ms single slap |
| CRYSTAL (X4 v2) | octave-up shimmer in the feedback path |
| TonePrint slots | user / artist algorithms |

- **Delay range:** 20 ms to 7 s (SLAPBACK 20 to 300 ms).
- **Knobs:** TYPE, DELAY (time), FEEDBACK, LEVEL (wet; the dry signal stays at unity), and SUBDIV, which has 11 positions: 1/4, dotted 1/8, 1/8, 1/8 triplet, three dual-delay pairs (for example 1/4 + dotted 1/8) and a custom setting. The v1 used a 3-way subdivision switch.
- **Tap tempo:** a dedicated footswitch, with an optional *global tempo* so presets keep the tapped tempo.
- **Kill-dry** switch. **Buffered bypass with spillover**, so the tails ring on after bypass.
- **Expression pedal input**, which morphs between two settings.
- **Presets:** 4 (X4) or 6 (2 banks × 3) on the A / B / C footswitches, plus TonePrint slots.
- **MASH** (X4 v2): pressure-sensing footswitches. Pressing harder sweeps up to 3 parameters along editable curves, for example more feedback toward self-oscillation, a time warp or a tone sweep.
- **Looper** (mode knob set to LOOP; the footswitches become the transport):
  - RECORD / OVERDUB, PLAY / STOP, PLAY ONCE, UNDO / REDO
  - 40 s mono or 20 s stereo
  - unlimited overdubs; UNDO reverts only the last layer
  - the LEVEL knob sets the loop volume
- **Stereo in and out.**

## 3. Proposed module (pending the user's choices)

- **Name:** working title MATRIX-ECHO, in the house MATRIX-DUO OLED look (`src/OledUi.hpp`). The knobs get amber value rings and `KnobTicks`.
- **Engines** in `src/Echo_dsp.hpp`:
  - The current `M8Delay` / `M8Reverb` move there unchanged and become the **CLASSIC** engine. That way DRUM / DUO / MATRIX-8 / FORGE share one copy and sound bit-for-bit the same.
  - New engines: 2290, 2290 MOD, ANALOG, TAPE, LO-FI, DYNAMIC, REVERSE, PING-PONG, SLAP, CRYSTAL, SPACE (delay into reverb).
- **Controls:**
  - TIME, SUBDIV, FEEDBACK, LEVEL, TONE, MOD
  - SPACE (reverb on the repeats)
  - MASH, a big knob or gate+CV that pushes feedback, mod and level together
  - a TAP button, a KILL DRY switch, and TRAILS on bypass
- **Inputs:** IN L / R, CLOCK (sets the time via SUBDIV), TAP, and CV for TIME / FEEDBACK / LEVEL / TONE / MASH. Also gates for BYPASS and FREEZE.
- **Looper:**
  - stereo, 60 s
  - REC / OVERDUB, PLAY / STOP, PLAY ONCE, UNDO / REDO, CLEAR
  - optional loop-length quantise to the clock
  - optional loop before or after the delay
  - trigger inputs for each action
- **Presets:** 6 slots (2 banks × 3) stored in the patch.
- **Outputs:** OUT L / R, plus WET L / R.
