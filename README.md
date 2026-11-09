# SynTHBloK (free singles)

**SynTHBloK** by **NoiZee ToyZ**: free VCV Rack 2 modules for hard techno and live grooveboxes. Synth voices, drum
synths, filters, effects, mixers, a mastering strip and a sample player, each its own small plugin.

![SynTHBloK QUAD](web-images/screenshots/SynTHBloK.png)

## Install

In VCV Rack 2: open the **Library**, search **SynTHBloK** and add the ones you want. Rack downloads and updates them.
Prebuilt packages for every platform are also on the [Releases](https://github.com/nickkollmann-ai/SynTHBloK/releases)
page: put a `.vcvplugin` file in Rack's `plugins` folder and restart Rack.

## Modules

| Module | What it does | Plugin |
|---|---|---|
| **QUAD** | Quad-oscillator synth voice with filter, envelope, glide, accent and LFO | `SynTHBloK-Quad` |
| **Poly** | Polyphonic unison engine for SynTHBloK | `SynTHBloK-Poly` |
| **Stereo Phaser** | Stereo width, tilt, air, glue, punch and clip, with analyzer | `SynTHBloK-StereoPhaser` |
| **Dual Filter** | Two stereo multimode filters (LP / BP / notch / HP sweep, 12 / 24 / 48 dB) with drive, run in series, parallel or stereo, linked cutoffs, a four-shape LFO, an ADSR or envelope follower, a stereo phaser and a three-band EQ | `SynTHBloK-DualFilter` |
| **Morph Filter** | Stereo drive into a state-variable filter that morphs LP to BP to HP, with audio-rate FM, 1V/oct keytrack, random chaos drift and cutoff spread | `SynTHBloK-MorphFilter` |
| **Resonator** | Stereo modal resonator with metal, wood, membrane and tube bodies | `SynTHBloK-Resonator` |
| **Character Delay** | Stereo delay with eight characters (clean, tape, analog, modern, ping-pong, dynamic, space, reverse), clock sync, hold and clear | `SynTHBloK-CharacterDelay` |
| **GRAIN-FORGE** | Beat shuffler, glitch, robot vocoder, clock-locked gate/filter and a warp delay whose repeats loop back through the robot and pulse stages; clock-locked stereo FX chain | `SynTHBloK-GrainForge` |
| **BloKMaster** | Mastering strip: trim, tilt EQ, shelves, glue compressor, punch, limiter, stereo tools, air and output gain | `SynTHBloK-BloKMaster` |
| **Kick** | Hard techno kick: pitch-swept sine, click, drive, tone, schranz rumble tail with ducking, accent and envelope out | `SynTHBloK-Kick` |
| **Snare/Hat** | Techno snare and hi-hat voice: two-oscillator snare body with noise snap, six-oscillator metallic hats with closed/open triggers and choke, drive, tone and accent | `SynTHBloK-SnareHat` |
| **Euclid** | Four-channel Euclidean rhythm generator: steps, hits and rotate per channel, clock/reset/run inputs, 10 ms trigger outputs for drums and gates | `SynTHBloK-Euclid` |
| **JAM Probe** | MIDI monitor and test transmitter for mapping controllers such as the Maschine JAM: live message log, per-control statistics, test notes/CCs, auto sweep, copy/save map summary | `SynTHBloK-JamProbe` |
| **MIX-6 Fire Edition** | Six-channel stereo mixer with per-channel tilt, drive, air and glue, mute/solo, meters and an aux FX bus | `SynTHBloK-MixFire` |
| **MIX-8 Fire Edition** | Eight-channel stereo mixer with per-channel tilt, drive, air and glue, mute/solo, meters and an aux FX bus | `SynTHBloK-MixFire` |
| **MIX-12 Fire Edition** | Twelve-channel stereo mixer with per-channel tilt, drive, air and glue, mute/solo, meters and an aux FX bus | `SynTHBloK-MixFire` |
| **Player Plus** | MIDI file player with a watch folder: 16 poly tracks (pitch, gate, velocity), 16th-note clock, reset and end outputs, speed and transpose | `SynTHBloK-PlayerPlus` |
| **MIDI + Expansion** | MIDI display for Player Plus: scrolling piano roll of the loaded MIDI with playhead, track activity and transport readout | `SynTHBloK-PlayerPlus` |
| **BLANK** | A NoiZee ToyZ blank panel for empty rack space: badge, backlit or circuit look, 1 to 32 HP | `SynTHBloK-Blanks` |

The MTX pack (the 8x8 sequencer family, Composer, JAM, RANDROYD) and BACKDROP are separate NoiZee ToyZ products.

## Build from source

You need the [VCV Rack SDK](https://vcvrack.com/manual/Building) 2.6.x. Each plugin builds from its own folder:

```
cd singles/SynTHBloK-Kick
make RACK_DIR=/path/to/Rack-SDK        # or: make dist / make install
```

## Licence

GPL-3.0-or-later (see `LICENSE.txt`). The SynTHBloK and NoiZee ToyZ names and logos are not covered by the licence.
