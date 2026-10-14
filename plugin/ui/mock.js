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
    ["Pad", -16, 0.5, "#5a7bd6", 0], ["Riser", -18, 0.5, "#c7c7c7", 0], ["FX 1-Vox Verb", -12, 0.5, "#9a6bd6", 1],
  ].map(([name, db, pan, colour, link]) => ({ name, hash: hashOf(name), volume: toNorm(db), pan, mute: false, solo: false, read: name === "FX", write: false, colour, link: !!link, trim: 0, protected: name === "Lead Vox" }));
  const B = { offset: 0, lastMove: "", log: [], version: 1, restore: {}, an: { state: "idle", seconds: 0, playing: true, report: null }, settings: {},
    apply: { active: false, what: "", done: 0, total: 0, skipped: 0, message: "", notes: [], serial: 0, canUndo: false, undoWhat: "", hearingReference: false },
    planId: 0, plan: null, undo: null };
  const ROLE_OF = { Kick: "kick", Snare: "snare", Hats: "hats", Perc: "perc", "808": "bass", Keys: "keys", Synth: "synth", Guitar: "guitar", "Lead Vox": "vox", FX: "fx", Adlibs: "adlibs", "Vox Double": "adlibs", Pad: "synth", Riser: "fx" };
  // Voxology on the vocals (M6, the suite link), as the hub reports it.
  const VOXOLOGY = {
    "Lead Vox": { beat: false, on: true, style: "Trap lead", comp: true, compDb: 4.2, deEsser: true, reverb: true, delay: false, eq: true, pitch: true, doubler: false, version: "0.47.0" },
    Adlibs: { beat: false, on: true, style: "Ad-libs", comp: true, compDb: 3.1, deEsser: true, reverb: false, delay: true, eq: true, pitch: true, doubler: false, version: "0.47.0" },
  };
  const roleOf = (c) => (B.settings[c.hash] && B.settings[c.hash].role) || ROLE_OF[c.name] || "other";
  function fakeReport() {
    const linked = chans.filter((c) => c.link);
    const tracks = linked.map((c, i) => ({ hash: c.hash, name: c.name, role: roleOf(c), roleFrom: B.settings[c.hash] && B.settings[c.hash].role ? "you" : VOXOLOGY[c.name] ? "voxology" : i % 4 === 3 ? "sound" : "name",
      excluded: !!(B.settings[c.hash] && B.settings[c.hash].excluded), faderKnown: true, activity: 0.8, loudness: +(lawDb(c.volume) - 12 + (i % 3)).toFixed(1), peak: +(lawDb(c.volume) - 3 + (i === 0 ? 4 : 0)).toFixed(1), width: c.name === "Keys" ? 0.31 : 0.04 }));
    const sg = (id, kind, icon, title, text, need, steps, tracks) => ({ id, kind, icon, title, text, need, steps, tracks });
    const refHash = Object.keys(B.settings).find((h) => B.settings[h].reference);
    const refTrack = refHash && tracks.find((t) => String(t.hash) === String(refHash));
    if (refTrack) { refTrack.excluded = true; refTrack.reference = true; }
    const bandsOf = (tilt) => Array.from({ length: 31 }, (_, b) => +(tilt(b)).toFixed(1));
    const mixBands = bandsOf((b) => 2.5 * Math.exp(-((b - 5) ** 2) / 6) + 1.8 * Math.exp(-((b - 12) ** 2) / 5) - 0.12 * (b - 15) - (b > 26 ? (b - 26) * 1.2 : 0));
    const reference = !refTrack ? { chosen: false } : {
      chosen: true, heard: true, hash: refTrack.hash, name: refTrack.name, loudness: -8.1, loudnessDiff: 4.7, width: 0.34, widthDiff: -0.06,
      bands: bandsOf((b) => 4 * Math.exp(-((b - 4) ** 2) / 5) - 0.1 * (b - 15) + (b > 25 ? 0.6 : 0)),
      regions: [
        { name: "Low end (40 - 125 Hz)", diff: -2.4, text: `Your mix has 2.4 dB less low end than '${refTrack.name}'.` },
        { name: "Low mids (200 - 500 Hz)", diff: 2.9, text: `Your mix has 2.9 dB more low mids than '${refTrack.name}'.` },
        { name: "Mids (630 Hz - 2 kHz)", diff: 0.6, text: `About the same as '${refTrack.name}'.` },
        { name: "Presence (2.5 - 5 kHz)", diff: -0.9, text: `About the same as '${refTrack.name}'.` },
        { name: "Air (8 - 16 kHz)", diff: -3.1, text: `Your mix has 3.1 dB less air than '${refTrack.name}'.` },
      ] };
    return {
      reference, mixWidth: 0.28, hasVocal: true, vocalRatio: -8.2, vocalFromYou: B.vocal != null, vocalTarget: B.vocal != null ? -8.2 + B.vocal : -1,
      seconds: B.an.seconds, style: (window.__mixStyle || "Hip Hop - Modern Clean"), mixLoudness: -12.8, mixPeak: -4.1, mixCorrelation: 0.62,
      unheard: chans.filter((c) => !c.link).map((c) => c.name).slice(0, 4), mixBands, targetBands: [], tracks,
      suggestions: [
        sg("kick-bass", "warn", "kick", "Kick and bass fight for the low end", "When they hit together, 72 % of their low end (40 - 160 Hz) sits in the same place. That makes the bottom muddy and the kick lose its punch.",
          "Yes, for most Hip Hop mixes: a clean low end is what makes it hit on big speakers and in the car.",
          ["Let each own a spot: the kick's punch around 60 - 100 Hz, the 808 / bass below 60 Hz (a small EQ dip in each where the other lives).", "Or duck the 808 for a moment each time the kick hits (sidechain compression on the 808, triggered by the kick)."], ["Kick", "808"]),
        sg("vocal-pocket", "warn", "mic", "Instruments cover the vocal's words", "'Keys' and 'Snare' are nearly as loud as the vocal at 1.6 - 4 kHz while it sings: the range where words are understood.",
          "Yes, if lyrics get lost in busy parts.", ["Dip 'Keys' and 'Snare' 2 - 3 dB around 2 - 3 kHz (a medium EQ bell), or pan them a little off-centre.", "Vocal Pocket (left of the analyzer) makes these dips for you, in each track's Mixology Link."], ["Keys", "Snare"]),
        sg("highpass", "info", "lowcut", "Low rumble in tracks that don't need it", "'Adlibs' has a lot of energy below 100 Hz. It adds mud and fights the kick and bass without being heard as part of the sound.",
          "Usually yes: it's free headroom and a cleaner low end.", ["Put a low cut (high-pass) on it: about 80 - 100 Hz for vocals, 60 - 80 Hz for keys, guitars and synths."], ["Adlibs"]),
        sg("unheard", "info", "link", "5 channels without Mixology Link", "'Perc', 'Synth', 'Guitar' and 'FX' and 1 more play in your mix, but Mixology can't hear them one by one, so the advice below leaves them out.",
          "Only if those tracks matter for the balance (they usually do).", ["Insert Mixology Link on each of them, after their other plug-ins, then Analyze Mix again."], ["Perc", "Synth", "Guitar", "FX"]),
        sg("vocal-ok", "calm", "vox", "Vocal level fits Hip Hop - Modern Clean", "While it sings, the vocal sits +1.4 dB against the rest of the mix.", "No.", [], ["Lead Vox"]),
      ],
    };
  }
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
      const set = B.settings[c.hash] || {};
      return { role: roleOf(c), roleSet: !!set.role, excluded: !!set.excluded, musicBus: !!set.musicBus, present: true, title: c.name, hash: c.hash, hasVolume: true, volume: c.volume, volumeText: text(c.volume), volumeDb: lawDb(c.volume), hasPan: true, pan: c.pan, panText: panText(c.pan),
        mute: c.mute, solo: c.solo, read: c.read, write: c.write, colour: c.colour, protected: c.protected, duplicate: false, changed: isChanged(c),
        linkStatus: c.link ? "matched" : "none", trim: c.trim, linkAudio: true, stereoPanner: c.name === "Keys", voxology: VOXOLOGY[c.name] };
    });
    return {
      enabled: true, link: "connected", portProblem: "", script: 3, toCubase: "Mixology to Cubase", fromCubase: "Cubase to Mixology", slots,
      changedCount: chans.filter(isChanged).length, restorePoints: Object.keys(B.restore).length, scanning: B.an.state === "reading",
      analysis: { state: B.an.state, seconds: B.an.seconds, enough: 20, max: 60, playing: B.an.playing, heard: chans.filter((c) => c.link).length, report: B.an.report },
      apply: { ...B.apply },
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
    if (name === "analyze") {
      window.__mixStyle = value && value.name;
      B.an.state = "reading"; B.an.seconds = 0; B.version++;
      setTimeout(() => { B.an.state = "listening"; B.version++; }, 1200);
      return { ok: true };
    }
    if (name === "analyzeFinish") { if (B.an.seconds < 1) return fail("Nothing heard yet: press play in Cubase."); B.an.state = "done"; B.an.report = fakeReport(); B.version++; return { ok: true }; }
    if (name === "analyzeCancel") { B.an.state = B.an.report ? "done" : "idle"; B.version++; return { ok: true }; }
    if (name === "plan") {
      const titles = { auto: "Auto Mix", balance: "Balance", clean: "Clean Up", pocket: "Vocal Pocket", stereo: "Stereo Architect", polish: "Polish" };
      const title = titles[value] || "Auto Mix";
      B.planId++;
      if (!B.an.report) return { ok: true, plan: { id: B.planId, tool: value, title, valid: false, summary: `Press Analyze Mix first and play the song for 20 - 60 seconds: ${title} works from what it hears.`, notes: [], changes: [] } };
      const ch = (n) => chans.find((c) => c.name === n);
      const lvl = (n, d, why, big) => { const f = lawDb(ch(n).volume); return { hash: ch(n).hash, name: n, role: roleOf(ch(n)), kind: "level", from: +f.toFixed(1), to: +(f + Math.max(-6, Math.min(6, d))).toFixed(1), wanted: +(f + d).toFixed(1), big: !!big, hz: 0, why }; };
      const pan = (n, to, why) => ({ hash: ch(n).hash, name: n, role: roleOf(ch(n)), kind: "pan", from: Math.round((ch(n).pan - 0.5) * 200), to, wanted: to, big: false, hz: 0, why });
      const eq = (n, kind, from, to, hz, why) => ({ hash: ch(n).hash, name: n, role: roleOf(ch(n)), kind, from, to, wanted: to, big: false, hz, why });
      const proc = (n, kind, fromText, toText, why) => ({ hash: ch(n).hash, name: n, role: roleOf(ch(n)), kind, from: 0, to: 0, wanted: 0, big: false, hz: 0, why, fromText, toText, fullMix: false });
      const parts = {
        balance: [lvl("Hats", -3.4, "the hats sit 4.4 dB louder against the rest of the beat than Hip Hop - Modern Clean usually has"),
          lvl("Kick", -1.5, "makes room for the vocal"), lvl("808", -1.5, "makes room for the vocal"), lvl("Snare", -1.5, "makes room for the vocal"),
          lvl("Adlibs", 1.5, "follows the lead vocal")],
        stereo: [pan("Hats", 15, "the mix leans left by 1.9 dB: this evens it out"), pan("Keys", -45, "pairs with 'Synth': one left, one right sounds wide and keeps the middle free for the vocal")],
        clean: [eq("Adlibs", "lowcut", 0, 100, 100, "41% of its energy is below 100 Hz, where it only muddies the kick and bass"),
          eq("Keys", "mud", 0, -2.5, 315, "the mix has 3.6 dB more 200 - 500 Hz than Hip Hop - Modern Clean usually does; this is one of the tracks carrying it")],
        pocket: [eq("Keys", "pocket", 0, -3, 2500, "at 1.6 - 4 kHz it is 2.1 dB under the vocal while it sings, covering its words"),
          eq("Snare", "pocket", 0, -2, 3150, "at 1.6 - 4 kHz it is 4.8 dB under the vocal while it sings, covering its words")],
        polish: [
          proc("Lead Vox", "eq", "Flat", "+2.0 dB at 3.5 kHz", "the vocal is dull: its presence (2 - 5 kHz) is 4.2 dB under its body; a little lift makes every word clear"),
          proc("Lead Vox", "eq", "Flat", "+1.5 dB at 10 kHz (shelf)", "the vocal has little air (8 - 12 kHz is 8.1 dB under its presence): a gentle shelf opens it up"),
          proc("Lead Vox", "comp", "Off", "3:1 from −12.0 dB", "its level swings 10 dB between quiet and loud lines: a compressor (about 4 dB on the loud parts) keeps every word in front"),
          proc("808", "width", "Lows 34 % on the sides", "Lows in the middle", "its low end is spread in stereo: in the middle it hits harder and stays solid in mono and on club systems"),
          proc("FX 1-Vox Verb", "space", "Off", "Vocal Plate, 100 % wet", "the lead vocal's reverb: a vocal plate (1.4 s) on this FX channel, ducking 6 dB under 'Lead Vox' so the words stay clear"),
          proc("Adlibs", "space", "Dry", "Ping-pong 1/8, mix 15 %", "ad-libs that echo left and right on the beat fill the gaps around the lead without covering it"),
          Object.assign(proc("Lead Vox", "tone", "Air +0.0 dB", "Air +1.5 dB", "against Hip Hop - Modern Clean's tone your mix is short of air (3.1 dB): a gentle Tone Balance on the whole mix (before Masterology)"), { name: "Full Mix", role: "other", hash: 0, fullMix: true }),
        ],
      };
      const changes = value === "auto" ? [...parts.balance, ...parts.stereo, ...parts.clean, ...parts.pocket, ...parts.polish] : parts[value] || [];
      const notes = ["'Lead Vox': protected (padlock), so it stays where it is (it would have moved +1.5 dB)."];
      if (value === "auto" || value === "clean" || value === "pocket") notes.push("'Synth' has no Mixology Link Mixology can reach: vocal pocket skipped.");
      if (value === "auto" || value === "polish") notes.push("Send 'Lead Vox' to 'FX 1-Vox Verb' in Cubase (MixConsole, Sends, about -10 dB): Mixology can't add sends.");
      const hd = value === "auto" || value === "balance" ? { headroomDb: -12.8, headroomCount: 8, headroomWhy: "Your mix peaks at +6.8 dBTP. Masterology works best with peaks around -6: every fader goes down 12.8 dB together. The balance stays the same, the mix just gets quieter (Masterology brings the level back)." } : { headroomDb: 0, headroomCount: 0, headroomWhy: "" };
      const busSet = Object.values(B.settings).some((x) => x && x.musicBus);
      B.plan = { ...hd, groupHints: (value === "auto" || value === "balance") && !busSet ? ["Pad"] : [], id: B.planId, tool: value, title, valid: true, summary: `${changes.length} changes for ${window.__mixStyle || "Hip Hop - Modern Clean"}: levels: the hats -3.4 dB, the kick -1.5 dB; pans: 2 tracks; 1 low cut, 1 mud dip; 2 pocket dips.`, notes, changes };
      return { ok: true, plan: B.plan };
    }
    if (name === "vocalTaste") {
      if (!B.an.report) return fail("Analyze Mix first (with the vocal singing): then Mixology knows where your vocal sits now.");
      B.vocal = value === "style" ? null : { right: 0, louder: 2.5, quieter: -2.5 }[value];
      B.an.report = fakeReport();
      B.version++;
      return { ok: true, info: value === "style" ? "Mixology aims for the style's usual vocal level again." : value === "right" ? "Got it: Mixology keeps your vocal where it is (saved with the project)." : `Got it: Balance and Auto Mix now aim for the vocal 2.5 dB ${value === "louder" ? "higher" : "lower"} than now (saved with the project).` };
    }
    if (name === "planApply") {
      if (!B.plan || arg.id !== B.plan.id) return fail("That plan is out of date: open it again.");
      const picked = (arg.picked || []).map((i) => B.plan.changes[i]).filter(Boolean);
      if (!picked.length && !arg.headroom) return fail("Tick at least one change.");
      const before = chans.map((c) => ({ c, v: c.volume, p: c.pan }));
      Object.assign(B.apply, { active: true, what: B.plan.title, done: 0, total: new Set(picked.filter((c) => c.kind === "level" || c.kind === "pan").map((c) => c.name)).size, skipped: 0, notes: [], canUndo: false, undoWhat: B.plan.title });
      B.version++;
      let k = 0;
      const tick = setInterval(() => {
        const c = picked[k++];
        if (c) {
          const t = chans.find((x) => x.hash === c.hash);
          if (c.kind === "level") { snap(t); t.volume = toNorm(arg.allowBig ? c.wanted : c.to); }
          if (c.kind === "pan") { snap(t); t.pan = (c.to + 100) / 200; }
          B.apply.done = Math.min(B.apply.total, B.apply.done + 1);
          B.version++;
          return;
        }
        clearInterval(tick);
        Object.assign(B.apply, { active: false, message: `${B.plan.title}: changed ${B.apply.total} channels.`, serial: B.apply.serial + 1, canUndo: true });
        B.undo = before;
        log(B.apply.message);
      }, 250);
      return { ok: true, info: `${B.plan.title}: moving channels in Cubase, Assist EQ on tracks.` };
    }
    if (name === "planUndo") {
      if (!B.undo) return fail("Nothing to undo.");
      for (const u of B.undo) { u.c.volume = u.v; u.c.pan = u.p; }
      B.undo = null;
      Object.assign(B.apply, { canUndo: false, message: "Undo done.", serial: B.apply.serial + 1 });
      B.version++;
      return { ok: true, info: "Undoing: everything it changed goes back.", undo: { bridge: false } };
    }
    if (name === "setReference") {
      for (const h of Object.keys(B.settings)) if (B.settings[h]) B.settings[h].reference = false;
      if (value) (B.settings[arg.hash] = B.settings[arg.hash] || {}).reference = true;
      B.apply.hearingReference = false;
      if (B.an.report) B.an.report = fakeReport();
      B.version++;
      return { ok: true };
    }
    if (name === "refListen") {
      if (!B.an.report || !B.an.report.reference.chosen) return fail("Choose your reference song first.");
      B.apply.hearingReference = value === "ref";
      B.version++;
      return { ok: true, info: value === "ref" ? `Playing '${B.an.report.reference.name}' alone, matched to your mix's loudness (its Trim: -4.7 dB).` : "Back to your mix." };
    }
    if (name === "setMusicBus") {
      for (const h of Object.keys(B.settings)) if (B.settings[h]) B.settings[h].musicBus = false;
      const target = arg.hash || (chans.find((c) => c.name === arg.name) || {}).hash;
      if (value && target) (B.settings[target] = B.settings[target] || {}).musicBus = true;
      B.version++;
      return { ok: true, info: value ? "Got it: the music plays through this group. Mixology moves the music on its fader, and never counts it as extra music." : "This channel is no longer the music group." };
    }
    if (name === "setRole" || name === "setExcluded") {
      const set = (B.settings[arg.hash] = B.settings[arg.hash] || {});
      if (name === "setRole") set.role = value || undefined; else set.excluded = !!value;
      if (B.an.report) B.an.report = fakeReport();
      B.version++;
      return { ok: true };
    }
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
      __juce__functions: ["bridgeCommand", "getBridgeState", "getSettings", "setSetting", "setUiState", "setPage", "procTarget", "procSet", "setMonoListen", "spaceDuck"],
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
        result = { theme: null, ui: ui || JSON.stringify(demoUi), version: "0.3.0 (preview)" };
      }
      else if (payload.name === "setUiState") { try { localStorage.setItem("mixology-ui", a); } catch (e) { /* none */ } }
      else if (payload.name === "setPage") { page = a; if (page !== "stereo") P.mono = false; }
      else if (payload.name === "procTarget") { P.key = a; P.listIn = 0; }
      else if (payload.name === "procSet") result = procSet(a, b);
      else if (payload.name === "setMonoListen") result = P.mono = !!a;
      else if (payload.name === "spaceDuck") { P.duck[a] = (b || []).map(Number); P.listIn = 0; }
      send("__juce__complete", { promiseId: payload.resultId, result });
    },
  };
  let page = "overview";

  // --- fake processing (Milestone 5): Full Mix + the linked tracks, each with every control ----------
  const P = { spec: null, key: "mix", values: {}, listIn: 0, mono: false, duck: {} };
  import("./procspec.js").then((m) => {
    P.spec = m;
    const fresh = () => m.PROC.map((p) => p.def);
    P.values.mix = fresh();
    chans.forEach((c, i) => { if (c.link) P.values["link:" + i + ":1"] = fresh(); });
    const set = (key, id, v) => { if (P.values[key]) P.values[key][m.PROC_INDEX[id]] = v; };
    const vox = "link:" + chans.findIndex((c) => c.name === "Lead Vox") + ":1";
    set(vox, "eq_on", 1); set(vox, "eq1_type", 3); set(vox, "eq1_freq", 90); set(vox, "eq4_gain", 2.5); set(vox, "eq4_freq", 3200);
    set(vox, "comp_on", 1); set(vox, "comp_thr", -24); set(vox, "tb_on", 1); set(vox, "tb_air", 1.5);
    // An FX channel with a plate reverb that ducks under the vocal; a delay on the Adlibs.
    const verb = "link:" + chans.findIndex((c) => c.name === "FX 1-Vox Verb") + ":1";
    set(verb, "sp_on", 1); set(verb, "sp_type", 0); set(verb, "sp_route", 1); set(verb, "rev_decay", 2.2);
    P.duck[verb] = [chans.find((c) => c.name === "Lead Vox").hash];
    const ad = "link:" + chans.findIndex((c) => c.name === "Adlibs") + ":1";
    set(ad, "sp_on", 1); set(ad, "sp_type", 1); set(ad, "dly_time", 4);
  });
  function procSet(key, changes) {
    const v = P.values[key];
    if (!v || !P.spec) return 0;
    let n = 0;
    for (const [id, val] of Object.entries(changes || {})) {
      const i = P.spec.PROC_INDEX[id];
      if (i === undefined) continue;
      const sp = P.spec.PROC[i];
      v[i] = Math.min(sp.max, Math.max(sp.min, sp.kind === "number" ? val : Math.round(val)));
      n++;
    }
    return n;
  }
  function procFrame(frame, beat) {
    if (!P.spec || !["eq", "dynamics", "tone", "stereo", "space"].includes(page)) return;
    const { PROC, PROC_INDEX, MODULES, MODULE_ON } = P.spec;
    const keyOf = (i) => "link:" + i + ":1";
    const onList = (v) => MODULES.filter((m) => v[PROC_INDEX[MODULE_ON[m]]] > 0.5);
    if (P.listIn-- <= 0) {
      P.listIn = 15;
      const sp = (v) => ({ spaceType: Math.round(v[PROC_INDEX.sp_type]), spaceRoute: Math.round(v[PROC_INDEX.sp_route]) });
      frame.procTargets = [Object.assign({ key: "mix", name: "Full Mix", colour: "", channels: 2, on: onList(P.values.mix), running: onList(P.values.mix) }, sp(P.values.mix))]
        .concat(chans.map((c, i) => c.link ? Object.assign({ key: keyOf(i), name: c.name, colour: c.colour, channels: c.name === "Lead Vox" || c.name === "808" ? 1 : 2,
          on: onList(P.values[keyOf(i)]), running: onList(P.values[keyOf(i)]), hash: c.hash, role: roleOf(c), duck: P.duck[keyOf(i)] || [] }, sp(P.values[keyOf(i)])) : null).filter(Boolean));
    }
    const v = P.values[P.key];
    if (!v) { frame.proc = { key: P.key, ok: false }; return; }
    const g = (id) => v[PROC_INDEX[id]];
    const chainOn = g("chain_on") > 0.5, on = (m) => chainOn && g(MODULE_ON[m]) > 0.5;
    const inL = -14 + 9 * beat + (Math.random() - 0.5) * 2, inR = inL - 0.8;
    const comp = on("comp") ? Math.max(0, (inL - g("comp_thr")) * (1 - 1 / g("comp_ratio"))) : 0;
    const out = inL - comp + (on("comp") ? g("comp_makeup") : 0);
    const limit = on("limit") ? Math.max(0, out + g("lim_in") - g("lim_ceil")) : 0;
    const gate = on("gate") && inL < g("gate_thr") ? g("gate_range") : 0;
    const multi = on("multi") ? [g("multi_low"), g("multi_mid"), g("multi_high")].map((a, i) => +(a / 100 * (4 + 3 * beat) * (i === 0 ? 1.2 : 0.8)).toFixed(1)) : [0, 0, 0];
    const spectrum = P.key === "mix" ? undefined : frame.spectrum.map((x, i) => +(x - 6 + 4 * Math.sin(i / 9 + P.key.length)).toFixed(1));
    frame.proc = {
      key: P.key, ok: true, values: v.slice(), sr: 48000, channels: P.key === "mix" ? 2 : 2, latencyMs: on("limit") ? 5.3 : 0, spectrum,
      corr: P.key === "mix" ? undefined : 0.8,
      meters: { inL: +inL.toFixed(1), inR: +inR.toFixed(1), outL: +(out - limit).toFixed(1), outR: +(out - limit - 0.8).toFixed(1), comp: +comp.toFixed(1), multi,
        trans: on("trans") ? +(g("trans_attack") / 100 * 6 * beat).toFixed(1) : 0, gate, limit: +limit.toFixed(1), clip: 0, glue: 0,
        space: on("space") ? +(-24 + 8 * beat + (Math.random() - 0.5) * 2).toFixed(1) : -100,
        duck: on("space") && (P.duck[P.key] || []).length ? +(g("duck_amount") * (0.5 + 0.5 * Math.sin(t * 1.3)) * 0.7).toFixed(1) : 0,
        running: MODULES.filter((m) => on(m)) },
    };
    frame.mono = P.mono;
  }

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
    if (B.an.state === "listening" && B.an.playing) {
      B.an.seconds += 1 / 30;
      if (Math.floor(B.an.seconds * 5) !== Math.floor((B.an.seconds - 1 / 30) * 5)) B.version++;
      if (B.an.seconds >= 60) { B.an.state = "done"; B.an.report = fakeReport(); B.version++; }
    }
    if (page === "overview" && Math.round(t * 30) % 6 === 0) {
      const groups = { kick: 0, bass: 1, snare: 2, hats: 2, perc: 2, vox: 3, adlibs: 3, keys: 4, synth: 4, guitar: 4, fx: 5, other: 5 };
      const centres = [5, 3.5, 22, 18, 19, 16];
      const curves = [null, null, null, null, null, null];
      chans.filter((c) => c.link && !(B.settings[c.hash] && B.settings[c.hash].excluded)).forEach((c, i) => {
        const gi = groups[roleOf(c)];
        const centre = gi === 2 && c.name === "Hats" ? 27 : centres[gi];
        const level = lawDb(c.volume) - 34 + (gi < 2 ? 6 * beat : 0) + (Math.random() - 0.5);
        const curve = Array.from({ length: 31 }, (_, b) => level - 2.2 * Math.pow((b - centre) / 3.2, 2) + 4.5 * Math.log2(Math.pow(2, (b - 17) / 3)) - 6);
        curves[gi] = curves[gi] ? curves[gi].map((v, b) => 10 * Math.log10(Math.pow(10, v / 10) + Math.pow(10, curve[b] / 10))) : curve;
      });
      frame.roleCurves = curves.map((c) => c && c.map((v) => +v.toFixed(1)));
    }
    procFrame(frame, beat);
    if (page === "space") {   // a song at 92 BPM, playing
      const bpm = 92, ppq = (performance.now() / 60000) * bpm;
      frame.beat = { bpm, ppq, bar: Math.floor(ppq / 4) * 4, num: 4, playing: B.an.playing !== false };
    }
    if (page === "master" && P.spec) {
      const v = P.values.mix, ix = P.spec.PROC_INDEX, on = (id) => v[ix.chain_on] > 0.5 && v[ix[id]] > 0.5;
      frame.master = { finOn: on("fin_on"), glue: v[ix.fin_glue], width: v[ix.fin_width], out: v[ix.fin_out], tbOn: on("tb_on"),
        warmth: v[ix.tb_warmth], air: v[ix.tb_air], glueGr: on("fin_on") ? +(v[ix.fin_glue] / 40 * (0.5 + beat)).toFixed(1) : 0,
        limOn: on("lim_on"), masterology: { goal: "Spotify / Apple Music", genre: "Hip-Hop / Trap", target: -14, ceiling: -1, inLufs: -15.2, inPeak: -5.9 } };
    }
    if (page === "overview" && P.spec) {
      const v = P.values.mix, ix = P.spec.PROC_INDEX;
      frame.mixEq = [1, 2, 3, 4, 5, 6].map((b) => v[ix[`eq${b}_gain`]]).concat([v[ix.eq_on] > 0.5]);
    }
    if (B.version !== sentVersion) {
      sentVersion = B.version;
      frame.bridge = state(B.log.length !== sentLog);
      sentLog = B.log.length;
    }
    window.__JUCE__.backend && window.__JUCE__.backend.emitByBackend("mixFrame", JSON.stringify(frame));
  }, 1000 / 30);
})();
