// End-to-end bridge simulator: runs the REAL Cubase script (bridge/cubase/.../Mixology_Bridge.js)
// against a fake MixConsole that implements the parts of Steinberg's MIDI Remote API it uses, and
// connects it to the REAL C++ BridgeSession (session_cli). Every message goes through both encoders
// and decoders, so the two sides can't drift apart unnoticed.
//
// Usage: node tools/bridge-sim/run.js <path to session_cli>
// The fake API follows the official type definitions (midiremote_api_v1.d.ts, API v1.2). Behaviour
// Steinberg doesn't document (callback order, action bindings) is modelled on community reports and
// is exactly what the Milestone 1 test in Cubase checks for real.

'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { spawn } = require('child_process');
const readline = require('readline');

const SCRIPT = path.join(__dirname, '../../bridge/cubase/Mixology/Bridge/Mixology_Bridge.js');

// ---------------------------------------------------------------------------------------------
// Fake Cubase

function lawDb(n) {
  if (n <= 0) return -1000;
  return 6 + 20 * Math.log10(Math.pow(n, 2.7) * (1 + 0.15 * (1 - n)));
}
function volumeText(n) {
  const db = lawDb(n);
  return db < -96 ? '-oo' : db.toFixed(2);
}
function panText(p) {
  const v = Math.round((p - 0.5) * 200);
  return v === 0 ? 'C' : (v < 0 ? 'L' : 'R') + Math.abs(v);
}

