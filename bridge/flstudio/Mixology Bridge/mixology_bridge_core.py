# Mixology Bridge core: the DAW side of Mixology's bridge, for DAWs with Python controller scripts (FL Studio,
# Ableton Live). It is the Cubase MIDI Remote script (bridge/cubase/Mixology/Bridge/Mixology_Bridge.js) in Python:
# the same SysEx protocol, the same bank of 12 strips, the same answers, so the Mixology plug-in can't tell the
# DAWs apart. Each DAW's script is this core plus a small adapter that reads and moves its mixer.
#
# Messages are SysEx frames  F0 7D 4D 58 <protocol> <command> <payload> F7 . The layout MUST match
# dsp/include/mixology/BridgeProtocol.h. Plain Python 3 (FL Studio and Live ship their own interpreter, without
# most of the standard library): no imports.

SCRIPT_VERSION = 4          # what Mixology expects of a bridge script (same behaviour as the Cubase script v4)
PROTOCOL_VERSION = 1
BANK_SIZE = 12
MAX_TEXT_BYTES = 63
VALUE_MAX = 2097151         # 21 bits

CMD_HELLO = 0x01
CMD_REQUEST_DUMP = 0x02
CMD_SET_VOLUME = 0x10
CMD_SET_PAN = 0x11
CMD_SET_MUTE = 0x12
CMD_SET_SOLO = 0x13
CMD_BANK = 0x20
CMD_PING = 0x30
CMD_HELLO_ACK = 0x41
CMD_SLOT_TITLE = 0x42
CMD_SLOT_VOLUME = 0x43
CMD_SLOT_PAN = 0x44
CMD_SLOT_MUTE = 0x45
CMD_SLOT_SOLO = 0x46
CMD_SLOT_COLOUR = 0x47
CMD_SLOT_AUTOMATION = 0x48
CMD_ACK = 0x50
CMD_LOG = 0x51
CMD_PONG = 0x52

ACK_APPLIED = 0
ACK_MISMATCH = 1
ACK_EMPTY_SLOT = 2
ACK_BAD_MESSAGE = 3
ACK_AUTOMATION_WRITE = 4

FIELD_ORDER = ('flag', 'slot', 'token', 'hash', 'id', 'value', 'rgb', 'text')
FIELD_GROUPS = {'flag': 1, 'slot': 1, 'token': 2, 'hash': 4, 'id': 4, 'value': 3, 'rgb': 4}
LAYOUT = {
    CMD_HELLO: ('token',),
    CMD_REQUEST_DUMP: (),
    CMD_SET_VOLUME: ('slot', 'token', 'hash', 'id', 'value'),
    CMD_SET_PAN: ('slot', 'token', 'hash', 'id', 'value'),
    CMD_SET_MUTE: ('slot', 'token', 'hash', 'id', 'flag'),
    CMD_SET_SOLO: ('slot', 'token', 'hash', 'id', 'flag'),
    CMD_BANK: ('flag',),
    CMD_PING: ('token',),
    CMD_HELLO_ACK: ('flag', 'slot', 'token'),
    CMD_SLOT_TITLE: ('slot', 'hash', 'id', 'text'),
    CMD_SLOT_VOLUME: ('slot', 'value', 'text'),
    CMD_SLOT_PAN: ('slot', 'value', 'text'),
    CMD_SLOT_MUTE: ('slot', 'flag'),
    CMD_SLOT_SOLO: ('slot', 'flag'),
    CMD_SLOT_COLOUR: ('slot', 'rgb', 'flag'),
    CMD_SLOT_AUTOMATION: ('slot', 'flag'),
    CMD_ACK: ('token', 'flag'),
    CMD_LOG: ('text',),
    CMD_PONG: ('token',),
}

BANK_RESET, BANK_PREVIOUS, BANK_NEXT, BANK_SHIFT_LEFT, BANK_SHIFT_RIGHT = 0, 1, 2, 3, 4


