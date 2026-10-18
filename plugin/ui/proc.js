// Milestone 5: the processing on the EQ, Dynamics, Tone, Stereo and Space pages. One target for all the pages:
// "Full Mix" (Mixology's own processing on the Stereo Out) or a track (its Mixology Link). Every control comes
// from procspec.js (generated from the engine's table), so ranges and defaults always match the plug-ins.

import { PROC, PROC_INDEX, MODULE_ON, SPACE_ON, MAX_SPACE_EFFECTS, spaceLayers } from "./procspec.js";
import { el, icon, select, store } from "./ui.js";
import { say } from "./daws.js";

export { PROC, PROC_INDEX, MODULE_ON, SPACE_ON, MAX_SPACE_EFFECTS, spaceLayers };

// Which controls belong to which module (the interface's module ids).
const PREFIX = { gate: "gate_", eq: "eq", comp: "comp_", multi: "multi_", trans: "trans_", limit: "lim_", balance: "tb_", loud: "ld_",
  clip: "clip_", harm: "harm_", analog: "an_", final: "fin_", width: "st_",
  space: ["sp_", "duck_", "rev_", "dly_", "cho_", "pha_", "hall_", "room_", "spx_"] };
export const MODULE_NAMES = { gate: "Gate", eq: "EQ", comp: "Compressor", multi: "Multiband", trans: "Transient", limit: "Limiter",
  balance: "Tone Balance", loud: "Loudness", clip: "Clip Control", harm: "Harmonics", analog: "Analog Touch", final: "Final Touch", width: "Stereo",
  space: "Space" };

const HOLD_MS = 800;   // after you move a control, the plug-in's echo of older values is ignored this long

