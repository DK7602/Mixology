# name=Mixology Bridge
# Mixology Bridge for FL Studio: lets the Mixology plug-in (Sonicology) read and move FL's mixer faders, pans,
# mute and solo, the way it does in Cubase.
#
# Install: this folder ("Mixology Bridge", with this file and mixology_bridge_core.py) goes into
#   Documents\Image-Line\FL Studio\Settings\Hardware\        (Mac: ~/Documents/Image-Line/FL Studio/Settings/Hardware/)
# The Sonicology installer puts it there. Then in FL Studio, Options > MIDI settings:
#   Input  "Mixology to Cubase": enable it, Controller type "Mixology Bridge", Port 200
#   Output "Cubase to Mixology": Port 200 (the same number: FL sends the script's replies to the output with it)
# (Windows: the two ports come from loopMIDI; on a Mac Mixology makes them itself.)
#
# Only mixer inserts in use are Mixology's strips: renamed ones, and ones with an effect (Mixology Link is one).
# Master and the "Current" track are left alone, as Cubase's Stereo Out is.

import math

import device
import midi
import mixer
import mixology_bridge_core as core

try:
    import plugins
except ImportError:   # older FL Studio: inserts count when renamed
    plugins = None

FIRST_INSERT = 1
RESCAN_TICKS = 25        # OnIdle runs about every 20 ms: the list of inserts in use again every half second
MIXER_SLOTS = 10


def _gain_db(v):
    """FL's fader law (as community scripts use it): 0.8 = 0 dB, 1.0 = +5.6 dB."""
    g = (pow(11.0, 1.25 * v) - 1.0) / 10.0
    return None if g <= 1e-9 else 20.0 * math.log10(g)


class FLStudio(object):
    def __init__(self):
        self.ticks = RESCAN_TICKS
        self.inserts = []
        self.any_solo = False
        self.mutes = {}   # each insert's own mute, as it was before a solo (FL's solo switches the others off)

    # ---- the inserts in use ----------------------------------------------------------------------------------
    def last_insert(self):
        try:
            return mixer.getTrackInfo(midi.TN_LastIns)
        except Exception:
            return mixer.trackCount() - 2   # count includes Master and the "Current" track

    def in_use(self, i):
        name = mixer.getTrackName(i) or ''
        if name and not (name.startswith('Insert ') and name[7:].strip().isdigit()):
            return True
        if plugins is not None:
            for slot in range(MIXER_SLOTS):
                try:
                    if plugins.isValid(i, slot):
                        return True
                except Exception:
                    return False
        return False

    def rescan(self):
        self.inserts = [i for i in range(FIRST_INSERT, self.last_insert() + 1) if self.in_use(i)]

    def tick(self):
        # Names and effects are checked twice a second (125 inserts: not on every 20 ms tick); a fader's own
        # values are read on every tick by the core.
        self.ticks += 1
        if self.ticks >= RESCAN_TICKS:
            self.ticks = 0
            self.rescan()
        # FL solos an insert by muting the others: while one is soloed, Mixology goes on seeing the mutes from
        # before (it knows who is heard from the solo itself), so Restore and the History never "unmute" them.
        self.any_solo = any(mixer.isTrackSolo(i) for i in self.inserts)
        if not self.any_solo:
            self.mutes = dict((i, bool(mixer.isTrackMuted(i))) for i in self.inserts)

    def soon(self):
        self.ticks = max(self.ticks, RESCAN_TICKS - 5)   # a rename or a new effect: look again within 0.1 s

    # ---- the adapter (see mixology_bridge_core.BridgeCore) ----------------------------------------------------
    def channels(self):
        return self.inserts

    def name(self, i):
        return mixer.getTrackName(i) or ''

    def volume(self, i):
        v = mixer.getTrackVolume(i)
        try:
            db = mixer.getTrackVolume(i, 1)   # dB (API 14 and later)
            db = None if db is None or db != db or db < -200.0 else float(db)
        except TypeError:
            db = _gain_db(v)
        if v <= 0.0:
            db = None
        return v, db

    def pan(self, i):
        return mixer.getTrackPan(i)

    def mute(self, i):
        if self.any_solo:
            return self.mutes.get(i, False)
        return bool(mixer.isTrackMuted(i))

    def solo(self, i):
        return bool(mixer.isTrackSolo(i))

    def colour(self, i):
        try:
            return mixer.getTrackColor(i) & 0xFFFFFF
        except Exception:
            return None

    def writing(self, i):
        return False   # FL records automation per control, not per track: nothing to refuse

    def set_volume(self, i, v):
        v = min(max(v, 0.0), 1.0)
        try:
            mixer.setTrackVolume(i, v, midi.PIM_None)
        except (TypeError, AttributeError):   # FL before scripting API 13: no pickup argument
            mixer.setTrackVolume(i, v)

    def set_pan(self, i, p):
        p = min(max(p, -1.0), 1.0)
        try:
            mixer.setTrackPan(i, p, midi.PIM_None)
        except (TypeError, AttributeError):
            mixer.setTrackPan(i, p)

    def set_mute(self, i, on):
        self.mutes[i] = on
        if self.any_solo:
            return   # FL's solo owns the mute switches now: kept for when the solo ends
        if bool(mixer.isTrackMuted(i)) != on:
            mixer.muteTrack(i)   # toggles (works on every FL version)

    def set_solo(self, i, on):
        if on and not self.any_solo:
            self.mutes = dict((k, bool(mixer.isTrackMuted(k))) for k in self.inserts)   # the mutes before the solo
        if bool(mixer.isTrackSolo(i)) != on:
            mixer.soloTrack(i)   # toggles
        self.any_solo = any(mixer.isTrackSolo(k) for k in self.inserts)

    def send(self, frame):
        device.midiOutSysex(bytes(frame))


fl = FLStudio()
bridge = core.BridgeCore(fl, 'FL Studio')


def OnInit():
    fl.rescan()


def OnSysEx(event):
    if bridge.on_sysex(event.sysex):
        event.handled = True


def OnIdle():
    fl.tick()
    if device.isAssigned():
        bridge.idle()


def OnRefresh(flags):
    if flags & midi.HW_Dirty_Mixer_Display:   # names, colours (fader values are read on every tick anyway)
        fl.soon()