function makeCubase(names) {
  const sim = {
    channels: names.map((name, i) => ({
      name, volume: 0.7766, pan: 0.5, mute: 0, solo: 0, read: 0, write: 0, visible: true,
      uid: 'chan-' + (1000 + i), rgb: [0.2, 0.4, 0.8],
    })),
    bankOffset: 0,
    bankChannels: [],
    sent: [],
    setCalls: 0,
    idCalls: 0,
    device: { name: 'activeDevice' },
    mapping: { name: 'activeMapping' },
  };

  // routes: which callbacks Cubase fires. 'channel' = only the channel / host values, 'control' = only
  // the bound controls (custom value variables), 'both'. Real Cubase Artist 14 is still being pinned down.
  sim.routes = 'both';
  sim.followVisibility = false;
  sim.visible = () => sim.channels.filter((c) => c.visible || !sim.followVisibility);
  sim.channelAt = (slot) => sim.visible()[sim.bankOffset + slot] || null;
  const viaChannel = () => sim.routes !== 'control';
  const viaControl = () => sim.routes !== 'channel';

  function fireSlot(k) {
    const bc = sim.bankChannels[k];
    if (!bc) return;
    const ch = sim.channelAt(k);
    const values = () => { if (ch) for (const key of Object.keys(bc.mValue)) bc.mValue[key]._fire(); };
    if (sim.reversedOrder) values();   // Cubase's callback order isn't documented: test both
    if (viaChannel() && bc.mOnTitleChange) bc.mOnTitleChange(sim.device, sim.mapping, ch ? ch.name : '');
    if (viaControl())
      for (const key of Object.keys(bc.mValue))
        for (const cv of bc.mValue[key]._bound) if (cv.mOnTitleChange) cv.mOnTitleChange(sim.device, ch ? ch.name : '', key.slice(1));
    if (!ch) return;
    if (viaChannel() && bc.mOnColorChange) bc.mOnColorChange(sim.device, sim.mapping, ch.rgb[0], ch.rgb[1], ch.rgb[2], 1, true);
    if (viaControl())
      for (const cv of bc.mValue.mVolume._bound) if (cv.mOnColorChange) cv.mOnColorChange(sim.device, ch.rgb[0], ch.rgb[1], ch.rgb[2], 1, true);
    if (!sim.reversedOrder) values();
  }
  sim.fireAll = () => sim.bankChannels.forEach((_, k) => fireSlot(k));
  sim.fireChannel = (ch) => {
    const slot = sim.visible().indexOf(ch) - sim.bankOffset;   // (respects followVisibility)
    if (slot >= 0 && slot < sim.bankChannels.length) fireSlot(slot);
  };

  function hostValue(slot, prop) {
    const hv = {
      mOnProcessValueChange: null, mOnDisplayValueChange: null, _bound: [],
      _fire() {
        const ch = sim.channelAt(slot);
        if (!ch) return;
        const v = ch[prop];
        const text = prop === 'volume' ? volumeText(v) : prop === 'pan' ? panText(v) : v ? 'On' : 'Off';
        const units = prop === 'volume' ? 'dB' : '';
        const display = () => {
          if (viaChannel() && hv.mOnDisplayValueChange) hv.mOnDisplayValueChange(sim.device, sim.mapping, text, units);
          if (viaControl()) for (const cv of hv._bound) if (cv.mOnDisplayValueChange) cv.mOnDisplayValueChange(sim.device, text, units);
        };
        const value = () => {
          if (viaChannel() && hv.mOnProcessValueChange) hv.mOnProcessValueChange(sim.device, sim.mapping, v);
          if (viaControl()) for (const cv of hv._bound) if (cv.mOnProcessValueChange) cv.mOnProcessValueChange(sim.device, v, 0);
        };
        if (sim.reversedOrder) display();
        value();
        if (!sim.reversedOrder) display();
      },
      _set(v) {
        const ch = sim.channelAt(slot);
        if (!ch) return;
        if (prop === 'volume') sim.setCalls++;
        ch[prop] = prop === 'volume' || prop === 'pan' ? Math.round(v * 16384) / 16384 : v > 0.5 ? 1 : 0;
        hv._fire();
      },
    };
    return hv;
  }

  function action(fn) { return { _trigger: () => { fn(); sim.fireAll(); } }; }
  const zone = {
    excludeInputChannels() { return zone; }, excludeOutputChannels() { return zone; },
    setFollowVisibility(v) { sim.followVisibility = !!v; return zone; },
    mAction: {
      mResetBank: action(() => { sim.bankOffset = 0; }),
      mPrevBank: action(() => { sim.bankOffset = Math.max(0, sim.bankOffset - sim.bankChannels.length); }),
      mNextBank: action(() => { sim.bankOffset += sim.bankChannels.length; }),
      mShiftLeft: action(() => { sim.bankOffset = Math.max(0, sim.bankOffset - 1); }),
      mShiftRight: action(() => { sim.bankOffset += 1; }),
    },
    makeMixerBankChannel() {
      const k = sim.bankChannels.length;
      const bc = {
        mOnTitleChange: null, mOnColorChange: null,
        mValue: {
          mVolume: hostValue(k, 'volume'), mPan: hostValue(k, 'pan'), mMute: hostValue(k, 'mute'),
          mSolo: hostValue(k, 'solo'), mAutomationRead: hostValue(k, 'read'), mAutomationWrite: hostValue(k, 'write'),
        },
        getUniqueIDString(mapping) { sim.idCalls++; if (mapping !== sim.mapping) throw new Error('bad mapping'); const c = sim.channelAt(k); return c ? c.uid : ''; },
        getRuntimeID() { sim.idCalls++; return k; },
      };
      sim.bankChannels.push(bc);
      return bc;
    },
  };

  const page = {
    mOnActivate: null,
    mHostAccess: { mMixConsole: { makeMixerBankZone() { return zone; } } },
    makeValueBinding(cv, hv) {
      cv._binding = { apply: (v) => hv._set(v) };
      hv._bound.push(cv);
      return { setValueTakeOverModeJump() { return this; } };
    },
    makeActionBinding(cv, act) {
      let last = 0;
      cv._binding = { apply: (v) => { if (v > 0.5 && last <= 0.5) act._trigger(); last = v; } };
      return {};
    },
  };

  const input = { mOnSysex: null };
  const output = { sendMidi(dev, bytes) { if (dev !== sim.device) throw new Error('bad device'); sim.sent.push(bytes.slice()); } };
  const chain = { expectInputNameContains() { return chain; }, expectOutputNameContains() { return chain; } };
  const driver = {
    mOnIdle: null,
    mPorts: { makeMidiInput() { return input; }, makeMidiOutput() { return output; } },
    makeDetectionUnit() { return { detectPortPair() { return chain; } }; },
    mSurface: {
      makeCustomValueVariable(name) {
        return { name, _binding: null, setProcessValue(dev, v) { if (dev !== sim.device) throw new Error('bad device'); if (this._binding) this._binding.apply(v); } };
      },
    },
    mMapping: { makePage() { return page; } },
  };

  const api = { makeDeviceDriver() { return driver; } };
  const code = fs.readFileSync(SCRIPT, 'utf8');
  vm.runInNewContext(code, { require: (n) => { if (n !== 'midiremote_api_v1') throw new Error('unexpected require ' + n); return api; }, console, Math, String });

  sim.activate = () => { if (page.mOnActivate) page.mOnActivate(sim.device, sim.mapping); sim.fireAll(); };
  sim.idle = () => { if (driver.mOnIdle) driver.mOnIdle(sim.device); };
  sim.deliver = (bytes) => { if (input.mOnSysex) input.mOnSysex(sim.device, bytes); };
  sim.db = (ch) => lawDb(ch.volume);
  return sim;
}