export function createProc(app) {
  const values = PROC.map((p) => p.def);
  const touched = new Map();   // id -> when you last moved it
  const pending = {};          // id -> value, sent together every 30 ms
  let flushTimer = null;
  const listeners = new Set();
  // The target saved with the project (by name: a track's key changes from session to session). Picked again as soon
  // as the plug-in lists that track (for a few seconds after the first list); until then the pages show the Full Mix.
  let wanted = store.get("procTarget", "Full Mix"), wantedUntil = 0;

  const api = {
    key: "mix",
    name: "Full Mix",
    targets: [{ key: "mix", name: "Full Mix", on: [], running: [] }],
    ok: true,
    loadedKey: null,   // the target whose values have arrived (until then they are only defaults)
    meters: null,
    spectrum: null,
    corr: null,
    sr: 48000,
    channels: 2,
    latencyMs: 0,
    mono: false,

    spec: (id) => PROC[PROC_INDEX[id]],
    get: (id) => values[PROC_INDEX[id]],
    isOn: (mod) => values[PROC_INDEX.chain_on] > 0.5 && values[PROC_INDEX[MODULE_ON[mod]]] > 0.5,
    switchedOn: (mod) => values[PROC_INDEX[MODULE_ON[mod]]] > 0.5,
    running: (mod) => !!(api.meters && api.meters.running && api.meters.running.includes(mod)),
    on: (f) => { listeners.add(f); return () => listeners.delete(f); },
    emit: (why) => { for (const f of listeners) f(why); },

    /** You moved a control. The module switches itself on when you touch one of its controls. */
    set(id, v, { autoOn = true } = {}) {
      const changes = { [id]: v };
      if (autoOn) {
        const mod = moduleOf(id);
        if (mod && !id.endsWith("_on") && values[PROC_INDEX[MODULE_ON[mod]]] < 0.5) changes[MODULE_ON[mod]] = 1;
      }
      api.setMany(changes);
    },
    setMany(changes) {
      const now = performance.now();
      for (const [id, v] of Object.entries(changes)) {
        const i = PROC_INDEX[id];
        if (i === undefined) continue;
        values[i] = v;
        touched.set(id, now);
        pending[id] = v;
      }
      if (!flushTimer) flushTimer = setTimeout(flush, 30);
      api.emit("values");
    },
    toggle(mod) { api.set(MODULE_ON[mod], api.switchedOn(mod) ? 0 : 1, { autoOn: false }); },
    /** Every control of a module back to its starting point (its on / off switch stays as it is). */
    reset(mod) {
      const changes = {};
      PROC.forEach((p) => { if (moduleOf(p.id) === mod && p.id !== MODULE_ON[mod]) changes[p.id] = p.def; });
      api.setMany(changes);
    },
    select(key) {
      const t = api.targets.find((x) => x.key === key);
      if (!t || key === api.key) return;
      wanted = null;
      api.key = key;
      api.name = t.name;
      store.set("procTarget", t.name);
      touched.clear();
      api.meters = null;
      api.spectrum = null;
      api.loadedKey = null;
      app.native.procTarget(key);
      api.emit("target");
    },
    setMono(on) {
      api.mono = !!on;
      app.native.setMonoListen(api.mono);
      api.emit("mono");
    },

    /** A target dropdown (the pages share the selection). opts.tracksOnly: no Full Mix (the Space page). */
    targetSelect(parent, x, y, w, opts = {}) {
      const options = () => api.targets.filter((t) => !(opts.tracksOnly && t.key === "mix")).map((t) => ({ value: t.key, label: t.name }));
      const shown = () => (options().some((o) => o.value === api.key) ? api.key : opts.placeholder || api.key);
      const s = select(parent, x, y, w, options(), shown(), (k) => api.select(k), opts);
      s.el.title = opts.tracksOnly ? "The track (its Mixology Link) or FX channel these controls work on"
        : `What these controls work on: the full mix (Mixology on the ${say(app.daw, "master")} channel) or one track (its Mixology Link)`;
      const refresh = () => { s.setOptions(options()); s.set(shown()); };
      api.on((why) => { if (why === "targets" || why === "target") refresh(); });
      return s;
    },

    /** The on / off switch for a module, in the style of the page's tags. */
    powerButton(parent, x, y, getMod) {
      // Built once; only its text and colour change (and only when they must). A button whose insides were
      // rebuilt while you held the mouse down never gets the click (the meters update 30 times a second).
      const b = el("button", "power-btn", parent, { left: x + "px", top: y + "px" });
      b.innerHTML = icon("power", 'stroke-width="2.6"');
      const label = el("span", "", b);
      let shown = "";
      const draw = () => {
        const mod = getMod();
        const on = api.switchedOn(mod);
        const key = mod + (on ? ":on:" : ":off:") + api.name;
        if (key === shown) return;
        shown = key;
        label.textContent = on ? "ON" : "OFF";
        b.classList.toggle("on", on);
        b.title = (on ? "Switch " : "Switch on ") + (MODULE_NAMES[mod] || mod) + (on ? " off (compare without it)" : "") + " on " + api.name;
      };
      b.addEventListener("click", () => { api.toggle(getMod()); draw(); });
      api.on((why) => { if (why !== "frame") draw(); });
      draw();
      return { el: b, draw };
    },

    onFrame(f) {
      if (f.procTargets) {
        api.targets = f.procTargets;
        let cur = api.targets.find((t) => t.key === api.key);
        if (wanted) {
          const now = performance.now();
          if (!wantedUntil) wantedUntil = now + 10000;
          const w = api.targets.find((t) => t.name === wanted);
          if (w) { cur = w; wanted = null; }
          else if (now > wantedUntil) wanted = null;   // that track isn't in this project (any more)
        }
        if (!cur) cur = api.targets.find((t) => t.name === api.name) || api.targets[0];
        if (cur && cur.key !== api.key) {
          api.key = cur.key;
          api.name = cur.name;
          api.loadedKey = null;
          touched.clear();
          app.native.procTarget(cur.key);
          api.emit("target");
        }
        api.emit("targets");
      }
      if (f.mono !== undefined && !!f.mono !== api.mono) { api.mono = !!f.mono; api.emit("mono"); }
      const p = f.proc;
      if (!p || p.key !== api.key) return;
      api.ok = !!p.ok;
      if (p.ok && p.values) {
        const now = performance.now();
        let changed = false;
        p.values.forEach((v, i) => {
          const id = PROC[i].id;
          if (now - (touched.get(id) || -1e9) < HOLD_MS) return;
          if (Math.abs(values[i] - v) > 1e-4) { values[i] = v; changed = true; }
        });
        if (changed) api.emit("values");
        api.loadedKey = p.key;
      }
      api.meters = p.meters || null;
      api.spectrum = p.spectrum || null;
      api.corr = p.corr ?? null;
      if (p.sr) api.sr = p.sr;
      if (p.channels) api.channels = p.channels;
      api.latencyMs = p.latencyMs || 0;
      api.emit("frame");
    },
  };

  function flush() {
    flushTimer = null;
    const changes = Object.assign({}, pending);
    for (const k of Object.keys(pending)) delete pending[k];
    if (Object.keys(changes).length) app.native.procSet(api.key, changes);
  }

  return api;
}

