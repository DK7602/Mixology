// Mixology Bridge: Cubase MIDI Remote script.
// Lets the Mixology plug-in move real MixConsole faders (volume, pan, mute, solo) and see them move.
//
// Install: copy the whole "Mixology" folder into
//   Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\
// so this file ends up at  ...\Local\Mixology\Bridge\Mixology_Bridge.js , then restart Cubase.
// Needs two virtual MIDI ports (loopMIDI): "Mixology to Cubase" and "Cubase to Mixology".
//
// Written for Cubase 12 or later (MIDI Remote API v1.1+; tested target: Cubase Artist 14 = API v1.2).
// Plain ES5: the script engine in Cubase is not a modern browser.
//
// Messages are SysEx frames  F0 7D 4D 58 <protocol> <command> <payload> F7 .
// The layout MUST match dsp/include/mixology/BridgeProtocol.h in the Mixology source.

var SCRIPT_VERSION = 1;
var PROTOCOL_VERSION = 1;
var BANK_SIZE = 12;                              // matches the 12 strips on Mixology's Tracks page
var PORT_FROM_MIXOLOGY = 'Mixology to Cubase';   // Cubase receives on this port
var PORT_TO_MIXOLOGY = 'Cubase to Mixology';     // Cubase sends on this port
var MAX_TEXT_BYTES = 63;
var VALUE_MAX = 2097151;                         // 21 bits

var CMD = {
    hello: 0x01, requestDump: 0x02, setVolume: 0x10, setPan: 0x11, setMute: 0x12, setSolo: 0x13,
    bank: 0x20, ping: 0x30,
    helloAck: 0x41, slotTitle: 0x42, slotVolume: 0x43, slotPan: 0x44, slotMute: 0x45, slotSolo: 0x46,
    slotColour: 0x47, slotAutomation: 0x48, ack: 0x50, log: 0x51, pong: 0x52
};
var ACK = { applied: 0, mismatch: 1, emptySlot: 2, badMessage: 3, automationWrite: 4 };

// Fields per command; they are always packed in FIELD_ORDER.
var FIELD_ORDER = ['flag', 'slot', 'token', 'hash', 'id', 'value', 'rgb', 'text'];
var FIELD_GROUPS = { flag: 1, slot: 1, token: 2, hash: 4, id: 4, value: 3, rgb: 4 };
var LAYOUT = {};
LAYOUT[CMD.hello] = ['token'];
LAYOUT[CMD.requestDump] = [];
LAYOUT[CMD.setVolume] = ['slot', 'token', 'hash', 'id', 'value'];
LAYOUT[CMD.setPan] = ['slot', 'token', 'hash', 'id', 'value'];
LAYOUT[CMD.setMute] = ['slot', 'token', 'hash', 'id', 'flag'];
LAYOUT[CMD.setSolo] = ['slot', 'token', 'hash', 'id', 'flag'];
LAYOUT[CMD.bank] = ['flag'];
LAYOUT[CMD.ping] = ['token'];
LAYOUT[CMD.helloAck] = ['flag', 'slot', 'token'];
LAYOUT[CMD.slotTitle] = ['slot', 'hash', 'id', 'text'];
LAYOUT[CMD.slotVolume] = ['slot', 'value', 'text'];
LAYOUT[CMD.slotPan] = ['slot', 'value', 'text'];
LAYOUT[CMD.slotMute] = ['slot', 'flag'];
LAYOUT[CMD.slotSolo] = ['slot', 'flag'];
LAYOUT[CMD.slotColour] = ['slot', 'rgb', 'flag'];
LAYOUT[CMD.slotAutomation] = ['slot', 'flag'];
LAYOUT[CMD.ack] = ['token', 'flag'];
LAYOUT[CMD.log] = ['text'];
LAYOUT[CMD.pong] = ['token'];

// ------------------------------------------------------------------------------------------------
// Encoding helpers

function utf8Bytes(str) {
    var out = [];
    str = str || '';
    for (var i = 0; i < str.length; i++) {
        var c = str.charCodeAt(i);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < str.length) {
            var d = str.charCodeAt(i + 1);
            if (d >= 0xDC00 && d <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                i++;
            }
        }
        if (c < 0x80) out.push(c);
        else if (c < 0x800) out.push(0xC0 | (c >> 6), 0x80 | (c & 63));
        else if (c < 0x10000) out.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
        else out.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
    return out;
}

function truncateUtf8(bytes, max) {
    if (bytes.length <= max) return bytes;
    var cut = max;
    while (cut > 0 && (bytes[cut] & 0xC0) === 0x80) cut--;
    return bytes.slice(0, cut);
}