def utf8_bytes(text):
    return list((text or '').encode('utf-8'))


def truncate_utf8(data, limit):
    if len(data) <= limit:
        return data
    cut = limit
    while cut > 0 and (data[cut] & 0xC0) == 0x80:
        cut -= 1
    return data[:cut]


def name_hash(text):
    """FNV-1a over the UTF-8 bytes, low 28 bits (the same as the plug-in and the Cubase script)."""
    h = 2166136261
    for b in utf8_bytes(text):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h & 0x0FFFFFFF


def _put7(out, value, groups):
    value = int(value)
    for g in range(groups - 1, -1, -1):
        out.append((value >> (7 * g)) & 0x7F)


def encode(cmd, msg=None):
    msg = msg or {}
    out = [0xF0, 0x7D, 0x4D, 0x58, PROTOCOL_VERSION, cmd]
    fields = LAYOUT[cmd]
    for name in FIELD_ORDER:
        if name not in fields:
            continue
        if name == 'value':
            v = msg.get('value', 0.0)
            v = 0.0 if not (v >= 0.0) else min(float(v), 1.0)
            _put7(out, int(round(v * VALUE_MAX)), 3)
        elif name == 'text':
            b = truncate_utf8(utf8_bytes(msg.get('text', '')), MAX_TEXT_BYTES)
            out.append(len(b))
            for c in b:
                out.append(c >> 4)
                out.append(c & 0x0F)
        else:
            _put7(out, msg.get(name, 0) or 0, FIELD_GROUPS[name])
    out.append(0xF7)
    return out


def decode(data):
    data = list(data or [])
    if len(data) < 7 or data[0] != 0xF0 or data[-1] != 0xF7:
        return None
    if data[1] != 0x7D or data[2] != 0x4D or data[3] != 0x58 or data[4] != PROTOCOL_VERSION:
        return None
    cmd = data[5]
    fields = LAYOUT.get(cmd)
    if fields is None:
        return None
    msg = {'cmd': cmd, 'flag': 0, 'slot': 0, 'token': 0, 'hash': 0, 'id': 0, 'value': 0.0, 'rgb': 0, 'text': ''}
    pos = [6]
    end = len(data) - 1

    def get7(groups):
        if pos[0] + groups > end:
            raise ValueError('short')
        v = 0
        for _ in range(groups):
            b = data[pos[0]]
            if b > 0x7F:
                raise ValueError('not 7-bit')
            v = v * 128 + b
            pos[0] += 1
        return v

    try:
        for name in FIELD_ORDER:
            if name not in fields:
                continue
            if name == 'value':
                msg['value'] = get7(3) / float(VALUE_MAX)
            elif name == 'text':
                n = get7(1)
                raw = [get7(1) * 16 + get7(1) for _ in range(n)]
                msg['text'] = bytes(raw).decode('utf-8', 'replace')
            else:
                msg[name] = get7(FIELD_GROUPS[name])
    except ValueError:
        return None
    if pos[0] != end:
        return None
    return msg


def db_text(db):
    """A fader level as Cubase writes it ("-6.02", "+2.40", "-oo"): Mixology learns the DAW's fader curve from it."""
    if db is None or db <= -144.0:
        return '-oo'
    return ('+' if db > 0.005 else '') + ('%.2f' % db)


def pan_text(pan):
    """A pan (-1 left .. 1 right) as Cubase writes it: "L30", "C", "R100"."""
    v = int(round(pan * 100.0))
    return 'C' if v == 0 else ('L%d' % -v if v < 0 else 'R%d' % v)


class Strip(object):
    """One of the 12 strips: what was last sent for it."""

    def __init__(self, index):
        self.index = index
        self.channel = None   # the DAW's channel key, or None (an empty strip)
        self.title = None     # None = nothing sent yet
        self.hash = 0
        self.volume = None
        self.pan = None
        self.mute = None
        self.solo = None
        self.colour = None
        self.automation = None


