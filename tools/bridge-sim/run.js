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
        getUniqueIDString(mapping) { if (mapping !== sim.mapping) throw new Error('bad mapping'); const c = sim.channelAt(k); return c ? c.uid : ''; },
        getRuntimeID() { return k; },
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
  async cmd(name, arg = 0) {
    const r = this.parse(await this.session.request(`cmd ${name} ${arg} ${this.now.toFixed(3)}`));
    return r;
  }
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
    check(r.state.slots[0].id !== 0, 'Cubase channel id reported');
    check(r.state.log.some((l) => l.includes('id chan-1000')), 'script logs the raw channel id');
  },

  async 'sets exact dB values on the locked channel only'(r) {
    await r.run(0.3);
    check((await r.cmd('lock', 0)).ok, 'lock Kick');
    for (const target of [0, -6, -12, 6, -0.5, -24]) {
      check((await r.cmd('setDb', target)).ok, 'setDb ' + target);
      await r.run(0.4);
      const kick = r.cubase.channels[0];
      check(near(r.cubase.db(kick), target, 0.006), `Kick at ${r.cubase.db(kick).toFixed(3)} dB, wanted ${target}`);
      check(r.state.lastMove.includes(target.toFixed(2).replace(/^(\d)/, '+$1')) || r.state.lastMove.includes(target.toFixed(2)), 'result text: ' + r.state.lastMove);
    }
    check((await r.cmd('minusInf')).ok, '-inf');
    await r.run(0.3);
    check(r.cubase.channels[0].volume === 0, 'Kick at -inf');
    check(r.cubase.channels.slice(1).every((c) => c.volume === 0.7766), 'no other fader moved');
  },

  async 'Cubase fader moves are mirrored without echo'(r) {
    await r.run(0.3);
    await r.cmd('lock', 1);
    const before = r.cubase.setCalls;
    for (let i = 0; i < 30; i++) { r.cubase.channels[1].volume = 0.4 + i * 0.01; r.cubase.fireChannel(r.cubase.channels[1]); await r.run(0.01); }
    await r.run(0.3);
    check(r.cubase.setCalls === before, 'no setProcessValue echoed back');
    check(near(r.state.slots[1].volume, 0.69, 1e-5), 'Mixology sees the final position ' + r.state.slots[1].volume);
    check(r.state.slots[1].volumeText === volumeText(0.69), 'and the matching display text');
  },

  async 'bank change pauses and never moves the wrong channel'(r) {
    await r.run(0.3);
    await r.cmd('lock', 1);   // Snare
    check((await r.cmd('bank', 4)).ok, 'shift right');
    await r.run(0.3);
    check(r.state.slots[1].title === 'Hats', 'slot 2 now Hats, got ' + r.state.slots[1].title);
    check(r.state.lock === 'paused', 'paused after bank change');
    check(!(await r.cmd('setDb', -20)).ok, 'write refused while paused');
    await r.run(0.2);
    check(r.cubase.channels.every((c) => c.volume === 0.7766), 'nothing moved');
    await r.cmd('bank', 0);   // reset
    await r.run(0.3);
    check(r.state.slots[0].title === 'Kick', 'reset bank starts at Kick, got ' + r.state.slots[0].title);
    await r.cmd('bank', 2);   // next bank: past the last channel
    await r.run(0.3);
    check(r.state.slots[0].title === '', 'slots past the last channel are empty');
  },

  async 'script refuses a write that races a bank change'(r) {
    await r.run(0.3);
    await r.cmd('lock', 0);
    await r.cmd('setDb', -18);
    r.cubase.bankOffset = 1;   // the user scrolls the mixer before the message lands...
    r.cubase.fireAll();        // ...but after Mixology sent it
    await r.run(0.4);
    check(r.cubase.channels.every((c) => c.volume === 0.7766), 'no channel moved');
    check(r.state.lock === 'paused', 'paused');
  },

  async 'reordered and renamed channels pause writing'(r) {
    await r.run(0.3);
    await r.cmd('lock', 2);   // Hats
    r.cubase.channels.push(r.cubase.channels.shift());   // Kick dragged to the end: every slot shifts
    r.cubase.fireAll();
    await r.run(0.2);
    check(r.state.lock === 'paused', 'reordering pauses');
    const hats = r.slotOf('Hats');
    check((await r.cmd('lock', hats)).ok, 're-lock Hats at its new slot ' + (hats + 1));
    const hatsCh = r.cubase.channels.find((c) => c.name === 'Hats');
    hatsCh.name = 'Hats Open';
    r.cubase.fireChannel(hatsCh);
    await r.run(0.2);
    check(r.state.lock === 'paused' && r.state.pauseReason.includes('renamed'), 'rename pauses: ' + r.state.pauseReason);
  },

  async 'automation write blocks moves at both ends'(r) {
    await r.run(0.3);
    await r.cmd('lock', 0);
    r.cubase.channels[0].write = 1;
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.1);
    const res = await r.cmd('setDb', -6);
    check(!res.ok && res.message.includes('automation'), 'Mixology refuses: ' + res.message);
    // Race: W switched on after Mixology sent the move.
    r.cubase.channels[0].write = 0;
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.1);
    check((await r.cmd('setDb', -6)).ok, 'allowed with W off');
    r.cubase.channels[0].write = 1;            // W switched on in Cubase before the move lands
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.4);
    check(r.cubase.channels[0].volume === 0.7766, 'script refused the racing write');
    check(r.state.log.some((l) => l.includes('writing automation')), 'and said why');
  },

  async 'pan, mute, solo and Restore'(r) {
    r.cubase.channels[4].volume = 0.61;
    r.cubase.channels[4].pan = 0.3;
    r.cubase.fireChannel(r.cubase.channels[4]);
    await r.run(0.3);
    await r.cmd('lock', 4);
    await r.cmd('setDb', 3);
    await r.run(0.4);
    await r.cmd('pan', 0.75);
    await r.cmd('mute', 1);
    await r.cmd('solo', 1);
    await r.run(0.2);
    const c = r.cubase.channels[4];
    check(c.mute === 1 && c.solo === 1 && near(c.pan, 0.75, 1e-4), 'pan / mute / solo applied');
    check(r.state.slots[4].panText === 'R50', 'pan text mirrored: ' + r.state.slots[4].panText);
    check((await r.cmd('restore')).ok, 'restore');
    await r.run(0.4);
    check(near(c.volume, 0.61, 1 / 16384), 'volume restored ' + c.volume);
    check(near(c.pan, 0.3, 1 / 16384) && c.mute === 0 && c.solo === 0, 'pan / mute / solo restored');
  },

  async 'non-English names survive the round trip'(r) {
    await r.run(0.3);
    const i = r.slotOf('Vocal été ✓');
    check(i === 11, 'accented name found at slot 12, got ' + i);
    check((await r.cmd('lock', i)).ok, 'lock it');
    check((await r.cmd('setDb', -3)).ok, 'move it');
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[11]), -3, 0.006), 'moved');
  },

  async 'callback order reversed (display text before value, values before title)'(r) {
    r.cubase.reversedOrder = true;
    await r.run(0.3);
    check(r.state.slots[2].title === 'Hats' && r.state.slots[2].volumeText === volumeText(0.7766), 'mirrored');
    await r.cmd('lock', 2);
    for (const target of [-6, 2, -15]) {
      await r.cmd('setDb', target);
      await r.run(0.4);
      check(near(r.cubase.db(r.cubase.channels[2]), target, 0.006), `Hats at ${r.cubase.db(r.cubase.channels[2]).toFixed(3)}, wanted ${target}`);
    }
    await r.cmd('bank', 4);
    await r.run(0.3);
    check(r.state.lock === 'paused', 'bank change still pauses');
  },

  async 'Cubase reports only through the bound controls (no channel callbacks)'(r) {
    r.cubase.routes = 'control';
    await r.run(0.3);
    check(r.state.slots[0].title === 'Kick' && r.state.slots[0].volumeText === volumeText(0.7766), 'names and values arrive: ' + r.state.slots[0].title);
    check(r.state.slots[0].id !== 0, 'channel id from the cached mapping');
    await r.cmd('lock', 0);
    await r.cmd('setDb', -6);
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[0]), -6, 0.006), 'moves land');
    r.cubase.channels[0].name = 'Kick 2';
    r.cubase.fireChannel(r.cubase.channels[0]);
    await r.run(0.2);
    check(r.state.lock === 'paused', 'rename still pauses');
    await r.cmd('refresh');
    await r.run(0.2);
    check(r.state.log.some((l) => l.includes('script v2: 12 named')), 'diagnostics: named channel count');
    check(r.state.log.some((l) => l.includes('calls: titles 0/')), 'diagnostics: which route fired: ' + r.state.log.filter((l) => l.includes('calls:')).pop());
    check(r.state.log.some((l) => l.includes('mapping ok, errors 0')), 'diagnostics: no script errors');
  },

  async 'Cubase reports only through the channel callbacks'(r) {
    r.cubase.routes = 'channel';
    await r.run(0.3);
    check(r.state.slots[3].title === 'Perc' && r.state.slots[3].panText === 'C', 'names and values arrive');
    await r.cmd('lock', 3);
    await r.cmd('setDb', 2);
    await r.run(0.4);
    check(near(r.cubase.db(r.cubase.channels[3]), 2, 0.006), 'moves land');
  },

  async 'script restart mid-session resumes the same channel'(r) {
    await r.run(0.3);
    await r.cmd('lock', 3);
    r.cubase.activate();   // e.g. "Reload Scripts" in Cubase
    await r.run(0.5);
    check(r.state.lock === 'locked', 'still locked after reload: ' + r.state.lock + ' ' + r.state.pauseReason);
    check((await r.cmd('setDb', -9)).ok, 'move after reload');
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