// ---------------------------------------------------------------------------------------------
// The C++ session, as a child process

class Session {
  constructor(exe) {
    this.proc = spawn(exe, [], { stdio: ['pipe', 'pipe', 'inherit'] });
    this.lines = [];
    this.waiters = [];
    readline.createInterface({ input: this.proc.stdout }).on('line', (l) => {
      this.lines.push(l);
      if (l === 'end') { const w = this.waiters.shift(); if (w) w(this.lines.splice(0)); }
    });
  }
  request(line) {
    return new Promise((resolve) => { this.waiters.push(resolve); this.proc.stdin.write(line + '\n'); });
  }
  push(line) { this.proc.stdin.write(line + '\n'); }
  close() { this.proc.stdin.write('quit\n'); this.proc.stdin.end(); }
}

const hex = (b) => Buffer.from(b).toString('hex');
const unhex = (s) => Array.from(Buffer.from(s, 'hex'));

class Rig {
  constructor(exe, names) {
    this.cubase = makeCubase(names);
    this.session = new Session(exe);
    this.now = 0;
    this.state = null;
    this.toScript = [];
    this.activated = false;   // Cubase starts the script on the first step, so a scenario can set modes first
  }
  parse(lines) {
    let result = null;
    for (const l of lines) {
      if (l.startsWith('out ')) this.toScript.push(unhex(l.slice(4)));
      else if (l.startsWith('state ')) this.state = JSON.parse(l.slice(6));
      else if (l.startsWith('result ')) result = { ok: l.startsWith('result ok'), message: l.replace(/^result (ok|fail) ?/, '') };
    }
    return result;
  }
  async step() {
    if (!this.activated) { this.activated = true; this.cubase.activate(); }
    this.cubase.idle();
    for (const f of this.cubase.sent.splice(0)) this.session.push('in ' + hex(f) + ' ' + this.now.toFixed(3));
    this.parse(await this.session.request('tick ' + this.now.toFixed(3)));
    for (const f of this.toScript.splice(0)) this.cubase.deliver(f);
    this.now += 0.01;
  }
  async run(seconds) { const end = this.now + seconds; while (this.now < end) await this.step(); }
  // A command on the strip showing `slot` right now (as the UI would send it), or on a remembered ref.
  ref(slot) { const s = this.state.slots[slot]; return { slot, hash: s ? s.hash : 0 }; }
  async cmd(name, slot = -1, arg = 0) { return this.cmdRef(name, slot >= 0 ? this.ref(slot) : { slot: -1, hash: 0 }, arg); }
  async cmdRef(name, ref, arg = 0) {
    return this.parse(await this.session.request(`cmd ${name} ${ref.slot} ${ref.hash} ${arg} ${this.now.toFixed(3)}`));
  }
  async global(name, arg = 0) { return this.cmd(name, -1, arg); }
  slotOf(title) { return this.state.slots.findIndex((s) => s.title === title); }
}

