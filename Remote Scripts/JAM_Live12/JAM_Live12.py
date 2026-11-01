# Maschine JAM control surface for Ableton Live 12 (works in Lite).
# Pairs with the "JAM Template 1 Live.ncmj" Controller Editor template: your JAM's
# Template 1 numbers, with matrix/touch strip page switching turned off.
# Everything is on MIDI channel 1.
import Live
from _Framework.ControlSurface import ControlSurface
from _Framework.InputControlElement import MIDI_NOTE_TYPE, MIDI_CC_TYPE
from _Framework.ButtonElement import ButtonElement
from _Framework.SliderElement import SliderElement
from _Framework.ButtonMatrixElement import ButtonMatrixElement
from _Framework.SessionComponent import SessionComponent
from _Framework.MixerComponent import MixerComponent
from _Framework.TransportComponent import TransportComponent
from _Framework.DeviceComponent import DeviceComponent

CH = 0
NUM_TRACKS = 8
NUM_SCENES = 8

# --- Template 1 MIDI numbers ---------------------------------------------------
# Grid (Matrix Page 1): top-left pad = note 56 (G#2), +1 per column, -8 per row down,
# bottom-left = note 0 (C-2).
MATRIX_TOP_LEFT = 56
SCENE_CCS = range(0, 8)     # scene buttons 1-8 (top row)
GROUP_CCS = range(8, 16)    # group buttons A-H (under the grid)
STRIP_CCS = range(48, 56)   # touch strips A-H (Touch Strip Page A)

# Left side
CC_SONG = 16            # stop all clips
CC_STEP = 17            # metronome
CC_PAD_MODE = 18        # loop on/off
CC_CLEAR = 19           # hold + group = stop track
CC_DUPLICATE = 20       # duplicate selected scene
CC_DPAD_UP = 21
CC_DPAD_LEFT = 22
CC_DPAD_RIGHT = 23
CC_DPAD_DOWN = 24
CC_NOTE_REPEAT = 25     # tap tempo
CC_MACRO = 26           # strips -> pan
CC_LEVEL = 27           # strips -> volume
CC_AUX = 28             # strips -> send A / B
CC_CONTROL = 29         # strips -> device parameters
CC_AUTO = 30            # session record
# Right side
CC_BROWSE = 31          # colour test
CC_PERFORM = 32         # capture MIDI
CC_NOTES = 33           # arrangement overdub
CC_LOCK = 34            # hold + group = mute
CC_TUNE = 35            # hold + group = solo
CC_SWING = 36           # hold + group = arm
CC_SELECT = 37          # hold + group = arm
# Buttons the template moved off their clashing Mackie notes onto free CCs
CC_KNOB_PUSH = 46       # reset what the encoder controls (0 dB / 120 BPM)
CC_GRID = 117           # cycle clip launch quantisation
CC_SOLO = 118           # hold + group = solo
CC_MUTE = 119           # hold + group = mute
# Encoder: MST / GRP / IN1 / CUE pick which CC it sends
CC_ENC_MASTER = 42      # master volume
CC_ENC_GROUP = 43       # tempo
CC_ENC_INPUT = 44       # selected track volume
CC_ENC_CUE = 45         # cue (preview) volume
# Bottom row: Mackie-style notes in Template 1
NOTE_REWIND = 91        # in CONTROL mode: previous device bank
NOTE_FFWD = 92          # in CONTROL mode: next device bank
NOTE_STOP = 93          # the TEMPO button
NOTE_PLAY = 94
NOTE_RECORD = 95

