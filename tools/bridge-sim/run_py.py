#!/usr/bin/env python3
"""End-to-end simulator for the Python bridges: runs the REAL FL Studio and Ableton Live scripts
(bridge/flstudio/..., bridge/ableton/...) against fake DAW APIs and connects them to the REAL C++ BridgeSession
(session_cli), like run.js does for the Cubase script. Every message goes through both encoders and decoders.

Usage: python3 tools/bridge-sim/run_py.py <path to session_cli>

The fakes follow the documented APIs (FL Studio's MIDI scripting stubs; Live's _Framework.ControlSurface and the
Live Object Model). What they can't know is how each DAW really behaves (when FL redraws, Live's exact fader
curve): that is what a test in the real DAW checks."""

import importlib
import math
import os
import subprocess
import sys
import types

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FL_DIR = os.path.join(ROOT, 'bridge', 'flstudio', 'Mixology Bridge')
LIVE_DIR = os.path.join(ROOT, 'bridge', 'ableton')


class Session(object):
    def __init__(self, exe):
        # session_cli speaks UTF-8 (track names like "Vocal été ✓"); Windows would read it as its code page
        self.proc = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines=True, bufsize=1,
                                     encoding='utf-8')

    def push(self, line):
        self.proc.stdin.write(line + '\n')

    def request(self, line):
        self.push(line)
        self.proc.stdin.flush()
        lines = []
        while True:
            l = self.proc.stdout.readline()
            if not l:
                raise RuntimeError('session_cli ended')
            l = l.rstrip('\n')
            if l == 'end':
                return lines
            lines.append(l)

    def close(self):
        try:
            self.push('quit')
            self.proc.stdin.close()
        except Exception:
            pass
        self.proc.wait()


# ---------------------------------------------------------------------------------------------------------
# Fake FL Studio: the modules a device script imports

def fl_db(v):
    g = (pow(11.0, 1.25 * v) - 1.0) / 10.0
    return -math.inf if g <= 1e-9 else 20.0 * math.log10(g)


