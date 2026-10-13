// Browser preview only. Inside the plug-in, JUCE provides window.__JUCE__ before this runs and this
// file does nothing. Opened in a normal browser it simulates the plug-in: a playing mix, and a Cubase
// bank of 12 channels you can lock and move, so the interface can be designed and checked without a DAW.
(function () {
  if (typeof window.__JUCE__ !== "undefined") return;

  const send = (id, obj) => setTimeout(() => window.__JUCE__.backend.emitByBackend(id, JSON.stringify(obj)), 0);

  // --- fake Cubase -----------------------------------------------------------------------------
  const lawDb = (n) => (n <= 0 ? -1000 : 6 + 20 * Math.log10(Math.pow(n, 2.7) * (1 + 0.15 * (1 - n))));
  const toNorm = (db) => { let lo = 0, hi = 1; for (let i = 0; i < 40; i++) { const m = (lo + hi) / 2; if (lawDb(m) < db) lo = m; else hi = m; } return (lo + hi) / 2; };
  const text = (n) => { const d = lawDb(n); return d < -96 ? "-oo" : d.toFixed(2); };
  const panText = (p) => { const v = Math.round((p - 0.5) * 200); return v === 0 ? "C" : (v < 0 ? "L" : "R") + Math.abs(v); };
  const chans = [
    ["Kick", -3, 0.5, "#d23b3b"], ["Snare", -6, 0.5, "#3bbf6b"], ["Hats", -9.5, 0.62, "#d24fa0"], ["Perc", -12, 0.38, "#3ab3d6"],
    ["808", -4.5, 0.5, "#d6b23a"], ["Keys", -11, 0.42, "#3a8ed6"], ["Synth", -8, 0.58, "#3a6fd6"], ["Guitar", -10.5, 0.35, "#8f4fd6"],
    ["Lead Vox", -2, 0.5, "#4fd66b"], ["FX", -14, 0.7, "#d6d23a"], ["Adlibs", -7.5, 0.6, "#d68a3a"], ["Vox Double", -13, 0.3, "#6bd64f"],
    ["Pad", -16, 0.5, "#5a7bd6"], ["Riser", -18, 0.5, "#c7c7c7"],
  ].map(([name, db, pan, colour]) => ({ name, volume: toNorm(db), pan, mute: false, solo: false, write: false, colour }));
  const B = { offset: 0, lock: "none", lockedSlot: -1, lockedTitle: "", pauseReason: "", snapshot: { valid: false }, lastMove: "", log: [], version: 1 };
  const t0 = performance.now();
  const stamp = () => { const ms = Math.round(performance.now() - t0); const s = Math.floor(ms / 1000); return `${String(Math.floor(s / 60) % 60).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}.${String(ms % 1000).padStart(3, "0")}`; };
  const log = (t) => { B.log.push(stamp() + "  " + t); if (B.log.length > 200) B.log.shift(); B.version++; };
  log("MIDI ports open (browser preview). Waiting for the Mixology Bridge script in Cubase...");
  log("Connected to Cubase (Mixology Bridge script v1, 12 channels per bank).");
  const inSlot = (i) => chans[B.offset + i];
  const state = (withLog) => ({
    enabled: true, link: "connected", portProblem: "", script: 1, toCubase: "Mixology to Cubase", fromCubase: "Cubase to Mixology",
    slots: Array.from({ length: 12 }, (_, i) => {
      const c = inSlot(i);
      if (!c) return { present: false, title: "" };
      return { present: true, title: c.name, hasVolume: true, volume: c.volume, volumeText: text(c.volume), volumeDb: lawDb(c.volume), pan: c.pan, panText: panText(c.pan), mute: c.mute, solo: c.solo, read: false, write: c.write, colour: c.colour };
    }),
    lock: B.lock, lockedSlot: B.lockedSlot, lockedTitle: B.lockedTitle, pauseReason: B.pauseReason, moving: false, lastMove: B.lastMove,
    lawPoints: 37, marks: [6, 0, -6, -12, -24, -36, -48].map((db) => ({ db, pos: toNorm(db) })),
    snapshot: B.snapshot, logVersion: B.log.length, log: withLog ? B.log.slice() : undefined,
  });
  const locked = () => (B.lock === "locked" ? inSlot(B.lockedSlot) : null);
  const snap = (c) => { if (!B.snapshot.valid || B.snapshot.title !== c.name) { B.snapshot = { valid: true, title: c.name, volumeText: text(c.volume), v: c.volume, p: c.pan, m: c.mute, s: c.solo }; log(`Restore point saved for '${c.name}': ${text(c.volume)} dB, pan ${panText(c.pan)}.`); } };
  function command(name, arg) {
    const fail = (message) => ({ ok: false, message });
    if (name === "lock") { const c = inSlot(arg); if (!c) return fail("That slot is empty."); B.lock = "locked"; B.lockedSlot = arg; B.lockedTitle = c.name; B.pauseReason = ""; log(`Locked '${c.name}' (slot ${arg + 1}). Only this channel can be moved.`); return { ok: true }; }
    if (name === "unlock") { if (B.lock !== "none") log(`Unlocked '${B.lockedTitle}'.`); B.lock = "none"; B.lockedSlot = -1; B.lockedTitle = ""; B.version++; return { ok: true }; }
    if (name === "bank") {
      const before = B.offset;
      B.offset = arg === 0 ? 0 : arg === 1 ? Math.max(0, B.offset - 12) : arg === 2 ? Math.min(chans.length - 1, B.offset + 12) : arg === 3 ? Math.max(0, B.offset - 1) : Math.min(chans.length - 1, B.offset + 1);
      log("Bank: " + ["reset", "previous bank", "next bank", "shift left", "shift right"][arg] + ".");
      if (B.lock === "locked" && B.offset !== before) { const c = inSlot(B.lockedSlot); B.lock = "paused"; B.pauseReason = `slot ${B.lockedSlot + 1} changed from '${B.lockedTitle}' to ${c ? "'" + c.name + "'" : "(empty)"} (bank moved, tracks reordered or renamed). Nothing will be written until you lock a channel again.`; log("PAUSED: " + B.pauseReason); }
      return { ok: true };
    }
    if (name === "refresh" || name === "reconnect" || name === "faderEnd") { if (name === "faderEnd" && locked()) log(`Fader dragged in Mixology: '${locked().name}' now shows ${text(locked().volume)} dB`); B.version++; return { ok: true }; }
    if (name === "copyLog") { try { navigator.clipboard.writeText(B.log.join("\n")); } catch (e) { /* preview */ } return { ok: true }; }
    const c = locked();
    if (!c) return fail(B.lock === "paused" ? "Paused: " + B.pauseReason : "Lock a channel first.");
    if (c.write) return fail(`'${c.name}' is writing automation (W is on). Switch W off first.`);
    snap(c);
    if (name === "setDb" || name === "minusInf") { const db = name === "minusInf" ? -1000 : Math.min(6, arg); c.volume = db <= -144 ? 0 : toNorm(db); B.lastMove = `Set '${c.name}' to ${db <= -144 ? "-inf" : (db >= 0 ? "+" : "") + db.toFixed(2)} dB (Cubase shows ${text(c.volume)})`; log(`Moving '${c.name}' to ${db <= -144 ? "-inf" : db.toFixed(2)} dB...`); log(B.lastMove); }
    else if (name === "nudge") { const d = lawDb(c.volume) + arg; c.volume = toNorm(d); B.lastMove = `Set '${c.name}' to ${d.toFixed(2)} dB`; log(B.lastMove); }
    else if (name === "fader") { c.volume = Math.max(0, Math.min(1, arg)); B.lastMove = `Set '${c.name}' to ${text(c.volume)} dB`; }
    else if (name === "pan") { c.pan = arg; }
    else if (name === "mute") { c.mute = !!arg; log(`${arg ? "Muting" : "Unmuting"} '${c.name}'.`); }
    else if (name === "solo") { c.solo = !!arg; log(`${arg ? "Soloing" : "Unsoloing"} '${c.name}'.`); }
    else if (name === "restore") { if (!B.snapshot.valid) return fail("Nothing to restore yet."); c.volume = B.snapshot.v; c.pan = B.snapshot.p; c.mute = B.snapshot.m; c.solo = B.snapshot.s; B.lastMove = `Set '${c.name}' to ${text(c.volume)} dB`; log(`Restoring '${c.name}' to ${B.snapshot.volumeText} dB...`); }
    B.version++;
    return { ok: true };
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
        result = { theme: null, ui: ui || JSON.stringify(demoUi), version: "0.1.0 (preview)" };
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
        const c = inSlot(i); if (!c || c.mute) return [-100, -100];
        const env = [beat, Math.pow(Math.max(0, Math.sin(t * Math.PI * 2 * 0.6 + 1.6)), 10), hat, hat * 0.7, beat, 0.6, 0.55, 0.5, 0.75 + 0.2 * Math.sin(t * 3), 0.3, 0.45 * (Math.sin(t) > 0), 0.5][i] || 0.4;
        const lvl = lawDb(c.volume) - 10 + 14 * env + (Math.random() - 0.5) * 2;
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