# --- LED colours (Indexed velocity values) --------------------------------------
# JAM palette: each hue is 4 values in a row, dim -> bright
# (red 4-7, orange 8-11, light orange 12-15, warm yellow 16-19, yellow 20-23).
# Press BROWSE to see the real palette on the grid, then tune these numbers.
#
# Yellow -> deep dark red, one step per column A-H / scene 1-8: (on/bright, off/dim).
GRADIENT = [
    (23, 21),   # yellow
    (19, 17),   # warm yellow
    (15, 13),   # light orange
    (11, 9),    # orange
    (7, 5),     # red
    (7, 4),     # red, darker when off
    (6, 4),     # dark red
    (5, 4),     # deep dark red
]
COLUMN_COLORS = GRADIENT    # grid columns + group buttons A-H: playing = on, has clip = off
SCENE_COLORS = GRADIENT     # scene buttons 1-8: launching = on, scene has clips = off
COLOR_OFF = 0
COLOR_RECORDING = 7         # bright red, flashing
COLOR_ARMED_EMPTY = 4       # dim red: empty slot on an armed track
BLINK_TICKS = 3             # flash speed: update_display ticks (~100 ms each) per on/off step


class JAM_Live12(ControlSurface):

    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        self._strip_mode = 'volume'
        self._send_index = 0
        self._group_mode = 'select'
        self._color_test_page = -1
        self._encoder_last = {}
        with self.component_guard():
            self._make_controls()
            self._make_session()
            self._make_mixer()
            self._make_device()
            self._make_transport()
            self._make_buttons()
            self._assign_strips()
            self._assign_groups()
        self._add_song_listeners()
        self.show_message('Maschine JAM connected')

    # ---- controls ----
    def _note(self, n):
        return ButtonElement(True, MIDI_NOTE_TYPE, CH, n)

    def _cc_button(self, n):
        return ButtonElement(True, MIDI_CC_TYPE, CH, n)

    def _make_controls(self):
        self._pads = [[self._note(MATRIX_TOP_LEFT - 8 * r + c) for c in range(NUM_TRACKS)]
                      for r in range(NUM_SCENES)]
        self._matrix = ButtonMatrixElement(rows=self._pads)
        self._scene_buttons = [self._cc_button(n) for n in SCENE_CCS]
        self._group_buttons = [self._cc_button(n) for n in GROUP_CCS]
        # A-H light in their column's colour: bright when on (selected/muted/...), dim when off.
        for b, (on, off) in zip(self._group_buttons, COLUMN_COLORS):
            b.set_on_off_values(on, off)
        self._strips = [SliderElement(MIDI_CC_TYPE, CH, n) for n in STRIP_CCS]
        self._rewind = self._note(NOTE_REWIND)
        self._ffwd = self._note(NOTE_FFWD)

    # ---- session: grid = 8 tracks x 8 scenes ----
    def _make_session(self):
        s = SessionComponent(NUM_TRACKS, NUM_SCENES)
        s.name = 'Session'
        s.set_clip_launch_buttons(self._matrix)
        s.set_track_bank_buttons(self._cc_button(CC_DPAD_RIGHT), self._cc_button(CC_DPAD_LEFT))
        s.set_scene_bank_buttons(self._cc_button(CC_DPAD_DOWN), self._cc_button(CC_DPAD_UP))
        s.set_stop_all_clips_button(self._cc_button(CC_SONG))
        for i in range(NUM_SCENES):
            scene = s.scene(i)
            scene_on, scene_off = SCENE_COLORS[i]
            scene.set_launch_button(self._scene_buttons[i])
            scene.set_triggered_value(scene_on)
            if hasattr(scene, 'set_scene_value'):
                scene.set_scene_value(scene_off)
                scene.set_no_scene_value(COLOR_OFF)
            for t in range(NUM_TRACKS):
                on, off = COLUMN_COLORS[t]
                slot = scene.clip_slot(t)
                slot.set_stopped_value(off)
                slot.set_started_value(on)
                slot.set_recording_value(COLOR_RECORDING)
                slot.set_triggered_to_play_value(on)
                slot.set_triggered_to_record_value(COLOR_RECORDING)
                slot.set_record_button_value(COLOR_ARMED_EMPTY)
        self._session = s
        self.set_highlighting_session_component(s)

    def _make_mixer(self):
        self._mixer = MixerComponent(NUM_TRACKS)
        self._mixer.name = 'Mixer'
        self._session.set_mixer(self._mixer)

    def _make_device(self):
        try:
            self._device = DeviceComponent(device_selection_follows_track_selection=True)
        except TypeError:
            self._device = DeviceComponent()
        self._device.name = 'Device'
        self.set_device_component(self._device)

    def _make_transport(self):
        t = TransportComponent()
        t.name = 'Transport'
        t.set_record_button(self._note(NOTE_RECORD))
        t.set_tap_tempo_button(self._cc_button(CC_NOTE_REPEAT))
        t.set_metronome_button(self._cc_button(CC_STEP))
        t.set_loop_button(self._cc_button(CC_PAD_MODE))
        t.set_overdub_button(self._cc_button(CC_NOTES))
        self._transport = t

    # Buttons the script handles itself.
    def _make_buttons(self):
        presses = {
            CC_AUTO: self._on_session_record,
            CC_LEVEL: lambda: self._set_strip_mode('volume'),
            CC_MACRO: lambda: self._set_strip_mode('pan'),
            CC_AUX: self._on_aux,
            CC_CONTROL: lambda: self._set_strip_mode('device'),
            CC_DUPLICATE: self._on_duplicate,
            CC_PERFORM: self._on_capture,
            CC_BROWSE: self._on_color_test,
            CC_GRID: self._on_grid,
            CC_KNOB_PUSH: self._on_knob_push,
        }
        modifiers = {CC_LOCK: 'mute', CC_TUNE: 'solo', CC_SWING: 'arm', CC_CLEAR: 'stop',
                     CC_MUTE: 'mute', CC_SOLO: 'solo', CC_SELECT: 'arm'}
        self._buttons = {}
        for cc, handler in presses.items():
            b = self._cc_button(cc)
            b.add_value_listener(self._make_press(handler))
            self._buttons[cc] = b
        for cc, mode in modifiers.items():
            b = self._cc_button(cc)
            b.add_value_listener(self._make_modifier(mode))
            self._buttons[cc] = b
        for note, handler in ((NOTE_PLAY, self._on_play), (NOTE_STOP, self._on_stop)):
            b = self._note(note)
            b.add_value_listener(self._make_press(handler))
            self._buttons[note] = b
        self._rewind.add_value_listener(self._make_press(lambda: self._on_seek(-4.0)))
        self._ffwd.add_value_listener(self._make_press(lambda: self._on_seek(4.0)))
        self._encoders = {}
        for cc in (CC_ENC_MASTER, CC_ENC_GROUP, CC_ENC_INPUT, CC_ENC_CUE):
            e = SliderElement(MIDI_CC_TYPE, CH, cc)
            e.add_value_listener(self._make_encoder(cc))
            self._encoders[cc] = e

    def _make_press(self, handler):
        def listener(value):
            if value:
                handler()
        return listener

    def _make_modifier(self, mode):
        def listener(value):
            self._group_mode = mode if value else 'select'
            self._assign_groups()
        return listener

    def _light(self, key, on):
        b = self._buttons.get(key)
        if b:
            b.send_value(127 if on else 0, True)

    # ---- touch strips ----
    def _on_aux(self):
        if self._strip_mode == 'send':
            self._send_index = 1 - self._send_index
        self._set_strip_mode('send')

    def _set_strip_mode(self, mode):
        self._strip_mode = mode
        self._assign_strips()
        names = {'volume': 'Strips: Volume', 'pan': 'Strips: Pan',
                 'send': 'Strips: Send %s' % 'AB'[self._send_index],
                 'device': 'Strips: Device parameters (< > change bank)'}
        self.show_message(names[mode])

    def _assign_strips(self):
        with self.component_guard():
            self._device.set_parameter_controls(None)
            self._device.set_bank_nav_buttons(None, None)
            for i in range(NUM_TRACKS):
                strip = self._mixer.channel_strip(i)
                strip.set_volume_control(None)
                strip.set_pan_control(None)
                strip.set_send_controls(None)
            for i in range(NUM_TRACKS):
                strip = self._mixer.channel_strip(i)
                ctl = self._strips[i]
                if self._strip_mode == 'volume':
                    strip.set_volume_control(ctl)
                elif self._strip_mode == 'pan':
                    strip.set_pan_control(ctl)
                elif self._strip_mode == 'send':
                    sends = [None, None]
                    sends[self._send_index] = ctl
                    strip.set_send_controls(tuple(sends))
            if self._strip_mode == 'device':
                self._device.set_parameter_controls(tuple(self._strips))
                self._device.set_bank_nav_buttons(self._rewind, self._ffwd)
        self._light(CC_LEVEL, self._strip_mode == 'volume')
        self._light(CC_MACRO, self._strip_mode == 'pan')
        self._light(CC_AUX, self._strip_mode == 'send')
        self._light(CC_CONTROL, self._strip_mode == 'device')
        self.request_rebuild_midi_map()

    # ---- group buttons A-H: select, or mute/solo/arm/stop while holding a modifier ----
    def _assign_groups(self):
        mode = self._group_mode
        with self.component_guard():
            self._session.set_stop_track_clip_buttons(None)
            for i in range(NUM_TRACKS):
                strip = self._mixer.channel_strip(i)
                strip.set_select_button(None)
                strip.set_mute_button(None)
                strip.set_solo_button(None)
                strip.set_arm_button(None)
            for i in range(NUM_TRACKS):
                strip = self._mixer.channel_strip(i)
                b = self._group_buttons[i]
                if mode == 'select':
                    strip.set_select_button(b)
                elif mode == 'mute':
                    strip.set_mute_button(b)
                elif mode == 'solo':
                    strip.set_solo_button(b)
                elif mode == 'arm':
                    strip.set_arm_button(b)
            if mode == 'stop':
                self._session.set_stop_track_clip_buttons(tuple(self._group_buttons))
        for cc in (CC_LOCK, CC_MUTE):
            self._light(cc, mode == 'mute')
        for cc in (CC_TUNE, CC_SOLO):
            self._light(cc, mode == 'solo')
        for cc in (CC_SWING, CC_SELECT):
            self._light(cc, mode == 'arm')
        self._light(CC_CLEAR, mode == 'stop')

    # ---- encoder: sends 0-127 and wraps, so work from the change between messages ----
    def _make_encoder(self, cc):
        def listener(value):
            last = self._encoder_last.get(cc)
            self._encoder_last[cc] = value
            if last is None:
                return
            delta = value - last
            if delta > 64:
                delta -= 128
            elif delta < -64:
                delta += 128
            if delta:
                self._on_encoder(cc, delta)
        return listener

    def _encoder_param(self, cc):
        song = self.song()
        if cc == CC_ENC_MASTER:
            return song.master_track.mixer_device.volume
        if cc == CC_ENC_CUE:
            return song.master_track.mixer_device.cue_volume
        return song.view.selected_track.mixer_device.volume

    def _on_encoder(self, cc, delta):
        self._last_encoder = cc
        song = self.song()
        if cc == CC_ENC_GROUP:
            song.tempo = max(20.0, min(999.0, song.tempo + delta))
            return
        p = self._encoder_param(cc)
        p.value = max(p.min, min(p.max, p.value + delta * 0.01))

    # Knob push: put whatever the encoder last turned back to its default.
    def _on_knob_push(self):
        cc = getattr(self, '_last_encoder', CC_ENC_MASTER)
        if cc == CC_ENC_GROUP:
            self.song().tempo = 120.0
            self.show_message('Tempo reset to 120 BPM')
        else:
            p = self._encoder_param(cc)
            p.value = p.default_value
            self.show_message('%s reset' % p.name)

    # GRID: step through clip launch quantisation.
    def _on_grid(self):
        Q = Live.Song.Quantization
        steps = [(Q.q_bar, '1 bar'), (Q.q_quarter, '1/4'), (Q.q_eight, '1/8'),
                 (Q.q_sixtenth, '1/16'), (Q.q_no_q, 'none')]
        song = self.song()
        values = [v for v, _ in steps]
        i = values.index(song.clip_trigger_quantization) + 1 if song.clip_trigger_quantization in values else 0
        value, name = steps[i % len(steps)]
        song.clip_trigger_quantization = value
        self.show_message('Launch quantisation: ' + name)

    def _update_grid_led(self):
        self._light(CC_GRID, self.song().clip_trigger_quantization != Live.Song.Quantization.q_no_q)

    # ---- transport ----
    def _on_play(self):
        song = self.song()
        if song.is_playing:
            song.stop_playing()
        else:
            song.start_playing()

    def _on_stop(self):
        self.song().stop_playing()

    def _on_seek(self, beats):
        if self._strip_mode != 'device':
            self.song().jump_by(beats)

    def _on_session_record(self):
        song = self.song()
        song.session_record = not song.session_record

    def _on_duplicate(self):
        song = self.song()
        scenes = list(song.scenes)
        if song.view.selected_scene in scenes:
            song.duplicate_scene(scenes.index(song.view.selected_scene))

    def _on_capture(self):
        try:
            self.song().capture_midi()
        except Exception:
            self.show_message('Nothing to capture')

    def _add_song_listeners(self):
        song = self.song()
        song.add_is_playing_listener(self._update_play_led)
        song.add_session_record_listener(self._update_record_led)
        song.add_clip_trigger_quantization_listener(self._update_grid_led)
        self._update_play_led()
        self._update_record_led()
        self._update_grid_led()

    def _update_play_led(self):
        self._light(NOTE_PLAY, self.song().is_playing)

    def _update_record_led(self):
        self._light(CC_AUTO, self.song().session_record)

    # ---- colour test: BROWSE cycles grid through velocities 0-63, 64-127, off ----
    def _on_color_test(self):
        self._color_test_page = self._color_test_page + 1 if self._color_test_page < 1 else -1
        if self._color_test_page < 0:
            self._session.set_enabled(True)
            self.show_message('Colour test off')
            return
        self._session.set_enabled(False)
        base = 64 * self._color_test_page
        for r in range(NUM_SCENES):
            for c in range(NUM_TRACKS):
                self._pads[r][c].send_value(base + 8 * r + c, True)
        self.show_message('Colour test: top-left pad = %d, +1 across, +8 down' % base)

    # ---- flashing: queued clips blink, recording clips pulse (update_display runs ~10x a second) ----
    def update_display(self):
        ControlSurface.update_display(self)
        self._blink_tick = (getattr(self, '_blink_tick', 0) + 1) % BLINK_TICKS
        if self._blink_tick or self._color_test_page >= 0:
            return
        self._blink_on = not getattr(self, '_blink_on', True)
        tracks = self._session.tracks_to_use()
        scenes = self.song().scenes
        t0, s0 = self._session.track_offset(), self._session.scene_offset()
        for r in range(NUM_SCENES):
            if s0 + r >= len(scenes):
                break
            for c in range(NUM_TRACKS):
                if t0 + c >= len(tracks):
                    break
                slot = tracks[t0 + c].clip_slots[s0 + r]
                clip = slot.clip if slot.has_clip else None
                if slot.is_triggered or (clip and clip.is_triggered):
                    color = COLOR_RECORDING if slot.will_record_on_start else COLUMN_COLORS[c][0]
                elif clip and clip.is_recording:
                    color = COLOR_RECORDING
                else:
                    continue
                self._pads[r][c].send_value(color if self._blink_on else COLOR_OFF, True)

    def disconnect(self):
        song = self.song()
        if song.is_playing_has_listener(self._update_play_led):
            song.remove_is_playing_listener(self._update_play_led)
        if song.session_record_has_listener(self._update_record_led):
            song.remove_session_record_listener(self._update_record_led)
        if song.clip_trigger_quantization_has_listener(self._update_grid_led):
            song.remove_clip_trigger_quantization_listener(self._update_grid_led)
        ControlSurface.disconnect(self)