class FakeFL(object):
    """Mixer: 0 = Master, 1..125 inserts, 126 = "Current"."""
    LAST = 125

    def __init__(self, names):
        self.tracks = [{'name': 'Master', 'vol': 0.8, 'pan': 0.0, 'mute': False, 'solo': False, 'fx': 0, 'color': 0x5F6B78}]
        for i in range(1, self.LAST + 2):
            self.tracks.append({'name': 'Insert %d' % i, 'vol': 0.8, 'pan': 0.0, 'mute': False, 'solo': False, 'fx': 0, 'color': 0x5F6B78})
        for i, n in enumerate(names):
            self.tracks[i + 1]['name'] = n
        self.sent = []
        self.set_calls = 0

    def db(self, i):
        return fl_db(self.tracks[i]['vol'])

    def install(self):
        fl = self
        mixer = types.ModuleType('mixer')
        mixer.trackCount = lambda: len(fl.tracks)
        mixer.getTrackInfo = lambda kind: {0: 0, 1: 1, 2: fl.LAST}[kind]
        mixer.getTrackName = lambda i: fl.tracks[i]['name']

        def getTrackVolume(i, mode=0):
            return fl.db(i) if mode == 1 else fl.tracks[i]['vol']
        mixer.getTrackVolume = getTrackVolume

        def setTrackVolume(i, v, pickup=0):
            fl.set_calls += 1
            fl.tracks[i]['vol'] = round(min(max(v, 0.0), 1.0), 9)
        mixer.setTrackVolume = setTrackVolume
        mixer.getTrackPan = lambda i: fl.tracks[i]['pan']

        def setTrackPan(i, p, pickup=0):
            fl.set_calls += 1
            fl.tracks[i]['pan'] = min(max(p, -1.0), 1.0)
        mixer.setTrackPan = setTrackPan
        mixer.isTrackMuted = lambda i: fl.tracks[i]['mute']
        mixer.isTrackSolo = lambda i: fl.tracks[i]['solo']

        def muteTrack(i, value=-1):
            fl.tracks[i]['mute'] = (not fl.tracks[i]['mute']) if value == -1 else bool(value)
        mixer.muteTrack = muteTrack

        def soloTrack(i, value=-1, mode=-1):
            # FL solos an insert by switching the other inserts' mutes on (and back when the solo ends).
            on = (not fl.tracks[i]['solo']) if value == -1 else bool(value)
            if on and not any(t['solo'] for t in fl.tracks):
                fl.before_solo = [t['mute'] for t in fl.tracks]
                for k in range(1, fl.LAST + 1):
                    fl.tracks[k]['mute'] = k != i
            for t in fl.tracks:
                t['solo'] = False
            fl.tracks[i]['solo'] = on
            if not on:
                for k, m in enumerate(getattr(fl, 'before_solo', [])):
                    fl.tracks[k]['mute'] = m
        mixer.soloTrack = soloTrack
        mixer.getTrackColor = lambda i: fl.tracks[i]['color'] - 0x1000000   # FL reports colours as signed ints

        device = types.ModuleType('device')
        device.midiOutSysex = lambda data: fl.sent.append(list(data))
        device.isAssigned = lambda: True
        device.getPortNumber = lambda: 200

        midi = types.ModuleType('midi')
        midi.TN_Master, midi.TN_FirstIns, midi.TN_LastIns, midi.TN_Sel = 0, 1, 2, 3
        midi.PIM_None, midi.PIM_AlwaysPickup, midi.PIM_FollowGlobal = 0, 1, 2
        midi.HW_Dirty_Mixer_Sel, midi.HW_Dirty_Mixer_Display, midi.HW_Dirty_Mixer_Controls = 1, 2, 4

        plugins = types.ModuleType('plugins')
        plugins.isValid = lambda i, slot=-1, useGlobalIndex=False: slot < fl.tracks[i]['fx']

        for m in (mixer, device, midi, plugins):
            sys.modules[m.__name__] = m

    def load(self):
        self.install()
        for m in ('mixology_bridge_core', 'device_Mixology_Bridge'):
            sys.modules.pop(m, None)
        sys.path.insert(0, FL_DIR)
        try:
            self.script = importlib.import_module('device_Mixology_Bridge')
        finally:
            sys.path.remove(FL_DIR)
        self.script.OnInit()

    def idle(self):
        self.script.OnIdle()

    def deliver(self, frame):
        class Event(object):
            pass
        e = Event()
        e.sysex = bytes(frame)
        e.handled = False
        self.script.OnSysEx(e)

    # scenario helpers
    def index(self, name):
        return next(i for i, t in enumerate(self.tracks) if t['name'] == name)

    def volume_of(self, name):
        return self.tracks[self.index(name)]['vol']

    def add(self, name):
        i = next(i for i in range(1, self.LAST + 1) if self.tracks[i]['name'].startswith('Insert ') and not self.tracks[i]['fx'])
        self.tracks[i]['name'] = name
        return i


# ---------------------------------------------------------------------------------------------------------
# Fake Ableton Live: _Framework.ControlSurface and the Live Object Model's tracks

def live_db(v):
    return -math.inf if v <= 0.0 else 6.0 + 85.0 * math.log10(v)   # a stand-in curve: 0.85 = 0 dB, 1.0 = +6 dB


class Param(object):
    def __init__(self, value, lo, hi, kind):
        self.value, self.min, self.max, self.kind = value, lo, hi, kind
        self.sets = 0

    def str_for_value(self, v):
        if self.kind == 'volume':
            db = live_db(v)
            return '-inf dB' if db == -math.inf else '%.1f dB' % db
        x = int(round(v * 50))
        return 'C' if x == 0 else ('%dL' % -x if x < 0 else '%dR' % x)

    def __setattr__(self, k, v):
        if k == 'value' and hasattr(self, 'kind'):
            self.__dict__['sets'] += 1
            v = min(max(v, self.min), self.max)
        object.__setattr__(self, k, v)


class Track(object):
    def __init__(self, name):
        self.name = name
        self.mute = False
        self.solo = False
        self.color = 0x3366CC
        self.mixer_device = types.SimpleNamespace(volume=Param(0.85, 0.0, 1.0, 'volume'), panning=Param(0.0, -1.0, 1.0, 'pan'))