class BridgeCore(object):
    """The bank of 12 strips over the DAW's mixer channels, the messages, and what changed since the last look.

    The adapter (one per DAW) provides:
        channels()             the mixer channels Mixology may move, in mixer order (any hashable keys)
        name(ch)               the channel's name ('' = unnamed: Mixology leaves it alone)
        volume(ch)             (normalised 0..1 as the DAW's fader, dB or None for -inf)
        pan(ch)                -1 .. 1 or None (no panner)
        mute(ch), solo(ch)     bool
        colour(ch)             0xRRGGBB or None
        writing(ch)            True while it writes automation (Mixology then never moves it)
        set_volume(ch, n), set_pan(ch, p), set_mute(ch, on), set_solo(ch, on)
        send(midi_bytes)       one SysEx frame to Mixology (a list of ints, F0 .. F7)
    """

    def __init__(self, adapter, daw_name):
        self.daw = adapter
        self.daw_name = daw_name
        self.offset = 0
        self.strips = [Strip(i) for i in range(BANK_SIZE)]
        self.errors = 0
        self.last_error = ''

    # ---- sending -----------------------------------------------------------------------------------------
    def send(self, cmd, msg=None):
        self.daw.send(encode(cmd, msg))

    def log(self, text):
        text = str(text)
        for i in range(0, len(text), 56):
            self.send(CMD_LOG, {'text': ('  ' if i else '') + text[i:i + 56]})

    def announce(self, token=0):
        self.send(CMD_HELLO_ACK, {'flag': SCRIPT_VERSION, 'slot': BANK_SIZE, 'token': token})

    # ---- the bank -----------------------------------------------------------------------------------------
    def bank_channels(self):
        chans = [c for c in self.daw.channels() if self.daw.name(c)]
        if self.offset > max(0, len(chans) - 1):
            self.offset = max(0, len(chans) - 1)
        return chans, chans[self.offset:self.offset + BANK_SIZE]

    def move_bank(self, action):
        chans, _ = self.bank_channels()
        last = max(0, len(chans) - 1)
        if action == BANK_RESET:
            self.offset = 0
        elif action == BANK_PREVIOUS:
            self.offset = max(0, self.offset - BANK_SIZE)
        elif action == BANK_NEXT:
            if self.offset + BANK_SIZE <= last:
                self.offset += BANK_SIZE
        elif action == BANK_SHIFT_LEFT:
            self.offset = max(0, self.offset - 1)
        elif action == BANK_SHIFT_RIGHT:
            if self.offset < last:
                self.offset += 1

    # ---- what changed: called on every idle tick and after each message -----------------------------------
    def flush(self, everything=False):
        _, view = self.bank_channels()
        for i, s in enumerate(self.strips):
            ch = view[i] if i < len(view) else None
            title = self.daw.name(ch) if ch is not None else ''
            if everything or title != s.title or ch != s.channel:
                s.channel, s.title, s.hash = ch, title, (name_hash(title) if title else 0)
                s.volume = s.pan = s.mute = s.solo = s.colour = s.automation = None
                self.send(CMD_SLOT_TITLE, {'slot': i, 'hash': s.hash, 'id': 0, 'text': title})
            if ch is None:
                continue
            n, db = self.daw.volume(ch)
            vol = (round(n, 6), db_text(db))
            if everything or vol != s.volume:
                s.volume = vol
                self.send(CMD_SLOT_VOLUME, {'slot': i, 'value': n, 'text': vol[1]})
            p = self.daw.pan(ch)
            pan = (0.5, '') if p is None else (round((p + 1.0) / 2.0, 6), pan_text(p))
            if everything or pan != s.pan:
                s.pan = pan
                self.send(CMD_SLOT_PAN, {'slot': i, 'value': pan[0], 'text': pan[1]})
            mute = 1 if self.daw.mute(ch) else 0
            if everything or mute != s.mute:
                s.mute = mute
                self.send(CMD_SLOT_MUTE, {'slot': i, 'flag': mute})
            solo = 1 if self.daw.solo(ch) else 0
            if everything or solo != s.solo:
                s.solo = solo
                self.send(CMD_SLOT_SOLO, {'slot': i, 'flag': solo})
            auto = 2 if self.daw.writing(ch) else 0
            if everything or auto != s.automation:
                s.automation = auto
                self.send(CMD_SLOT_AUTOMATION, {'slot': i, 'flag': auto})
            rgb = self.daw.colour(ch)
            if rgb is not None and (everything or rgb != s.colour):
                s.colour = rgb
                self.send(CMD_SLOT_COLOUR, {'slot': i, 'rgb': rgb & 0xFFFFFF, 'flag': 1})

    def idle(self):
        self.guard('idle', lambda: self.flush(False))

    # ---- receiving -----------------------------------------------------------------------------------------
    def target(self, m):
        if m['slot'] < 0 or m['slot'] >= BANK_SIZE:
            self.send(CMD_ACK, {'token': m['token'], 'flag': ACK_EMPTY_SLOT})
            return None
        s = self.strips[m['slot']]
        if s.channel is None or not s.title:
            self.send(CMD_ACK, {'token': m['token'], 'flag': ACK_EMPTY_SLOT})
            return None
        if s.hash != m['hash'] or m['id'] != 0:
            self.log('Refused: slot %d holds "%s" now' % (m['slot'] + 1, s.title))
            self.send(CMD_ACK, {'token': m['token'], 'flag': ACK_MISMATCH})
            return None
        if self.daw.writing(s.channel):
            self.send(CMD_ACK, {'token': m['token'], 'flag': ACK_AUTOMATION_WRITE})
            return None
        return s

    def on_sysex(self, data):
        """A SysEx frame from Mixology. True when it was ours."""
        m = decode(data)
        if m is None:
            return False
        self.guard('message', lambda: self.handle(m))
        return True

    def handle(self, m):
        cmd = m['cmd']
        if cmd == CMD_HELLO:
            self.announce(m['token'])
        elif cmd == CMD_REQUEST_DUMP:
            self.dump()
        elif cmd == CMD_PING:
            self.send(CMD_PONG, {'token': m['token']})
        elif cmd in (CMD_SET_VOLUME, CMD_SET_PAN, CMD_SET_MUTE, CMD_SET_SOLO):
            s = self.target(m)
            if s is not None:
                if cmd == CMD_SET_VOLUME:
                    self.daw.set_volume(s.channel, m['value'])
                elif cmd == CMD_SET_PAN:
                    self.daw.set_pan(s.channel, m['value'] * 2.0 - 1.0)
                elif cmd == CMD_SET_MUTE:
                    self.daw.set_mute(s.channel, bool(m['flag']))
                else:
                    self.daw.set_solo(s.channel, bool(m['flag']))
                self.send(CMD_ACK, {'token': m['token'], 'flag': ACK_APPLIED})
        elif cmd == CMD_BANK:
            self.move_bank(m['flag'])
        self.flush(False)

    def dump(self):
        self.flush(True)
        named = 0
        for s in self.strips:
            if s.title:
                named += 1
                self.log('slot %d "%s"' % (s.index + 1, s.title))
        chans, _ = self.bank_channels()
        self.log('%s bridge v%d: %d named channel(s) in the bank, %d in the mixer' % (self.daw_name, SCRIPT_VERSION, named, len(chans)))
        self.log('errors %d' % self.errors)
        if self.errors:
            self.log('last error: ' + self.last_error)

    def guard(self, where, fn):
        try:
            fn()
        except Exception as e:   # a DAW API surprise must never stop the script
            self.errors += 1
            self.last_error = '%s: %s' % (where, e)