// FNV-1a 32-bit, low 28 bits. 16777619 = 2^24 + 403, multiplied without Math.imul (ES5).
function fnv28(bytes) {
    var h = 2166136261;
    for (var i = 0; i < bytes.length; i++) {
        h = (h ^ bytes[i]) >>> 0;
        h = (((h << 24) >>> 0) + h * 403) % 4294967296;
    }
    return (h >>> 0) & 0x0FFFFFFF;
}

function nameHash(str) { return fnv28(utf8Bytes(str)); }

function put7(out, v, groups) {
    for (var g = groups - 1; g >= 0; g--) out.push(Math.floor(v / Math.pow(2, 7 * g)) & 0x7F);
}

function encode(cmd, msg) {
    var out = [0xF0, 0x7D, 0x4D, 0x58, PROTOCOL_VERSION, cmd];
    var fields = LAYOUT[cmd];
    for (var f = 0; f < FIELD_ORDER.length; f++) {
        var name = FIELD_ORDER[f];
        if (fields.indexOf(name) < 0) continue;
        if (name === 'value') {
            var v = msg.value;
            if (!(v >= 0)) v = 0;
            if (v > 1) v = 1;
            put7(out, Math.round(v * VALUE_MAX), 3);
        } else if (name === 'text') {
            var b = truncateUtf8(utf8Bytes(msg.text), MAX_TEXT_BYTES);
            out.push(b.length);
            for (var i = 0; i < b.length; i++) out.push(b[i] >> 4, b[i] & 0x0F);
        } else {
            put7(out, msg[name] || 0, FIELD_GROUPS[name]);
        }
    }
    out.push(0xF7);
    return out;
}

function decode(data) {
    if (!data || data.length < 7) return null;
    var n = data.length;
    if (data[0] !== 0xF0 || data[n - 1] !== 0xF7) return null;
    if (data[1] !== 0x7D || data[2] !== 0x4D || data[3] !== 0x58) return null;
    if (data[4] !== PROTOCOL_VERSION) return null;
    var cmd = data[5];
    var fields = LAYOUT[cmd];
    if (!fields) return null;
    var msg = { cmd: cmd, flag: 0, slot: 0, token: 0, hash: 0, id: 0, value: 0, rgb: 0, text: '' };
    var p = 6, end = n - 1;
    function get7(groups) {
        if (p + groups > end) throw 'short';
        var v = 0;
        for (var g = 0; g < groups; g++) {
            if (data[p] > 0x7F) throw 'not 7-bit';
            v = v * 128 + data[p++];
        }
        return v;
    }
    try {
        for (var f = 0; f < FIELD_ORDER.length; f++) {
            var name = FIELD_ORDER[f];
            if (fields.indexOf(name) < 0) continue;
            if (name === 'value') msg.value = get7(3) / VALUE_MAX;
            else if (name === 'text') {
                var len = get7(1), s = '';
                for (var i = 0; i < len; i++) s += String.fromCharCode(get7(1) * 16 + get7(1));
                msg.text = s;   // only used for logs; raw bytes are fine
            } else msg[name] = get7(FIELD_GROUPS[name]);
        }
    } catch (e) {
        return null;
    }
    if (p !== end) return null;
    return msg;
}

// ------------------------------------------------------------------------------------------------
// Device driver and ports

var midiremote_api = require('midiremote_api_v1');
var driver = midiremote_api.makeDeviceDriver('Mixology', 'Bridge', 'Mixology');
var fromMixology = driver.mPorts.makeMidiInput(PORT_FROM_MIXOLOGY);
var toMixology = driver.mPorts.makeMidiOutput(PORT_TO_MIXOLOGY);

// "Contains" so ports still match when Windows MIDI Services or the driver adds a prefix ("2- ...").
driver.makeDetectionUnit().detectPortPair(fromMixology, toMixology)
    .expectInputNameContains(PORT_FROM_MIXOLOGY)
    .expectOutputNameContains(PORT_TO_MIXOLOGY);

var surface = driver.mSurface;
var page = driver.mMapping.makePage('Mixology');

// One bank of channels as you see them in the MixConsole (hidden channels skipped).
// Inputs and outputs (including Stereo Out) are excluded: Mixology never touches the main bus here.
var zone = page.mHostAccess.mMixConsole.makeMixerBankZone('Mixology Bank')
    .excludeInputChannels()
    .excludeOutputChannels()
    .setFollowVisibility(true);

function send(device, cmd, msg) {
    if (!device) return;
    toMixology.sendMidi(device, encode(cmd, msg || {}));
}

function log(device, text) {
    send(device, CMD.log, { text: String(text) });
}

// ------------------------------------------------------------------------------------------------
// Slots: one mixer bank channel each, with value variables bound to the host's controls.

var slots = [];

