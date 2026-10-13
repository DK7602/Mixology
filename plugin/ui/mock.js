// Browser preview only. Inside the plug-in, JUCE provides window.__JUCE__ before this runs and this
// file does nothing. Opened in a normal browser it simulates the plug-in: a playing mix, a Cubase project
// of 14 channels (12 in view) you can move, protect and restore, and Mixology Link on some of them.
(function () {
  if (typeof window.__JUCE__ !== "undefined") return;

  const send = (id, obj) => setTimeout(() => window.__JUCE__.backend.emitByBackend(id, JSON.stringify(obj)), 0);

  // --- fake Cubase -----------------------------------------------------------------------------
  const lawDb = (n) => (n <= 0 ? -1000 : 6 + 20 * Math.log10(Math.pow(n, 2.7) * (1 + 0.15 * (1 - n))));
  const toNorm = (db) => { let lo = 0, hi = 1; for (let i = 0; i < 40; i++) { const m = (lo + hi) / 2; if (lawDb(m) < db) lo = m; else hi = m; } return (lo + hi) / 2; };
  const text = (n) => { const d = lawDb(n); return d < -96 ? "-oo" : d.toFixed(2); };
  const panText = (p) => { const v = Math.round((p - 0.5) * 200); return v === 0 ? "C" : (v < 0 ? "L" : "R") + Math.abs(v); };
  const hashOf = (str) => { let h = 2166136261; for (const c of new TextEncoder().encode(str)) { h ^= c; h = Math.imul(h, 16777619) >>> 0; } return h & 0x0FFFFFFF; };
  const chans = [
    ["Kick", -3, 0.5, "#d23b3b", 1], ["Snare", -6, 0.5, "#3bbf6b", 1], ["Hats", -9.5, 0.62, "#d24fa0", 1], ["Perc", -12, 0.38, "#3ab3d6", 0],
    ["808", -4.5, 0.5, "#d6b23a", 1], ["Keys", -11, 0.42, "#3a8ed6", 1], ["Synth", -8, 0.58, "#3a6fd6", 0], ["Guitar", -10.5, 0.35, "#8f4fd6", 0],
    ["Lead Vox", -2, 0.5, "#4fd66b", 1], ["FX", -14, 0.7, "#d6d23a", 0], ["Adlibs", -7.5, 0.6, "#d68a3a", 1], ["Vox Double", -13, 0.3, "#6bd64f", 0],
    ["Pad", -16, 0.5, "#5a7bd6", 0], ["Riser", -18, 0.5, "#c7c7c7", 0],
  ].map(([name, db, pan, colour, link]) => ({ name, hash: hashOf(name), volume: toNorm(db), pan, mute: false, solo: false, read: name === "FX", write: false, colour, link: !!link, trim: 0, protected: name === "Lead Vox" }));
  const B = { offset: 0, lastMove: "", log: [], version: 1, restore: {} };
  const t0 = performance.now();
  const stamp = () => { const ms = Math.round(performance.now() - t0); const s = Math.floor(ms / 1000); return `${String(Math.floor(s / 60) % 60).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}.${String(ms % 1000).padStart(3, "0")}`; };
  const log = (t) => { B.log.push(stamp() + "  " + t); if (B.log.length > 200) B.log.shift(); B.version++; };
  log("MIDI ports open (browser preview). Waiting for the Mixology Bridge script in Cubase...");
  log("Connected to Cubase (Mixology Bridge script v3, 12 channels per bank).");
  const inSlot = (i) => chans[B.offset + i];
  const isChanged = (c) => { const r = B.restore[c.hash]; return !!r && (Math.abs(r.v - c.volume) > 1e-4 || Math.abs(r.p - c.pan) > 1e-4 || r.m !== c.mute || r.s !== c.solo); };
  const state = (withLog) => {
    const slots = Array.from({ length: 12 }, (_, i) => {
      const c = inSlot(i);
      if (!c) return { present: false, title: "", hash: 0 };
      return { present: true, title: c.name, hash: c.hash, hasVolume: true, volume: c.volume, volumeText: text(c.volume), volumeDb: lawDb(c.volume), hasPan: true, pan: c.pan, panText: panText(c.pan),
        mute: c.mute, solo: c.solo, read: c.read, write: c.write, colour: c.colour, protected: c.protected, duplicate: false, changed: isChanged(c),
        linkStatus: c.link ? "matched" : "none", trim: c.trim, linkAudio: true };
    });
    return {
      enabled: true, link: "connected", portProblem: "", script: 3, toCubase: "Mixology to Cubase", fromCubase: "Cubase to Mixology", slots,
      changedCount: chans.filter(isChanged).length, restorePoints: Object.keys(B.restore).length,
      linksSeen: chans.filter((c) => c.link).length, linksPaired: slots.filter((s) => s.linkStatus === "matched").length, linksShared: true,
      lastMove: B.lastMove, lawPoints: 37, marks: [6, 0, -6, -12, -24, -36, -48].map((db) => ({ db, pos: toNorm(db) })),
      logVersion: B.log.length, log: withLog ? B.log.slice() : undefined,
    };
  };
  const snap = (c) => { if (!B.restore[c.hash]) { B.restore[c.hash] = { v: c.volume, p: c.pan, m: c.mute, s: c.solo }; log(`Restore point saved for '${c.name}': ${text(c.volume)} dB, pan ${panText(c.pan)}.`); } };
  function command(name, arg) {
    const fail = (message) => ({ ok: false, message });
    const value = arg && typeof arg === "object" ? arg.value : arg;
    if (name === "bank") {
      B.offset = value === 0 ? 0 : value === 1 ? Math.max(0, B.offset - 12) : value === 2 ? Math.min(chans.length - 1, B.offset + 12) : value === 3 ? Math.max(0, B.offset - 1) : Math.min(chans.length - 1, B.offset + 1);
      log("Bank: " + ["reset", "previous bank", "next bank", "shift left", "shift right"][value] + ".");
      return { ok: true };
    }
    if (name === "refresh" || name === "reconnect") { B.version++; return { ok: true }; }
    if (name === "copyLog") { try { navigator.clipboard.writeText(B.log.join("\n")); } catch (e) { /* preview */ } return { ok: true }; }
    if (name === "forget") { B.restore = {}; log("Restore points cleared: the mix as it is now is the new starting point."); return { ok: true }; }
    if (name === "restoreAll") {
      let n = 0;
      for (const c of chans) if (isChanged(c) && !c.protected) { const r = B.restore[c.hash]; c.volume = r.v; c.pan = r.p; c.mute = r.m; c.solo = r.s; n++; }
      const msg = n ? `Restored ${n} channel${n === 1 ? "" : "s"}.` : "Everything Mixology changed is already back how it was.";
      log(msg);
      return { ok: true, info: msg };
    }
    const c = inSlot(arg.slot);
    if (!c) return fail("That strip is empty.");
    if (c.hash !== arg.hash) return fail(`That strip now shows '${c.name}' (the bank or the track order changed). Nothing was moved.`);
    if (name === "protect") { c.protected = !!value; log(value ? `Protected '${c.name}': Mixology won't move it.` : `'${c.name}' is no longer protected.`); return { ok: true }; }
    if (name === "faderEnd") { log(`Fader dragged in Mixology: '${c.name}' now shows ${text(c.volume)} dB`); return { ok: true }; }
    if (c.protected) return fail(`'${c.name}' is protected (padlock). Click the padlock to let Mixology move it.`);
    if (c.write) return fail(`'${c.name}' is writing automation (W is on). Switch W off first.`);
    if (name === "restore") { const r = B.restore[c.hash]; if (!r) return fail(`Mixology hasn't changed '${c.name}'.`); c.volume = r.v; c.pan = r.p; c.mute = r.m; c.solo = r.s; B.lastMove = `Restored '${c.name}': Cubase shows ${text(c.volume)} dB`; log(B.lastMove); return { ok: true }; }
    if (name === "trim") { c.trim = value; return { ok: true }; }
    snap(c);
    let warning = "";
    if (c.read && !c.warned) { c.warned = true; warning = `'${c.name}' has automation Read (R) on: if it has volume automation, playback will move the fader back.`; }
    if (name === "setDb" || name === "minusInf") { const db = name === "minusInf" ? -1000 : Math.min(6, value); c.volume = db <= -144 ? 0 : toNorm(db); B.lastMove = `Set '${c.name}' to ${db <= -144 ? "-inf" : (db >= 0 ? "+" : "") + db.toFixed(2)} dB (Cubase shows ${text(c.volume)})`; log(B.lastMove); }
    else if (name === "nudge") { const d = lawDb(c.volume) + value; c.volume = toNorm(d); B.lastMove = `Set '${c.name}' to ${d.toFixed(2)} dB`; }
    else if (name === "fader") { c.volume = Math.max(0, Math.min(1, value)); }
    else if (name === "pan") { c.pan = value; }
    else if (name === "mute") { c.mute = !!value; log(`${value ? "Muting" : "Unmuting"} '${c.name}'.`); }
    else if (name === "solo") { c.solo = !!value; log(`${value ? "Soloing" : "Unsoloing"} '${c.name}'.`); }
    B.version++;
    return { ok: true, warning };
  }

  // --- JUCE backend stand-in ------------------------------------------------------------------
  const demoUi = { page: "overview", values: {} };
  window.__JUCE__ = {
    initialisationData: {
      __juce__platform: [], __juce__registeredGlobalEventIds: [], __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [],
      __juce__functions: ["bridgeCommand", "getBridgeState", "getSettings", "setSetting", "setUiState", "setPage"],
    },
    postMessage(message) {
      const { eventId, payload } = JSON.parse(message);
      if (eventId !== "__juce__invoke") return;
      const [a, b] = payload.params || [];
      let result = true;
      if (payload.name === "bridgeCommand") result = command(a, b);
      else if (payload.name === "getBridgeState") result = state(true);
      else if (payload.name === "getSettings") {
        let ui = null; try { ui = localStorage.getItem("mixology-ui"); } catch (e) { /* none */ }
        result = { theme: null, ui: ui || JSON.stringify(demoUi), version: "0.2.0 (preview)" };
      }
      else if (payload.name === "setUiState") { try { localStorage.setItem("mixology-ui", a); } catch (e) { /* none */ } }
      else if (payload.name === "setPage") page = a;
      send("__juce__complete", { promiseId: payload.resultId, result });
    },
  };
  let page = "overview";

  // --- fake audio -----------------------------------------------------------------------------
  let t = 0, sentVersion = 0, sentLog = 0, integ = [];
  const shape = (i, n) => {
    const f = 20 * Math.pow(1000, i / (n - 1));
    const lo = f < 35 ? -12 * Math.log2(35 / f) : 0, hi = f > 14000 ? -16 * Math.log2(f / 14000) : 0;
    return -40 + lo + hi + 5 * Math.exp(-Math.pow(Math.log2(f / 60), 2) / 0.6) + 3 * Math.exp(-Math.pow(Math.log2(f / 120), 2) / 0.5)
      + 3 * Math.exp(-Math.pow(Math.log2(f / 450), 2) / 0.4) + 4 * Math.exp(-Math.pow(Math.log2(f / 2000), 2) / 0.5)
      + 2.5 * Math.exp(-Math.pow(Math.log2(f / 5000), 2) / 0.6) + 3.5 * Math.exp(-Math.pow(Math.log2(f / 11000), 2) / 0.5);
  };
  setInterval(() => {
    t += 1 / 30;
    const beat = Math.pow(Math.max(0, Math.sin(t * Math.PI * 2 * 1.2)), 8);
    const hat = Math.pow(Math.max(0, Math.sin(t * Math.PI * 2 * 4.8)), 6);
    const st = -13.5 + 1.2 * Math.sin(t / 5) + 1.5 * beat;
    integ.push(st); if (integ.length > 3000) integ.shift();
    const spectrum = [];
    for (let i = 0; i < 120; i++) spectrum.push(+(shape(i, 120) + (Math.random() - 0.5) * 2.4 + (i < 30 ? 6 * beat : 0) + (i > 90 ? 4 * hat : 0)).toFixed(1));
    const frame = {
      peakL: +(-6.5 + 4.5 * beat + Math.random()).toFixed(1), peakR: +(-7 + 4.5 * beat + Math.random()).toFixed(1),
      momentary: +(st + 1.5 * beat).toFixed(1), shortTerm: +st.toFixed(1), integrated: +(integ.reduce((a, b) => a + b, 0) / integ.length).toFixed(1),
      truePeak: -4.2, sr: 48000, spectrum,
      trackMeters: Array.from({ length: 12 }, (_, i) => {
        const c = inSlot(i); if (!c || !c.link) return null; if (c.mute) return [-100, -100];
        const env = [beat, Math.pow(Math.max(0, Math.sin(t * Math.PI * 2 * 0.6 + 1.6)), 10), hat, hat * 0.7, beat, 0.6, 0.55, 0.5, 0.75 + 0.2 * Math.sin(t * 3), 0.3, 0.45 * (Math.sin(t) > 0), 0.5][i] || 0.4;
        const lvl = -14 + c.trim + lawDb(c.volume) + 12 * env + (Math.random() - 0.5) * 2;   // Link level + fader
        return [lvl, lvl - 1.2 + (c.pan - 0.5) * 6];
      }),
    };
    if (page === "stereo" || page === "master") {
      const dots = [];
      for (let i = 0; i < 512; i++) {
        const m = (Math.random() - 0.5) * 0.9 * (0.4 + beat), s = (Math.random() - 0.5) * 0.35 * (0.5 + hat) * Math.random();
        dots.push(+s.toFixed(3), +m.toFixed(3));
      }
      frame.scope = dots;
      frame.corr = +(0.62 + 0.15 * Math.sin(t / 3)).toFixed(2);
    }
    if (B.version !== sentVersion) {
      sentVersion = B.version;
      frame.bridge = state(B.log.length !== sentLog);
      sentLog = B.log.length;
    }
    window.__JUCE__.backend && window.__JUCE__.backend.emitByBackend("mixFrame", JSON.stringify(frame));
  }, 1000 / 30);
})();