class FakeLive(object):
    def __init__(self, names):
        self.song = types.SimpleNamespace(tracks=[Track(n) for n in names], return_tracks=[Track('A-Reverb'), Track('B-Delay')])
        self.sent = []
        self.set_calls = 0

    def all(self):
        return list(self.song.tracks) + list(self.song.return_tracks)

    def db(self, t):
        return live_db(t.mixer_device.volume.value)

    def install(self):
        live = self
        fw = types.ModuleType('_Framework')
        cs = types.ModuleType('_Framework.ControlSurface')

        class ControlSurface(object):
            def __init__(self, c_instance):
                self._c_instance = c_instance

            def song(self):
                return live.song

            def _send_midi(self, midi_bytes, optimized=None):
                assert isinstance(midi_bytes, tuple)
                live.sent.append(list(midi_bytes))
                return True

            def update_display(self):
                pass

            def handle_sysex(self, midi_bytes):
                pass

            def disconnect(self):
                pass

            def receive_midi(self, midi_bytes):
                if len(midi_bytes) != 3:
                    self.handle_sysex(midi_bytes)

        cs.ControlSurface = ControlSurface
        fw.ControlSurface = cs
        sys.modules['_Framework'] = fw
        sys.modules['_Framework.ControlSurface'] = cs

    def load(self):
        self.install()
        for m in list(sys.modules):
            if m == 'MixologyBridge' or m.startswith('MixologyBridge.'):
                sys.modules.pop(m)
        sys.path.insert(0, LIVE_DIR)
        try:
            pkg = importlib.import_module('MixologyBridge')
        finally:
            sys.path.remove(LIVE_DIR)
        self.surface = pkg.create_instance(object())

    def idle(self):
        self.surface.update_display()

    def deliver(self, frame):
        self.surface.receive_midi(tuple(frame))

    def set_count(self):
        return sum(t.mixer_device.volume.sets + t.mixer_device.panning.sets for t in self.all())


# ---------------------------------------------------------------------------------------------------------
# The rig and the scenarios

class Rig(object):
    def __init__(self, exe, daw, idle_every):
        self.daw = daw
        self.daw.load()
        self.session = Session(exe)
        self.now = 0.0
        self.state = None
        self.idle_every = idle_every
        self.steps = 0
        self.to_script = []

    def parse(self, lines):
        import json
        result = None
        for l in lines:
            if l.startswith('out '):
                self.to_script.append(list(bytes.fromhex(l[4:])))
            elif l.startswith('state '):
                self.state = json.loads(l[6:])
            elif l.startswith('result '):
                result = {'ok': l.startswith('result ok'), 'message': l[len('result ok '):] if l.startswith('result ok') else l[len('result fail '):]}
        return result

    def step(self):
        if self.steps % self.idle_every == 0:
            self.daw.idle()
        self.steps += 1
        for f in self.daw.sent:
            self.session.push('in %s %.3f' % (bytes(f).hex(), self.now))
        self.daw.sent = []
        self.parse(self.session.request('tick %.3f' % self.now))
        frames, self.to_script = self.to_script, []
        for f in frames:
            self.daw.deliver(f)
        self.now += 0.01

    def run(self, seconds):
        end = self.now + seconds
        while self.now < end:
            self.step()

    def ref(self, slot):
        s = self.state['slots'][slot]
        return (slot, s['hash'])

    def cmd(self, name, slot=-1, arg=0.0):
        return self.cmd_ref(name, self.ref(slot) if slot >= 0 else (-1, 0), arg)

    def cmd_ref(self, name, ref, arg=0.0):
        return self.parse(self.session.request('cmd %s %d %d %s %.3f' % (name, ref[0], ref[1], repr(float(arg)), self.now)))

    def plan(self, spec):
        return self.parse(self.session.request('plan %.3f %s' % (self.now, spec)))

    def undo(self):
        return self.parse(self.session.request('undo %.3f' % self.now))

    def slot_of(self, title):
        return next((i for i, s in enumerate(self.state['slots']) if s['title'] == title), -1)


checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print('  FAIL: ' + what)


def near(a, b, tol):
    return abs(a - b) <= tol


NAMES = ['Kick', 'Snare', 'Hats', 'Perc', '808', 'Keys', 'Synth', 'Guitar', 'Lead Vox', 'FX', 'Adlibs', 'Vocal été ✓']