function channelId(channel, mapping) {
    // Prefer Cubase's unique ID string; fall back to the runtime ID; 0 when the host gives neither.
    var raw = '';
    try {
        if (mapping && typeof channel.getUniqueIDString === 'function') raw = String(channel.getUniqueIDString(mapping) || '');
        if (!raw && mapping && typeof channel.getRuntimeID === 'function') {
            var rid = channel.getRuntimeID(mapping);
            if (rid !== undefined && rid !== null && rid !== -1) raw = 'rid:' + rid;
        }
    } catch (e) {
        raw = '';
    }
    return { raw: raw, hash: raw ? (fnv28(utf8Bytes(raw)) || 1) : 0 };
}

function makeSlot(index) {
    var channel = zone.makeMixerBankChannel();
    var s = {
        index: index, channel: channel,
        title: '', hash: 0, id: 0, idRaw: '', titleDirty: true,
        volume: 0, volumeText: '', volumeDirty: false,
        pan: 0.5, panText: '', panDirty: false,
        mute: 0, solo: 0, muteDirty: false, soloDirty: false,
        read: 0, write: 0, automationDirty: false,
        rgb: 0, hasColour: 0, colourDirty: false
    };

    s.volumeVar = surface.makeCustomValueVariable('mixologyVolume' + index);
    s.panVar = surface.makeCustomValueVariable('mixologyPan' + index);
    s.muteVar = surface.makeCustomValueVariable('mixologyMute' + index);
    s.soloVar = surface.makeCustomValueVariable('mixologySolo' + index);
    page.makeValueBinding(s.volumeVar, channel.mValue.mVolume).setValueTakeOverModeJump();
    page.makeValueBinding(s.panVar, channel.mValue.mPan).setValueTakeOverModeJump();
    page.makeValueBinding(s.muteVar, channel.mValue.mMute);
    page.makeValueBinding(s.soloVar, channel.mValue.mSolo);

    channel.mOnTitleChange = function (device, mapping, title) {
        title = title || '';
        var id = channelId(channel, mapping);
        var hash = title ? nameHash(title) : 0;
        if (title === s.title && hash === s.hash && id.hash === s.id) return;   // Cubase repeats titles a lot
        s.title = title;
        s.hash = hash;
        s.id = title ? id.hash : 0;
        s.idRaw = id.raw;
        s.titleDirty = s.volumeDirty = s.panDirty = s.muteDirty = s.soloDirty = s.automationDirty = s.colourDirty = true;
    };
    channel.mOnColorChange = function (device, mapping, r, g, b, a, isActive) {
        var rgb = (Math.round(r * 255) << 16) | (Math.round(g * 255) << 8) | Math.round(b * 255);
        if (rgb === s.rgb && s.hasColour) return;
        s.rgb = rgb;
        s.hasColour = 1;
        s.colourDirty = true;
    };

    // Value and display text arrive as separate callbacks; both are sent together from flush(), so
    // Mixology always gets a matching (position, "-6.02") pair.
    channel.mValue.mVolume.mOnProcessValueChange = function (device, mapping, value) {
        s.volume = value;
        s.volumeDirty = true;
    };
    channel.mValue.mVolume.mOnDisplayValueChange = function (device, mapping, value, units) {
        s.volumeText = String(value);
        s.volumeDirty = true;
    };
    channel.mValue.mPan.mOnProcessValueChange = function (device, mapping, value) {
        s.pan = value;
        s.panDirty = true;
    };
    channel.mValue.mPan.mOnDisplayValueChange = function (device, mapping, value, units) {
        s.panText = String(value);
        s.panDirty = true;
    };
    channel.mValue.mMute.mOnProcessValueChange = function (device, mapping, value) {
        s.mute = value > 0.5 ? 1 : 0;
        s.muteDirty = true;
    };
    channel.mValue.mSolo.mOnProcessValueChange = function (device, mapping, value) {
        s.solo = value > 0.5 ? 1 : 0;
        s.soloDirty = true;
    };
    channel.mValue.mAutomationRead.mOnProcessValueChange = function (device, mapping, value) {
        s.read = value > 0.5 ? 1 : 0;
        s.automationDirty = true;
    };
    channel.mValue.mAutomationWrite.mOnProcessValueChange = function (device, mapping, value) {
        s.write = value > 0.5 ? 1 : 0;
        s.automationDirty = true;
    };
    return s;
}

for (var i = 0; i < BANK_SIZE; i++) slots.push(makeSlot(i));

// Bank navigation: one value variable per action; Mixology "presses" it.
var bankVars = [];
var bankActions = [zone.mAction.mResetBank, zone.mAction.mPrevBank, zone.mAction.mNextBank,
                   zone.mAction.mShiftLeft, zone.mAction.mShiftRight];