// ---------------------------------------------------------------------------------------------
// Scenarios

const NAMES = ['Kick', 'Snare', 'Hats', 'Perc', '808', 'Keys', 'Synth', 'Guitar', 'Lead Vox', 'FX', 'Adlibs', 'Vocal été ✓'];
let failures = 0, checks = 0;
function check(cond, what) {
  checks++;
  if (!cond) { failures++; console.log('  FAIL: ' + what); }
}
function near(a, b, tol) { return Math.abs(a - b) <= tol; }

const scenarios = {
  async 'connects and mirrors the bank'(r) {
    await r.run(0.3);
    check(r.state.link === 'connected', 'link connected, got ' + r.state.link);
    check(r.state.slots.length === 12, '12 slots');
    check(r.state.slots[0].title === 'Kick' && r.state.slots[7].title === 'Guitar', 'titles mirrored');
    check(r.state.slots[0].volumeText === volumeText(0.7766), 'volume text ' + r.state.slots[0].volumeText);
    check(r.state.slots[0].panText === 'C', 'pan text');
    check(r.state.slots.every((sl) => sl.id === 0), 'script v3+ reads no Cubase channel IDs (crash guard)');
    check(r.state.log.some((l) => l.includes('slot 1 "Kick"')), 'script logs the named slots');
    check(r.cubase.idCalls === 0, 'getUniqueIDString / getRuntimeID never called');
    check(!r.state.log.some((l) => l.includes('Fader moved in Cubase')), 'connecting is not logged as fader moves');
  },

  async 'sets exact dB values on any channel'(r) {
    await r.run(0.3);
    for (const slot of [0, 5, 11]) {
      for (const target of [0, -6, -12, 6, -0.5, -24]) {
        check((await r.cmd('setDb', slot, target)).ok, `setDb ${target} on slot ${slot + 1}`);
        await r.run(0.4);
        const ch = r.cubase.channels[slot];
        check(near(r.cubase.db(ch), target, 0.006), `${ch.name} at ${r.cubase.db(ch).toFixed(3)} dB, wanted ${target}`);
      }
      check((await r.cmd('minusInf', slot)).ok, '-inf');
      await r.run(0.3);
      check(r.cubase.channels[slot].volume === 0, r.cubase.channels[slot].name + ' at -inf');
    }
    check(r.cubase.channels.filter((c, i) => ![0, 5, 11].includes(i)).every((c) => c.volume === 0.7766), 'no other fader moved');
  },

  async 'all twelve faders at once'(r) {
    await r.run(0.3);
    const targets = [-3, -6, -9, -12, -4.5, -10, -8, -11, -2, -14, -7.5, -13];
    for (let i = 0; i < 12; i++) check((await r.cmd('setDb', i, targets[i])).ok, 'setDb slot ' + (i + 1));
    await r.run(0.8);
    check(!r.state.moving, 'every move finished');
    for (let i = 0; i < 12; i++) check(near(r.cubase.db(r.cubase.channels[i]), targets[i], 0.006), `${r.cubase.channels[i].name} at ${r.cubase.db(r.cubase.channels[i]).toFixed(3)}, wanted ${targets[i]}`);
  },

  async 'Cubase fader moves are mirrored without echo, logged once'(r) {
    await r.run(0.3);
    const before = r.cubase.setCalls;
    for (let i = 0; i < 30; i++) { r.cubase.channels[1].volume = 0.4 + i * 0.01; r.cubase.fireChannel(r.cubase.channels[1]); await r.run(0.01); }
    await r.run(0.5);
    for (let i = 0; i < 5; i++) { r.cubase.fireChannel(r.cubase.channels[1]); await r.run(0.4); }   // Cubase repeating itself
    check(r.cubase.setCalls === before, 'no setProcessValue echoed back');
    check(near(r.state.slots[1].volume, 0.69, 1e-5), 'Mixology sees the final position ' + r.state.slots[1].volume);
    check(r.state.slots[1].volumeText === volumeText(0.69), 'and the matching display text');
    check(r.state.log.filter((l) => l.includes("Fader moved in Cubase: 'Snare'")).length === 1, 'logged once');
  },

  async 'dragging a fader in Mixology: exact positions, latest wins, logged once'(r) {
    await r.run(0.3);
    const ref = r.ref(6);   // Synth
    for (let i = 0; i < 20; i++) { check((await r.cmdRef('fader', ref, 0.4 + i * 0.015)).ok, 'drag step'); await r.run(0.01); }
    await r.cmdRef('faderEnd', ref);
    await r.run(0.8);
    check(near(r.cubase.channels[6].volume, 0.685, 1 / 16384), 'Synth at the last dragged position ' + r.cubase.channels[6].volume);
    check(r.state.log.filter((l) => l.includes("Fader dragged in Mixology: 'Synth'")).length === 1, 'drag logged once');
    check(!r.state.log.some((l) => l.includes('Fader moved in Cubase')), 'own readbacks not logged as Cubase moves');
  },

  async 'a bank change never moves the wrong channel'(r) {
    await r.run(0.3);
    const snare = r.ref(1);
    check((await r.global('bank', 4)).ok, 'shift right');
    await r.run(0.3);
    check(r.state.slots[1].title === 'Hats', 'slot 2 now Hats, got ' + r.state.slots[1].title);
    const res = await r.cmdRef('setDb', snare, -20);
    check(!res.ok && res.message.includes('Hats'), 'a stale strip is refused: ' + res.message);
    await r.run(0.2);
    check(r.cubase.channels.every((c) => c.volume === 0.7766), 'nothing moved');
    check((await r.cmd('setDb', 0, -5)).ok, 'Snare in its new slot can be moved');
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[1]), -5, 0.006), 'Snare moved');
    await r.global('bank', 0);   // reset
    await r.run(0.3);
    check(r.state.slots[0].title === 'Kick', 'reset bank starts at Kick, got ' + r.state.slots[0].title);
    await r.global('bank', 2);   // next bank: past the last channel
    await r.run(0.3);
    check(r.state.slots[0].title === '', 'slots past the last channel are empty');
  },

  async 'script refuses a write that races a bank change'(r) {
    await r.run(0.3);
    await r.cmd('setDb', 0, -18);
    r.cubase.bankOffset = 1;   // the user scrolls the mixer before the message lands...
    r.cubase.fireAll();        // ...but after Mixology sent it
    await r.run(0.4);
    check(r.cubase.channels.every((c) => c.volume === 0.7766), 'no channel moved');
    check(r.state.log.some((l) => l.includes('Nothing was changed')), 'refusal reported');
  },

  async 'reordered and renamed channels: stale strips refused'(r) {
    await r.run(0.3);
    const hats = r.ref(2);
    r.cubase.channels.push(r.cubase.channels.shift());   // Kick dragged to the end: every slot shifts
    r.cubase.fireAll();
    await r.run(0.2);
    check(!(await r.cmdRef('setDb', hats, -6)).ok, 'old Hats strip refused after reordering');
    const at = r.slotOf('Hats');
    check(at === 1, 'Hats now in slot 2');
    check((await r.cmd('setDb', at, -6)).ok, 'Hats at its new slot');
    await r.run(0.4);
    const hatsCh = r.cubase.channels.find((c) => c.name === 'Hats');
    check(near(r.cubase.db(hatsCh), -6, 0.006), 'Hats moved');
    const ref = r.ref(at);
    hatsCh.name = 'Hats Open';
    r.cubase.fireChannel(hatsCh);
    await r.run(0.2);
    check(!(await r.cmdRef('setDb', ref, -9)).ok, 'renamed: old strip refused');
  },

  async 'automation: Write blocks at both ends, Read warns'(r) {
    await r.run(0.3);
    r.cubase.channels[0].write = 1;
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.1);
    const res = await r.cmd('setDb', 0, -6);
    check(!res.ok && res.message.includes('automation'), 'Mixology refuses: ' + res.message);
    // Race: W switched on after Mixology sent the move.
    r.cubase.channels[0].write = 0;
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.1);
    check((await r.cmd('setDb', 0, -6)).ok, 'allowed with W off');
    r.cubase.channels[0].write = 1;            // W switched on in Cubase before the move lands
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.4);
    check(r.cubase.channels[0].volume === 0.7766, 'script refused the racing write');
    check(r.state.log.some((l) => l.includes('writing automation')), 'and said why');

    r.cubase.channels[3].read = 1;
    r.cubase.fireChannel(r.cubase.channels[3]);
    await r.run(0.1);
    check(r.state.slots[3].read, 'R reported');
    const rd = await r.cmd('setDb', 3, -4);
    check(rd.ok && rd.message.includes('WARNING') && rd.message.includes('Read'), 'Read: allowed with a warning: ' + rd.message);
  },

  async 'protect (padlock) holds through bank moves'(r) {
    await r.run(0.3);
    check((await r.cmd('protect', 8, 1)).ok, 'protect Lead Vox');
    check(r.state.slots[8].protected, 'shown as protected');
    check(!(await r.cmd('setDb', 8, -6)).ok, 'refused');
    check(!(await r.cmd('mute', 8, 1)).ok, 'mute refused');
    await r.global('bank', 4);
    await r.run(0.3);
    check(r.state.slots[7].title === 'Lead Vox' && r.state.slots[7].protected, 'still protected in its new slot');
    check(!(await r.cmd('fader', 7, 0.2)).ok, 'drag refused');
    await r.run(0.2);
    check(r.cubase.channels[8].volume === 0.7766 && r.cubase.channels[8].mute === 0, 'Lead Vox untouched');
  },

  async 'pan, mute, solo, Restore and Restore all'(r) {
    r.cubase.channels[4].volume = 0.61;
    r.cubase.channels[4].pan = 0.3;
    r.cubase.fireChannel(r.cubase.channels[4]);
    await r.run(0.3);
    await r.cmd('setDb', 4, 3);
    await r.run(0.4);
    await r.cmd('pan', 4, 0.75);
    await r.cmd('mute', 4, 1);
    await r.cmd('solo', 4, 1);
    await r.cmd('setDb', 9, -20);
    await r.cmd('pan', 10, 0);
    await r.run(0.4);
    const c = r.cubase.channels[4];
    check(c.mute === 1 && c.solo === 1 && near(c.pan, 0.75, 1e-4), 'pan / mute / solo applied');
    check(r.state.slots[4].panText === 'R50', 'pan text mirrored: ' + r.state.slots[4].panText);
    check(r.state.changed === 3, 'three channels changed, got ' + r.state.changed);
    check((await r.cmd('restore', 4)).ok, 'restore 808');
    await r.run(0.4);
    check(near(c.volume, 0.61, 1 / 16384), 'volume restored ' + c.volume);
    check(near(c.pan, 0.3, 1 / 16384) && c.mute === 0 && c.solo === 0, 'pan / mute / solo restored');
    const all = await r.global('restoreAll');
    await r.run(0.4);
    check(all.ok && all.message.includes('Restored 2'), 'restore all: ' + all.message);
    check(near(r.cubase.channels[9].volume, 0.7766, 1 / 16384) && near(r.cubase.channels[10].pan, 0.5, 1 / 16384), 'FX and Adlibs back');
    check(r.state.changed === 0, 'nothing left to restore');
  },

  async 'non-English names survive the round trip'(r) {
    await r.run(0.3);
    const i = r.slotOf('Vocal été ✓');
    check(i === 11, 'accented name found at slot 12, got ' + i);
    check((await r.cmd('setDb', i, -3)).ok, 'move it');
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[11]), -3, 0.006), 'moved');
  },

  async 'callback order reversed (display text before value, values before title)'(r) {
    r.cubase.reversedOrder = true;
    await r.run(0.3);
    check(r.state.slots[2].title === 'Hats' && r.state.slots[2].volumeText === volumeText(0.7766), 'mirrored');
    for (const target of [-6, 2, -15]) {
      await r.cmd('setDb', 2, target);
      await r.run(0.4);
      check(near(r.cubase.db(r.cubase.channels[2]), target, 0.006), `Hats at ${r.cubase.db(r.cubase.channels[2]).toFixed(3)}, wanted ${target}`);
    }
    const ref = r.ref(2);
    await r.global('bank', 4);
    await r.run(0.3);
    check(!(await r.cmdRef('setDb', ref, -1)).ok, 'stale strip refused after a bank change');
  },

  async 'Cubase reports only through the bound controls (no channel callbacks)'(r) {
    r.cubase.routes = 'control';
    await r.run(0.3);
    check(r.state.slots[0].title === 'Kick' && r.state.slots[0].volumeText === volumeText(0.7766), 'names and values arrive: ' + r.state.slots[0].title);
    check(r.cubase.idCalls === 0, 'no channel-ID calls');
    await r.cmd('setDb', 0, -6);
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[0]), -6, 0.006), 'moves land');
    const ref = r.ref(0);
    r.cubase.channels[0].name = 'Kick 2';
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.2);
    check(!(await r.cmdRef('setDb', ref, -3)).ok, 'rename: old strip refused');
    await r.global('refresh');
    await r.run(0.2);
    check(r.state.log.some((l) => /script v\d+: 12 named/.test(l)), 'diagnostics: named channel count');
    check(r.state.log.some((l) => l.includes('calls: titles 0/')), 'diagnostics: which route fired: ' + r.state.log.filter((l) => l.includes('calls:')).pop());
    check(r.state.log.some((l) => l.includes('mapping ok, errors 0')), 'diagnostics: no script errors');
  },

  async 'Cubase reports only through the channel callbacks'(r) {
    r.cubase.routes = 'channel';
    await r.run(0.3);
    check(r.state.slots[3].title === 'Perc' && r.state.slots[3].panText === 'C', 'names and values arrive');
    await r.cmd('setDb', 3, 2);
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[3]), 2, 0.006), 'moves land');
  },

  async 'reading every fader pages through the bank and comes back'(r) {
    for (let i = 0; i < 18; i++)   // 30 channels: three pages of 12
      r.cubase.channels.push({ name: 'Extra ' + (i + 1), volume: 0.3 + i * 0.02, pan: 0.5, mute: 0, solo: 0, read: 0, write: 0, visible: true, uid: 'x' + i, rgb: [0.5, 0.5, 0.5] });
    await r.run(0.3);
    await r.global('bank', 4);
    await r.global('bank', 4);
    await r.global('bank', 4);   // the user looks at channels 4 - 15
    await r.run(0.3);
    check(r.state.slots[0].title === 'Perc', 'starts at Perc, got ' + r.state.slots[0].title);
    check((await r.global('scan')).ok, 'scan starts');
    check(!(await r.cmd('setDb', 0, -6)).ok, 'no moves while reading');
    await r.run(3.0);
    check(!r.state.scanning, 'scan finished');
    check(r.state.knownFaders === 30, 'all 30 faders read, got ' + r.state.knownFaders);
    check(r.state.slots[0].title === 'Perc', 'back at Perc, got ' + r.state.slots[0].title);
    check(r.cubase.setCalls === 0, 'nothing was moved');
    check(r.state.log.some((l) => l.includes('Read 30 channel faders')), 'logged');
  },

  async 'a plan reaches channels on every page, comes back, and undoes'(r) {
    for (let i = 0; i < 18; i++)   // 30 channels: three pages of 12
      r.cubase.channels.push({ name: 'Extra ' + (i + 1), volume: 0.3 + i * 0.02, pan: 0.5, mute: 0, solo: 0, read: 0, write: 0, visible: true, uid: 'x' + i, rgb: [0.5, 0.5, 0.5] });
    await r.run(0.3);
    for (let i = 0; i < 3; i++) await r.global('bank', 4);   // the user looks at channels 4 - 15
    await r.run(0.3);
    const before = r.cubase.channels.map((c) => [c.volume, c.pan]);
    const res = r.parse(await r.session.request(`plan ${r.now.toFixed(3)} Kick|-2|-;Extra 17|-4|0.25;Perc|-6.5|0.7`));
    check(res.ok, 'plan starts');
    check(r.state.applying, 'applying');
    check(!(await r.cmd('setDb', 0, -6)).ok, 'no other moves meanwhile');
    await r.run(6.0);
    check(!r.state.applying, 'plan finished');
    const byName = (n) => r.cubase.channels.find((c) => c.name === n);
    check(near(r.cubase.db(byName('Kick')), -2, 0.006), 'Kick (page 1) at -2, got ' + r.cubase.db(byName('Kick')));
    check(near(r.cubase.db(byName('Extra 17')), -4, 0.006), 'Extra 17 (page 3) at -4');
    check(Math.abs(byName('Extra 17').pan - 0.25) < 0.002, 'Extra 17 panned');
    check(near(r.cubase.db(byName('Perc')), -6.5, 0.006) && Math.abs(byName('Perc').pan - 0.7) < 0.002, 'Perc (in view) set');
    check(r.state.plan.done === 3 && r.state.plan.skipped === 0, 'all three done');
    check(r.state.slots[0].title === 'Perc', 'back at Perc, got ' + r.state.slots[0].title);
    check(r.cubase.channels.every((c, i) => ['Kick', 'Extra 17', 'Perc'].includes(c.name) || (c.volume === before[i][0] && c.pan === before[i][1])), 'nothing else moved');
    check(r.parse(await r.session.request(`undo ${r.now.toFixed(3)}`)).ok, 'undo starts');
    await r.run(6.0);
    check(r.cubase.channels.every((c, i) => Math.abs(c.volume - before[i][0]) < 1e-4 && Math.abs(c.pan - before[i][1]) < 1e-4), 'undo puts every fader and pan back');
    check(r.state.slots[0].title === 'Perc', 'still at Perc');
  },

  async 'script restart mid-session: writing works right after'(r) {
    await r.run(0.3);
    r.cubase.activate();   // e.g. "Reload Scripts" in Cubase
    await r.run(0.5);
    check(r.state.link === 'connected' && r.state.slots[3].title === 'Perc', 'reconnected with the bank');
    check((await r.cmd('setDb', 3, -9)).ok, 'move after reload');
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[3]), -9, 0.006), 'moved');
  },
};

(async () => {
  const exe = process.argv[2];
  if (!exe) { console.error('usage: node run.js <session_cli>'); process.exit(2); }
  for (const [name, fn] of Object.entries(scenarios)) {
    const r = new Rig(exe, NAMES);
    const before = failures;
    try { await fn(r); } catch (e) { failures++; console.log('  ERROR: ' + (e && e.stack || e)); }
    r.session.close();
    console.log((failures === before ? 'PASS ' : 'FAIL ') + name);
  }
  console.log(`${checks} checks, ${failures} failed`);
  process.exit(failures ? 1 : 0);
})();