def common_scenarios(daw_db, vol_of, names_in_order, tol):
    """The same checks for both DAWs. daw_db(name) = that channel's dB in the DAW; vol_of(name) its fader; tol = how
    close a move lands (Live shows its faders to 0.1 dB, so Mixology can't do better than about 0.05 there)."""

    def connects(r):
        r.run(0.5)
        check(r.state['link'] == 'connected', 'link connected, got ' + r.state['link'])
        titles = [s['title'] for s in r.state['slots']]
        check(titles == names_in_order(r)[:12], 'bank mirrored: %s' % titles)
        check(r.state['slots'][0]['volumeText'] in ('0.00', '-0.00', '+0.00'), 'volume text at unity: ' + r.state['slots'][0]['volumeText'])
        check(r.state['slots'][0]['panText'] == 'C', 'pan text ' + r.state['slots'][0]['panText'])
        check(not any('Fader moved' in l for l in r.state['log']), 'connecting is not logged as fader moves')

    def exact_db(r):
        r.run(0.5)
        for slot in (0, 5, 11):
            name = r.state['slots'][slot]['title']
            for target in (0.0, -6.0, -12.0, 3.0, -0.5, -24.0):
                res = r.cmd('setDb', slot, target)
                check(res['ok'], 'setDb %s on %s: %s' % (target, name, res['message']))
                r.run(1.0)
                check(near(daw_db(r, name), target, tol), '%s at %.3f dB, wanted %s' % (name, daw_db(r, name), target))
            check(r.cmd('minusInf', slot)['ok'], '-inf')
            r.run(0.6)
            check(vol_of(r, name) == 0.0, name + ' at -inf')

    def twelve(r):
        r.run(0.5)
        targets = [-3, -6, -9, -12, -4.5, -10, -8, -11, -2, -14, -7.5, -13]
        for i in range(12):
            check(r.cmd('setDb', i, targets[i])['ok'], 'setDb slot %d' % (i + 1))
        r.run(2.0)
        check(not r.state['moving'], 'every move finished')
        for i in range(12):
            name = r.state['slots'][i]['title']
            check(near(daw_db(r, name), targets[i], tol), '%s at %.3f, wanted %s' % (name, daw_db(r, name), targets[i]))

    def bank(r):
        r.run(0.5)
        snare = r.ref(1)
        check(r.cmd('bank', -1, 4)['ok'], 'shift right')
        r.run(0.5)
        check(r.state['slots'][1]['title'] == 'Hats', 'slot 2 now Hats, got ' + r.state['slots'][1]['title'])
        res = r.cmd_ref('setDb', snare, -20)
        check(not res['ok'] and 'Hats' in res['message'], 'a stale strip is refused: ' + res['message'])
        r.run(0.5)
        check(near(daw_db(r, 'Snare'), 0.0, 0.01), 'Snare did not move')
        check(r.cmd('setDb', 0, -5)['ok'], 'Snare in its new slot')
        r.run(1.0)
        check(near(daw_db(r, 'Snare'), -5.0, tol), 'Snare moved')
        r.cmd('bank', -1, 0)
        r.run(0.5)
        check(r.state['slots'][0]['title'] == 'Kick', 'reset: Kick first, got ' + r.state['slots'][0]['title'])

    def pan_mute_solo(r):
        r.run(0.5)
        check(r.cmd('pan', 4, 0.25)['ok'], 'pan 808')
        r.run(1.0)
        check(r.state['slots'][4]['panText'] == 'L50', '808 pan text ' + r.state['slots'][4]['panText'])
        check(r.cmd('mute', 5, 1)['ok'], 'mute Keys')
        check(r.cmd('solo', 8, 1)['ok'], 'solo Lead Vox')
        r.run(0.5)
        check(r.state['slots'][5]['mute'] and r.state['slots'][8]['solo'], 'mute and solo mirrored')
        others = [s['title'] for i, s in enumerate(r.state['slots'][:9]) if s['mute'] and i not in (5, 8)]
        check(not others, "FL's solo mutes the others, Mixology still sees only Keys muted: %s" % others)
        check(r.cmd('restore', 4)['ok'], 'restore 808')
        r.run(1.0)
        check(r.state['slots'][4]['panText'] == 'C', '808 back to C: ' + r.state['slots'][4]['panText'])

    def scan_and_plan(r):
        r.run(0.5)
        for _ in range(3):
            r.cmd('bank', -1, 4)
        r.run(0.5)
        start = r.state['slots'][0]['title']
        check(r.cmd('scan')['ok'], 'scan starts')
        r.run(4.0)
        check(not r.state['scanning'], 'scan finished')
        total = len(names_in_order(r))
        check(r.state['knownFaders'] == total, 'all %d faders read, got %d' % (total, r.state['knownFaders']))
        check(r.state['slots'][0]['title'] == start, 'back where the user was')
        last = names_in_order(r)[-1]
        res = r.plan('Kick|-2|-;%s|-4|0.25;Perc|-6.5|0.7' % last)
        check(res['ok'], 'plan starts')
        r.run(8.0)
        check(not r.state['applying'], 'plan finished')
        check(near(daw_db(r, 'Kick'), -2, tol), 'Kick (page 1) at -2: %.3f' % daw_db(r, 'Kick'))
        check(near(daw_db(r, last), -4, tol), '%s (last page) at -4: %.3f' % (last, daw_db(r, last)))
        check(near(daw_db(r, 'Perc'), -6.5, tol), 'Perc at -6.5')
        check(r.state['plan']['done'] == 3 and r.state['plan']['skipped'] == 0, 'all three done: %s' % r.state['plan'])
        check(r.undo()['ok'], 'undo starts')
        r.run(8.0)
        for n in ('Kick', last, 'Perc'):
            check(near(daw_db(r, n), 0.0, tol), '%s back to 0 dB: %.3f' % (n, daw_db(r, n)))

    def ping_and_dump(r):
        r.run(0.5)
        r.cmd('refresh')
        r.run(0.5)
        check(any('bridge v4' in l for l in r.state['log']), 'the bridge reports itself: %s' % [l for l in r.state['log'] if 'bridge' in l][-1:])
        check(any('errors 0' in l for l in r.state['log']), 'no script errors')

    return [('connects and mirrors the bank', connects), ('sets exact dB values', exact_db), ('all twelve faders at once', twelve),
            ('a bank change never moves the wrong channel', bank), ('pan, mute, solo and restore', pan_mute_solo),
            ('reads every fader across the pages, a plan everywhere, undo', scan_and_plan), ('diagnostics', ping_and_dump)]