export function moduleOf(id) {
  if (id === "chain_on") return null;
  for (const [mod, prefix] of Object.entries(PREFIX))
    if ([].concat(prefix).some((p) => id.startsWith(p))) return mod;
  return null;
}

/** A knob definition for a control: range, default and curve from the spec, label and format from the page. */
export function knobFor(proc, id, extra) {
  const p = proc.spec(id);
  return Object.assign({ min: p.min, max: p.max, def: p.def, curve: p.curve === "log" ? "log" : "lin", step: p.step || undefined, value: proc.get(id) }, extra);
}

// ---------------------------------------------------------------------------------------------
// EQ curves: the same formulas as the engine (dsp/src/ProcDsp.cpp design() and ProcChain designBand()).

export function biquad(type, f0, gainDb, q, sr) {
  const A = Math.pow(10, gainDb / 40), w0 = (2 * Math.PI * Math.min(Math.max(f0, 5), 0.49 * sr)) / sr, cw = Math.cos(w0), sw = Math.sin(w0), alpha = sw / (2 * Math.max(q, 0.05));
  let b0, b1, b2, a0, a1, a2;
  if (type === "bell") { b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; }
  else if (type === "lowshelf") { const s = 2 * Math.sqrt(A) * alpha; b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s); a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; }
  else if (type === "highshelf") { const s = 2 * Math.sqrt(A) * alpha; b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s); a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; }
  else if (type === "highpass") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }
  else { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }   // lowpass
  return { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 };
}

export function magnitudeDb(k, f, sr) {
  const w = (2 * Math.PI * f) / sr, c1 = Math.cos(w), c2 = Math.cos(2 * w), s1 = Math.sin(w), s2 = Math.sin(2 * w);
  const nr = k.b0 + k.b1 * c1 + k.b2 * c2, ni = -(k.b1 * s1 + k.b2 * s2), dr = 1 + k.a1 * c1 + k.a2 * c2, di = -(k.a1 * s1 + k.a2 * s2);
  return 10 * Math.log10(Math.max((nr * nr + ni * ni) / (dr * dr + di * di), 1e-24));
}

export const EQ_SHAPES = ["bell", "lowshelf", "highshelf", "lowcut", "highcut"];   // the engine's order

/** One band as the engine builds it: null when it does nothing (flat), else { k, stages }. */
export function eqBand(type, f, g, q, mode, sr) {
  const shape = EQ_SHAPES[type] || "bell";
  const cut = shape === "lowcut" || shape === "highcut";
  if (!cut && Math.abs(g) <= 1e-4) return null;
  let qq = q;
  if (mode === 1 && !cut) qq = q * 0.6;
  if (mode === 2 && shape === "bell") qq = q * Math.min(1.6, Math.max(0.5, 0.45 + Math.abs(g) * 0.09));
  const k = biquad(shape === "lowcut" ? "highpass" : shape === "highcut" ? "lowpass" : shape, f, cut ? 0 : g, qq, sr);
  return { k, stages: cut && mode !== 1 ? 2 : 1 };
}