for (var b = 0; b < bankActions.length; b++) {
    var v = surface.makeCustomValueVariable('mixologyBank' + b);
    page.makeActionBinding(v, bankActions[b]);
    bankVars.push(v);
}

// ------------------------------------------------------------------------------------------------
// Sending state

function flushSlot(device, s, all) {
    if (all || s.titleDirty) {
        send(device, CMD.slotTitle, { slot: s.index, hash: s.hash, id: s.id, text: s.title });
        s.titleDirty = false;
    }
    if (!s.title) {
        s.volumeDirty = s.panDirty = s.muteDirty = s.soloDirty = s.automationDirty = s.colourDirty = false;
        return;
    }
    if (all || s.volumeDirty) {
        send(device, CMD.slotVolume, { slot: s.index, value: s.volume, text: s.volumeText });
        s.volumeDirty = false;
    }
    if (all || s.panDirty) {
        send(device, CMD.slotPan, { slot: s.index, value: s.pan, text: s.panText });
        s.panDirty = false;
    }
    if (all || s.muteDirty) {
        send(device, CMD.slotMute, { slot: s.index, flag: s.mute });
        s.muteDirty = false;
    }
    if (all || s.soloDirty) {
        send(device, CMD.slotSolo, { slot: s.index, flag: s.solo });
        s.soloDirty = false;
    }
    if (all || s.automationDirty) {
        send(device, CMD.slotAutomation, { slot: s.index, flag: s.read | (s.write << 1) });
        s.automationDirty = false;
    }
    if ((all || s.colourDirty) && s.hasColour) {
        send(device, CMD.slotColour, { slot: s.index, rgb: s.rgb, flag: 1 });
        s.colourDirty = false;
    }
}

function flush(device, all) {
    for (var i = 0; i < slots.length; i++) flushSlot(device, slots[i], all);
}

function announce(device, token) {
    send(device, CMD.helloAck, { flag: SCRIPT_VERSION, slot: BANK_SIZE, token: token || 0 });
}

function dump(device) {
    flush(device, true);
    for (var i = 0; i < slots.length; i++) {
        var s = slots[i];
        if (s.title) log(device, 'slot ' + (i + 1) + ' "' + s.title + '" id ' + (s.idRaw || 'none'));
    }
}

page.mOnActivate = function (device, mapping) {
    announce(device, 0);
};

// API 1.1+ (Cubase 13 and later). On Cubase 12 this is never called and updates go out with each
// message from Mixology instead (it pings every second).
driver.mOnIdle = function (device) {
    flush(device, false);
};

// ------------------------------------------------------------------------------------------------
// Receiving commands from Mixology

function ack(device, token, status) {
    send(device, CMD.ack, { token: token, flag: status });
}

function checkTarget(device, m) {
    if (m.slot < 0 || m.slot >= slots.length) { ack(device, m.token, ACK.emptySlot); return null; }
    var s = slots[m.slot];
    if (!s.title) { ack(device, m.token, ACK.emptySlot); return null; }
    if (s.hash !== m.hash || s.id !== m.id) {
        log(device, 'Refused: slot ' + (m.slot + 1) + ' holds "' + s.title + '" now');
        ack(device, m.token, ACK.mismatch);
        return null;
    }
    if (s.write) { ack(device, m.token, ACK.automationWrite); return null; }
    return s;
}

fromMixology.mOnSysex = function (device, data) {
    var m = decode(data);
    if (!m) return;   // not ours (or another protocol version): ignore

    var s;
    switch (m.cmd) {
        case CMD.hello:
            announce(device, m.token);
            break;
        case CMD.requestDump:
            dump(device);
            break;
        case CMD.ping:
            send(device, CMD.pong, { token: m.token });
            break;
        case CMD.setVolume:
            if ((s = checkTarget(device, m))) { s.volumeVar.setProcessValue(device, m.value); ack(device, m.token, ACK.applied); }
            break;
        case CMD.setPan:
            if ((s = checkTarget(device, m))) { s.panVar.setProcessValue(device, m.value); ack(device, m.token, ACK.applied); }
            break;
        case CMD.setMute:
            if ((s = checkTarget(device, m))) { s.muteVar.setProcessValue(device, m.flag ? 1 : 0); ack(device, m.token, ACK.applied); }
            break;
        case CMD.setSolo:
            if ((s = checkTarget(device, m))) { s.soloVar.setProcessValue(device, m.flag ? 1 : 0); ack(device, m.token, ACK.applied); }
            break;
        case CMD.bank:
            if (m.flag >= 0 && m.flag < bankVars.length) {
                bankVars[m.flag].setProcessValue(device, 1);
                bankVars[m.flag].setProcessValue(device, 0);
            }
            break;
        default:
            break;
    }
    flush(device, false);
};