def fl_suite(exe):
    def make():
        fl = FakeFL(NAMES)
        for n in ('Extra %d' % (i + 1) for i in range(18)):
            fl.add(n)
        fl.tracks[40]['fx'] = 1   # an unnamed insert with an effect (Mixology Link): a strip too
        return fl

    def order(r):
        return [t['name'] for i, t in enumerate(r.daw.tracks) if 1 <= i <= FakeFL.LAST and (not t['name'].startswith('Insert ') or t['fx'])]

    db = lambda r, name: r.daw.db(r.daw.index(name))
    vol = lambda r, name: r.daw.volume_of(name)
    scen = common_scenarios(db, vol, order, 0.02)

    def unused(r):
        r.run(0.5)
        r.cmd('scan')
        r.run(4.0)
        check(r.state['knownFaders'] == 31, 'only the inserts in use (30 named + 1 with an effect): %d' % r.state['knownFaders'])
        check(r.daw.tracks[0]['vol'] == 0.8, 'Master untouched')
        i = r.daw.add('New Bass')
        r.run(0.8)
        r.cmd('scan')
        r.run(4.0)
        check(r.state['knownFaders'] == 32, 'a renamed insert shows up: %d' % r.state['knownFaders'])

    def rename(r):
        r.run(0.5)
        ref = r.ref(2)
        r.daw.tracks[3]['name'] = 'Hats Open'
        r.run(0.8)
        check(not r.cmd_ref('setDb', ref, -9)['ok'], 'renamed: the old strip is refused')
        check(r.state['slots'][2]['title'] == 'Hats Open', 'the new name shows: ' + r.state['slots'][2]['title'])

    return 'FL Studio', make, 2, scen + [('only inserts in use are strips', unused), ('a rename is seen', rename)]


