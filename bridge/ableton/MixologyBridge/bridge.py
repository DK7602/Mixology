# The Live side of Mixology's bridge: the shared core (mixology_bridge_core.py, the same file as FL Studio's) plus
# this adapter for Live's tracks. Mixology's strips are Live's tracks and return tracks, in mixer order; the Master
# is left alone, as Cubase's Stereo Out is. Everything runs on Live's main thread: messages from Mixology arrive in
# handle_sysex, Live calls update_display about every 100 ms and the core sends what changed.

from _Framework.ControlSurface import ControlSurface

from . import mixology_bridge_core as core


def _db_from_text(text):
    """Live's own fader text ("-6.0 dB", "-inf dB", "0.0 dB") as dB, None for -inf."""
    t = str(text).replace('dB', '').strip()
    if 'inf' in t:
        return None
    try:
        return float(t)
    except ValueError:
        return None


class Live(object):
    def __init__(self, surface):
        self.surface = surface

    def song(self):
        return self.surface.song()

    def track(self, key):
        kind, index = key
        tracks = self.song().tracks if kind == 't' else self.song().return_tracks
        return tracks[index] if index < len(tracks) else None

    # ---- the adapter (see mixology_bridge_core.BridgeCore) ----------------------------------------------------
    def channels(self):
        s = self.song()
        return [('t', i) for i in range(len(s.tracks))] + [('r', i) for i in range(len(s.return_tracks))]

    def name(self, key):
        t = self.track(key)
        return t.name if t is not None else ''

    def volume(self, key):
        p = self.track(key).mixer_device.volume
        span = (p.max - p.min) or 1.0
        n = (p.value - p.min) / span
        return n, (None if p.value <= p.min else _db_from_text(p.str_for_value(p.value)))

    def pan(self, key):
        return self.track(key).mixer_device.panning.value

    def mute(self, key):
        return bool(self.track(key).mute)

    def solo(self, key):
        return bool(self.track(key).solo)

    def colour(self, key):
        try:
            return int(self.track(key).color) & 0xFFFFFF
        except Exception:
            return None

    def writing(self, key):
        # Live records automation for the whole Set: Automation Arm on while it records (Arrangement or Session).
        s = self.song()
        armed = getattr(s, 'session_automation_record', False)
        recording = getattr(s, 'record_mode', False) or getattr(s, 'session_record', False)
        return bool(armed and recording and getattr(s, 'is_playing', False))

    def set_volume(self, key, n):
        p = self.track(key).mixer_device.volume
        p.value = p.min + min(max(n, 0.0), 1.0) * (p.max - p.min)

    def set_pan(self, key, pan):
        p = self.track(key).mixer_device.panning
        p.value = min(max(pan, p.min), p.max)

    def set_mute(self, key, on):
        self.track(key).mute = on

    def set_solo(self, key, on):
        self.track(key).solo = on

    def send(self, frame):
        self.surface._send_midi(tuple(frame))


class MixologyBridge(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        self._live = Live(self)
        self._core = core.BridgeCore(self._live, 'Ableton Live')

    def handle_sysex(self, midi_bytes):
        self._core.on_sysex(midi_bytes)

    def update_display(self):
        ControlSurface.update_display(self)
        self._core.idle()

    def disconnect(self):
        ControlSurface.disconnect(self)