def live_suite(exe):
    def make():
        live = FakeLive(NAMES + ['Extra %d' % (i + 1) for i in range(16)])
        return live

    def order(r):
        return [t.name for t in r.daw.all()]

    def track(r, name):
        return next(t for t in r.daw.all() if t.name == name)

    db = lambda r, name: r.daw.db(track(r, name))
    vol = lambda r, name: track(r, name).mixer_device.volume.value
    scen = common_scenarios(db, vol, order, 0.06)

    def returns(r):
        r.run(0.5)
        r.cmd('scan')
        r.run(4.0)
        check(r.state['knownFaders'] == 30, 'tracks and return tracks: %d' % r.state['knownFaders'])

    def reorder(r):
        r.run(0.5)
        hats = r.ref(2)
        r.daw.song.tracks.append(r.daw.song.tracks.pop(0))   # Kick dragged to the end
        r.run(0.5)
        check(not r.cmd_ref('setDb', hats, -6)['ok'], 'old Hats strip refused after reordering')
        at = r.slot_of('Hats')
        check(at == 1, 'Hats now in slot 2: %d' % at)
        check(r.cmd('setDb', at, -6)['ok'], 'Hats at its new slot')
        r.run(1.5)
        check(near(db(r, 'Hats'), -6, 0.06), 'Hats moved: %.3f' % db(r, 'Hats'))

    return 'Ableton Live', make, 10, scen + [('return tracks are strips too', returns), ('a reordered Set: stale strips refused', reorder)]


def core_race_checks():
    """A move that arrives before the script's next look at the mixer (Live looks every 100 ms, FL every 20 ms)
    must never land on the neighbour of a track that was just deleted, or on a track that moved into its strip."""
    sys.path.insert(0, os.path.join(ROOT, 'bridge', 'common'))
    try:
        sys.modules.pop('mixology_bridge_core', None)
        core = importlib.import_module('mixology_bridge_core')
    finally:
        sys.path.pop(0)

    class Mixer(object):   # Live-style: the channels are track indexes
        def __init__(self):
            self.t = [['Bass', 0.85], ['Keys', 0.85], ['Vox', 0.85]]
            self.out = []
        def channels(self): return list(range(len(self.t)))
        def name(self, i): return self.t[i][0] if i < len(self.t) else ''
        def volume(self, i): return self.t[i][1], 0.0
        def pan(self, i): return 0.0
        def mute(self, i): return False
        def solo(self, i): return False
        def colour(self, i): return None
        def writing(self, i): return False
        def set_volume(self, i, v): self.t[i][1] = v
        def set_pan(self, i, p): pass
        def set_mute(self, i, on): pass
        def set_solo(self, i, on): pass
        def send(self, f): self.out.append(f)

    def move(b, d, slot, name):
        d.out = []
        b.on_sysex(core.encode(core.CMD_SET_VOLUME, {'slot': slot, 'token': 7, 'hash': core.name_hash(name), 'id': 0, 'value': 0.2}))
        acks = [core.decode(f) for f in d.out if core.decode(f)['cmd'] == core.CMD_ACK]
        return acks[-1]['flag'] if acks else None

    d = Mixer()
    b = core.BridgeCore(d, 'Live')
    b.flush(True)
    del d.t[0]   # 'Bass' deleted; the next look is still to come
    check(move(b, d, 0, 'Bass') == core.ACK_MISMATCH, 'a move for a deleted track is refused')
    check(d.t[0] == ['Keys', 0.85], "the neighbour 'Keys' is untouched: %s" % d.t[0])

    d = Mixer()
    b = core.BridgeCore(d, 'Live')
    b.flush(True)
    d.t.insert(0, ['New', 0.85])   # a track inserted before: every index moves
    check(move(b, d, 1, 'Keys') == core.ACK_MISMATCH, 'a move for a track that moved out of its strip is refused')
    check(d.t[1] == ['Bass', 0.85], "'Bass', now in that strip, is untouched: %s" % d.t[1])
    check(move(b, d, 2, 'Keys') == core.ACK_APPLIED, "'Keys' in the strip it moved to")
    check(d.t[2][0] == 'Keys' and near(d.t[2][1], 0.2, 1e-5), "'Keys' moved: %s" % d.t[2])


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else None
    if not exe:
        print('usage: run_py.py <session_cli>')
        return 2
    before = failures
    core_race_checks()
    print(('PASS ' if failures == before else 'FAIL ') + 'core: moves arriving before the next look')
    for name, make, idle_every, scenarios in (fl_suite(exe), live_suite(exe)):
        for title, fn in scenarios:
            before = failures
            r = Rig(exe, make(), idle_every)
            try:
                fn(r)
            except Exception as e:
                import traceback
                traceback.print_exc()
                check(False, 'error: %s' % e)
            r.session.close()
            print(('PASS ' if failures == before else 'FAIL ') + name + ': ' + title)
    print('%d checks, %d failed' % (checks, failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
