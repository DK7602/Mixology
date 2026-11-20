// Sonicology Rack: a made-up project for previewing the view in a browser (tools/rack/preview.html; not in the
// plug-ins). Lead Vox with Voxology + a Link, Chorus Vox with Honey Tune + Voxology + a
// Link, Ad-Libs with Voxology alone, the Beat with Voxology in Beat mode + a Link, drums / 808 / keys with Links, the
// Full Mix (Mixology) and the Master (Masterology). Meters move. Stage 2: knobs turn and modules switch (request /
// answer, about a fifth of a second later, as a plug-in would); ?lock=1 locks Masterology (Auto-Master running);
// Masterology's Tonal EQ Air refuses (to see a refusal); ?old=1 adds a plug-in from another
// beta; ?auto=1 has the DAW's automation move a turned knob back; ?order=1 puts Chorus Vox's Link before Voxology;
// ?coach=ready starts Lead Vox with the Coach's plan ready (Beta 26); ?mastercoach=ready the master's (Beta 29).
(function () {
  "use strict";
  const t0 = Date.now();
  const wob = (seed, depth = 1) => {
    const t = (Date.now() - t0) / 1000;
    return depth * (Math.sin(t * 2.1 + seed) * 0.55 + Math.sin(t * 5.3 + seed * 1.7) * 0.3 + Math.sin(t * 11.7 + seed * 0.3) * 0.15);
  };
  const v = (name, text, norm, bi) => ({ name, text, norm, bi: !!bi });
  const MINUS = "\u2212", clamp01 = (x) => Math.max(0, Math.min(1, x));
  const off = (key) => ({ key, state: "off", summary: "Off" });
  const H = { lead: 0x1a2b301, chorus: 0x1a2b302, adlibs: 0x1a2b303, beat: 0x1a2b304, kick: 0x1a2b305, snare: 0x1a2b306, hats: 0x1a2b307, bass: 0x1a2b308, keys: 0x1a2b309 };

  // a vocal-ish spectrum (240 bins, 20 Hz - 20 kHz, dB), spiky like a real one
  const vocalSpectrum = () => Array.from({ length: 360 }, (_, i) => {
    const f = 20 * Math.pow(1000, i / 359);
    const body = -40 + 22 * Math.exp(-Math.pow(Math.log2(f / 250), 2) / 3.2) + 15 * Math.exp(-Math.pow(Math.log2(f / 2500), 2) / 2.2);
    const roll = f < 45 ? -14 * Math.log2(45 / f) : 0;
    const spikes = 8 * Math.abs(Math.sin(i * 12.9898 + 78.233 * Math.sin(i))) - 4;
    return body + roll - (f > 9000 ? 8 * Math.log2(f / 9000) : 0) + spikes + wob(i * 0.7, 2) + 17;
  });

  function voxLead() {
    const gr = -2.6 + wob(1, 1.4), ds = Math.min(0, -1.8 + wob(2, 1.8)), rd = wob(3, 2.2);
    return {
      id: "vox-lead", product: "voxology", version: "0.55.0", track: "Lead Vox", hash: H.lead, role: "vocal", on: true,
      inDb: -16 + wob(4, 4), outDb: -12 + wob(5, 3),
      modules: [
        { key: "vox.pitch", state: "on", summary: "Classic · 20 ms · F minor (from the beat)", values: [v("Mode", "Classic", 0.5), v("Amount", "85 %", 0.85), v("Retune", "20 ms", 0.2), v("Vibrato", "as sung", 0.5), v("Humanize", "20 %", 0.2), v("Formant", "0 st", 0.5, true)] },
        { key: "vox.cleanup", state: "on", summary: "Noise −10 dB · Low cut 90 Hz · Breaths −6 dB", values: [v("Noise", "45 %", 0.45), v("Room", "20 %", 0.2), v("Low Cut", "90 Hz", 0.35), v("Pops", "On", 1), v("Clicks", "30 %", 0.3), v("Breaths", "−6 dB", 0.4), v("Gate", "Off", 0)] },
        { key: "vox.eq", state: "on", summary: "Mud −3 dB · Presence +2.5 dB · Air +2 dB", spectrum: vocalSpectrum(),
          bands: [{ name: "Body", type: "lowShelf", hz: 120, db: 0.5, q: 0.7 }, { name: "Mud", type: "bell", hz: 320, db: -3, q: 1.1 }, { name: "Nasal", type: "bell", hz: 900, db: -0.5, q: 1.2 }, { name: "Presence", type: "bell", hz: 3500, db: 2.5, q: 0.9 }, { name: "Air", type: "highShelf", hz: 10000, db: 2, q: 0.7 }],
          values: [v("Body", "+0.5 dB", 0.53, true), v("Mud", "−3.0 dB", 0.33, true), v("Nasal", "−0.5 dB", 0.47, true), v("Presence", "+2.5 dB", 0.64, true), v("Air", "+2.0 dB", 0.61, true)] },
        { key: "vox.dyneq", state: "on", summary: "Harsh 3.2 kHz cuts up to 4 dB · Smooth 30 %", spectrum: vocalSpectrum(),
          bands: [{ name: "Boom", type: "bell", hz: 180, db: -0.5, q: 1.4 }, { name: "Mud", type: "bell", hz: 420, db: -1.5, q: 1.4 }, { name: "Honk", type: "bell", hz: 1100, db: 0, q: 1.4 }, { name: "Harsh", type: "bell", hz: 3200, db: -4.2 + wob(6, 0.8), q: 2.2 }],
          values: [v("Max Cut", "4.0 dB", 0.4), v("Frequency", "3.2 kHz", 0.62), v("Sensitivity", "55 %", 0.55), v("Smooth", "30 %", 0.3)] },
        { key: "vox.deess", state: "on", summary: "Amount 45 % at 6.8 kHz", meterKind: "gr", meter: ds, values: [v("Amount", "45 %", 0.45), v("Sensitivity", "50 %", 0.5), v("Frequency", "6.8 kHz", 0.7)] },
        { key: "vox.rider", state: "on", summary: "Target −18 dB · Range ±6 dB · Medium", meterKind: "gr", meter: rd, values: [v("Target", "−18 dB", 0.4), v("Range", "±6 dB", 0.5), v("Speed", "Med", 0.5)] },
        { key: "vox.comp", state: "on", summary: "Peak 30 % · 3:1 leveler · Mix 100 %", meterKind: "gr", meter: gr, values: [v("Peak", "30 %", 0.3), v("Threshold", "−24 dB", 0.4), v("Ratio", "3.0:1", 0.35), v("Makeup", "+3.0 dB", 0.6, true), v("Mix", "100 %", 1)] },
        { key: "vox.sat", state: "on", summary: "Tube · Drive 18 % · Mix 60 %", values: [v("Mode", "Tube", 0.5), v("Drive", "18 %", 0.18), v("Mix", "60 %", 0.6)] },
        off("vox.voices"), off("vox.dimension"),
        { key: "vox.delay", state: "on", summary: "1/4 · Mix 10 % · Duck · Throw", values: [v("Time", "1/4", 0.5), v("Feedback", "22 %", 0.22), v("Mix", "10 %", 0.1), v("Tone", "Dark", 0.3), v("Duck", "60 %", 0.6), v("Throw", "On", 1)] },
        { key: "vox.reverb", state: "on", summary: "Plate 1.4 s · Mix 12 % · Duck", values: [v("Decay", "1.4 s", 0.35), v("Pre-delay", "30 ms", 0.3), v("Mix", "12 %", 0.12), v("Tone", "Bright", 0.65), v("Duck", "50 %", 0.5), v("Throw", "Off", 0)] },
        { key: "vox.output", state: "on", summary: "+1.0 dB", values: [v("Output", "+1.0 dB", 0.54, true)] },
      ],
      together: [
        { level: "ok", text: "Mixology treats this track as the lead vocal and sets its level (it moves this fader): −2.1 dB under the style's target, now on target." },
        { level: "ok", text: "Voxology's key comes from the Beat (F minor), so every vocal sings in the same key." },
      ],
    };
  }

  function voxChorus() {
    const base = voxLead();
    return Object.assign(base, {
      id: "vox-chorus", track: "Chorus Vox", hash: H.chorus, inDb: -18 + wob(7, 4), outDb: -14 + wob(8, 3),
      modules: base.modules.map((m) => m.key === "vox.pitch" ? { key: "vox.pitch", state: "rest", summary: "Rests: Honey Tune tunes this track", values: [v("Mode", "Natural", 0), v("Amount", "70 %", 0.7), v("Retune", "50 ms", 0.5)] }
        : m.key === "vox.voices" ? { key: "vox.voices", state: "on", summary: "Double + 3rd up · Width 80 %", values: [v("Double", "On", 1), v("Width", "80 %", 0.8), v("Voice 1", "3rd up", 0.6), v("Voice 2", "Unison", 0.3), v("Level", "−6 dB", 0.45, true)] }
        : m),
      together: [
        { level: "ok", text: "Voxology's Pitch rests while Honey Tune tunes this track: one tuner at a time." },
        { level: "ok", text: "Mixology treats this track as a vocal (a double of Lead Vox: spread left and right)." },
      ],
    });
  }

  function voxAdlibs() {
    const base = voxLead();
    return Object.assign(base, { id: "vox-adlibs", track: "Ad-Libs", hash: H.adlibs, inDb: -20 + wob(9, 6), outDb: -17 + wob(10, 5),
      modules: base.modules.map((m) => m.key === "vox.delay" ? Object.assign({}, m, { summary: "1/8 dotted · Mix 22 % · Ping-Pong" }) : m),
      together: [{ level: "warn", text: "No Mixology Link on this track: Mixology can't hear Ad-Libs or set its level. Insert Mixology Link after Voxology." }] });
  }

  function honey() {
    return { id: "ht-chorus", product: "honeytune", version: "0.55.0", track: "Chorus Vox", hash: H.chorus, role: "vocal", on: true, inDb: -18 + wob(11, 4), outDb: -18 + wob(11, 4),
      modules: [{ key: "ht.tune", state: "on", summary: "14 notes edited · 3 steadied · Auto: F Minor", values: [v("Key", "F", 0.45), v("Scale", "Minor", 0.3), v("Keep Drift", "40 %", 0.4), v("Keep Vibrato", "60 %", 0.6)] }],
      together: [] };
  }

  function voxBeat() {
    return { id: "vox-beat", product: "voxology", version: "0.55.0", track: "Beat", hash: H.beat, role: "beat", on: true, inDb: -9 + wob(12, 3), outDb: -9 + wob(12, 3),
      modules: [{ key: "vox.beat", state: "on", summary: "Hears F minor (sure) · shared with 3 vocals and Honey Tune", values: [v("Key", "F", 0.45), v("Scale", "Minor", 0.3)] }],
      together: [{ level: "ok", text: "Voxology in Beat mode: it listens for the song's key and shares it with your vocals and Honey Tune." },
        { level: "ok", text: "Unmask dips the beat only where the vocal sits, while the vocal sings (Amount 50 %)." }] };
  }

  const linkMods = (on) => {
    const order = ["mix.assist", "mix.gate", "mix.eq", "mix.comp", "mix.multi", "mix.trans", "mix.tone", "mix.loud", "mix.harm", "mix.analog", "mix.clip", "mix.stereo", "mix.final", "mix.space", "mix.limit"];
    return order.map((k) => on[k] ? Object.assign({ key: k, state: "on" }, on[k]) : off(k));
  };
  function link(id, track, hash, role, inDb, on, together) {
    return { id, product: "link", version: "0.26.0", track, hash, role, on: true, inDb: inDb + wob(hash % 97, 3), outDb: inDb + 1 + wob(hash % 89, 3), modules: linkMods(on), together: together || [] };
  }

  function rawUnits() {
    return [
      voxLead(),
      link("link-lead", "Lead Vox", H.lead, "vocal", -12, {
        "mix.assist": { summary: "Low cut 80 Hz", by: "Mixology's Clean Up", bands: [{ type: "lowCut", hz: 80, q: 0.7 }], values: [v("Low Cut", "80 Hz", 0.3), v("Mud", "Off", 0), v("Pocket", "Off", 0)] },
      }, [{ level: "ok", text: "Mixology hears this track through its Link (after Voxology)." }]),
      honey(), voxChorus(),
      link("link-chorus", "Chorus Vox", H.chorus, "vocal", -14, {
        "mix.space": { summary: "Hall 1.8 s · Mix 14 % · ducks under Lead Vox", layers: ["hall"], values: [v("Effect", "Hall", 0.6), v("Decay", "1.8 s", 0.45), v("Mix", "14 %", 0.14), v("Ducking", "−8 dB", 0.5)] },
      }),
      voxAdlibs(),
      voxBeat(),
      link("link-beat", "Beat", H.beat, "beat", -9, {
        "mix.assist": { summary: "Pocket −2.0 dB at 3.2 kHz", by: "Mixology's Vocal Pocket", bands: [{ name: "Pocket", type: "bell", hz: 3200, db: -2, q: 1.4 }], values: [v("Low Cut", "Off", 0), v("Mud", "Off", 0), v("Pocket", "−2.0 dB", 0.35, true)] },
        "mix.stereo": { summary: "Lows in the middle below 120 Hz", values: [v("Width", "100 %", 0.5), v("Low Width", "0 %", 0), v("Mid Width", "100 %", 0.5), v("High Width", "110 %", 0.55)] },
      }),
      link("link-kick", "Kick", H.kick, "track", -10, {
        "mix.eq": { summary: "Low cut 30 Hz · box −3 dB at 400 Hz", by: "Mixology AUTO", bands: [{ type: "lowCut", hz: 30, q: 0.7 }, { name: "Thump", type: "bell", hz: 60, db: 1.5, q: 1 }, { name: "Box", type: "bell", hz: 400, db: -3, q: 1.6 }, { name: "Click", type: "bell", hz: 4000, db: 1.5, q: 1.2 }],
          values: [v("Low Cut", "30 Hz", 0.1), v("Thump", "+1.5 dB", 0.56, true), v("Box", "−3.0 dB", 0.38, true), v("Click", "+1.5 dB", 0.56, true)] },
        "mix.trans": { summary: "Attack +20 % · Sustain −10 %", values: [v("Attack", "+20 %", 0.6, true), v("Sustain", "−10 %", 0.45, true), v("Speed", "Fast", 0.7), v("Mix", "100 %", 1)] },
      }),
      link("link-snare", "Snare", H.snare, "track", -12, {
        "mix.eq": { summary: "Crack +2 dB at 3.5 kHz", bands: [{ type: "lowCut", hz: 70, q: 0.7 }, { name: "Crack", type: "bell", hz: 3500, db: 2, q: 1.2 }], values: [v("Low Cut", "70 Hz", 0.2), v("Crack", "+2.0 dB", 0.58, true)] },
        "mix.trans": { summary: "Attack +15 %", values: [v("Attack", "+15 %", 0.58, true), v("Sustain", "0 %", 0.5, true)] },
        "mix.space": { summary: "Room 0.6 s · Mix 8 %", layers: ["room"], values: [v("Effect", "Room", 0.7), v("Decay", "0.6 s", 0.2), v("Mix", "8 %", 0.08)] },
      }),
      link("link-hats", "Hats", H.hats, "track", -18, {
        "mix.eq": { summary: "Low cut 300 Hz", bands: [{ type: "lowCut", hz: 300, q: 0.7 }], values: [v("Low Cut", "300 Hz", 0.45)] },
        "mix.assist": { summary: "Pocket −1.5 dB at 3.2 kHz", by: "Mixology's Vocal Pocket", bands: [{ name: "Pocket", type: "bell", hz: 3200, db: -1.5, q: 1.4 }], values: [v("Low Cut", "Off", 0), v("Mud", "Off", 0), v("Pocket", "−1.5 dB", 0.4, true)] },
      }),
      link("link-808", "808", H.bass, "track", -8, {
        "mix.comp": { summary: "4:1 · GR 3 dB", meterKind: "gr", meter: -3 + wob(13, 1.5), values: [v("Threshold", "−18 dB", 0.45), v("Ratio", "4.0:1", 0.45), v("Attack", "10 ms", 0.3), v("Release", "120 ms", 0.4), v("Makeup", "+2.0 dB", 0.56, true), v("Knee", "Medium", 0.5)] },
        "mix.stereo": { summary: "Lows in the middle below 120 Hz", values: [v("Width", "100 %", 0.5), v("Low Width", "0 %", 0)] },
      }),
      link("link-keys", "Keys", H.keys, "track", -16, {
        "mix.eq": { summary: "Mud −2.5 dB at 300 Hz", by: "Mixology AUTO", bands: [{ type: "lowCut", hz: 90, q: 0.7 }, { name: "Mud", type: "bell", hz: 300, db: -2.5, q: 1.3 }], values: [v("Low Cut", "90 Hz", 0.3), v("Mud", "−2.5 dB", 0.4, true)] },
        "mix.comp": { summary: "2:1 · GR 1.5 dB", meterKind: "gr", meter: -1.5 + wob(14, 0.8), values: [v("Threshold", "−22 dB", 0.4), v("Ratio", "2.0:1", 0.2), v("Attack", "20 ms", 0.4), v("Release", "200 ms", 0.5)] },
        "mix.tone": { summary: "Air +1.5 dB", values: [v("Warmth", "0.0 dB", 0.5, true), v("Presence", "0.0 dB", 0.5, true), v("Air", "+1.5 dB", 0.62, true)] },
        "mix.stereo": { summary: "Width 120 %", values: [v("Width", "120 %", 0.6)] },
        "mix.space": { summary: "Hall + Chorus · ducks under the vocals", layers: ["chorus", "hall"], values: [v("Effects", "Chorus + Hall", 0.5), v("Hall Mix", "16 %", 0.16), v("Chorus Mix", "25 %", 0.25), v("Ducking", "−6 dB", 0.4)] },
      }),
      { id: "mixology", product: "mixology", version: "0.26.0", track: "Stereo Out", hash: 0, role: "fullmix", on: true, inDb: -4.8 + wob(15, 1.5), outDb: -8.3 + wob(15, 1.5),
        modules: linkMods({
          "mix.tone": { summary: "Warmth +0.5 dB · Air +1.0 dB", values: [v("Warmth", "+0.5 dB", 0.54, true), v("Presence", "0.0 dB", 0.5, true), v("Air", "+1.0 dB", 0.58, true), v("Glue", "10 %", 0.1), v("Punch", "0 %", 0)] },
          "mix.final": { summary: "Output −3.5 dB: room made for Masterology", by: "Make Room", values: [v("Width", "100 %", 0.5), v("Air", "0 %", 0), v("Glue", "0 %", 0), v("Output", "−3.5 dB", 0.45, true)] },
        }).filter((m) => m.key !== "mix.assist"),
        together: [{ level: "ok", text: "Mixology hands the mix to Masterology: style Hip Hop - Trap, peaks −5.8 dBTP, tone against the style." }] },
      { id: "masterology", product: "masterology", version: "0.31.0", track: "Stereo Out", hash: 0, role: "master", on: true, inDb: -8.3 + wob(16, 1.5), outDb: -1.2 + wob(16, 0.3),
        modules: [
          { key: "mas.unlimiter", state: "off", summary: "Neutral" },
          { key: "mas.tonaleq", state: "on", summary: "Low End +1.0 · Upper Mid −0.5 · Air +1.5 dB",
            bands: [{ name: "Low End", type: "lowShelf", hz: 45, db: 1, q: 0.7 }, { name: "Bass", type: "bell", hz: 100, db: 0, q: 0.8 }, { name: "Low Mid", type: "bell", hz: 350, db: 0, q: 0.9 }, { name: "Mid", type: "bell", hz: 1000, db: 0, q: 0.8 }, { name: "Upper Mid", type: "bell", hz: 2500, db: -0.5, q: 0.9 }, { name: "Presence", type: "bell", hz: 5000, db: 0, q: 0.9 }, { name: "Air", type: "highShelf", hz: 12000, db: 1.5, q: 0.7 }],
            values: [v("Low End", "+1.0", 0.56, true), v("Bass", "0.0", 0.5, true), v("Low Mid", "0.0", 0.5, true), v("Mid", "0.0", 0.5, true), v("Upper Mid", "−0.5", 0.47, true), v("Presence", "0.0", 0.5, true), v("Air", "+1.5", 0.58, true)] },
          { key: "mas.stemeq", state: "on", summary: "Vocals +1.0 dB", values: [v("Vocals", "+1.0 dB", 0.56, true), v("Drums", "0.0 dB", 0.5, true), v("Bass", "0.0 dB", 0.5, true), v("Other", "0.0 dB", 0.5, true)] },
          { key: "mas.steady", state: "on", summary: "Amount 40 %", values: [v("Amount", "40 %", 0.4), v("Range", "3 dB", 0.4), v("Speed", "Medium", 0.5)] },
          { key: "mas.resonance", state: "on", summary: "Amount 30 %", values: [v("Amount", "30 %", 0.3), v("Sensitivity", "50 %", 0.5), v("Release", "Med", 0.5), v("Focus", "Highs", 0.7)] },
          { key: "mas.bass", state: "on", summary: "Mono below 120 Hz · Punch 20 %", values: [v("Mono Below", "120 Hz", 0.4), v("Sub Tame", "10 %", 0.1), v("Punch", "20 %", 0.2)] },
          { key: "mas.glue", state: "on", summary: "2:1 · GR 1.5 dB", meterKind: "gr", meter: -1.5 + wob(17, 0.7), values: [v("Character", "Clean", 0.3), v("Threshold", "−14 dB", 0.5), v("Ratio", "2.0:1", 0.2), v("Attack", "30 ms", 0.4), v("Release", "Auto", 0.5), v("Mix", "100 %", 1)] },
          { key: "mas.dynamics", state: "on", summary: "4 bands · GR 1 - 2 dB", meterKind: "gr", meter: -1.2 + wob(18, 0.6), values: [["Low", 21, 1.5], ["Lo-Mid", 22, 1.0], ["Hi-Mid", 23, 0.5], ["High", 24, 1.0]].map(([n, seed, d]) => {
            // as Masterology: each band's gain reduction now, a reading (moves all the time while the song plays)
            const g = Math.min(0, -d + wob(seed, 0.6));
            return v(n, (g < -0.05 ? MINUS + Math.abs(g).toFixed(1) : "0.0") + " dB", clamp01(-g / 12));
          }) },
          { key: "mas.imager", state: "on", summary: "Highs 115 %", values: [v("Lows", "0 %", 0), v("Mids", "100 %", 0.5), v("Highs", "115 %", 0.58)] },
          { key: "mas.sat", state: "off", summary: "Off" },
          { key: "mas.clipper", state: "on", summary: "Clip level −1.0 dB", meterKind: "gr", meter: Math.min(0, -0.6 + wob(19, 0.9)), values: [v("Clip Level", "−1.0 dB", 0.6), v("Softness", "50 %", 0.5), v("Oversampling", "4x", 0.66)] },
          { key: "mas.max", state: "on", summary: "Ceiling −1.0 dBTP · GR 3.2 dB · Streaming −14", meterKind: "gr", meter: -3.2 + wob(20, 1.2), values: [v("Drive", "+4.5 dB", 0.19), v("Input Trim", "0.0 dB", 0.5, true), v("Ceiling", "−1.0 dBTP", 0.66), v("Release", "120 ms", 0.4), v("Bass First", "30 %", 0.3)] },
        ],
        together: [{ level: "ok", text: "Masterology waits while Mixology changes the mix, then hears it again from the start." }] },
    ];
  }

  // Stage 2: every value a knob the Rack can turn (key from its name), choices stepped; what was turned or switched.
  const CHOICES = { mode: 3, speed: 3, time: 6, character: 3, focus: 3, oversampling: 4, effect: 7, effects: 7, knee: 3 };
  const SHOWN_ONLY = new Set(["ht.tune|key", "ht.tune|scale", "vox.beat|key", "vox.beat|scale",
    "mas.dynamics|low", "mas.dynamics|lomid", "mas.dynamics|himid", "mas.dynamics|high"]);   // (readings, as in Masterology)
  const FIXED = new Set(["vox.output", "vox.beat", "ht.tune", "mix.assist", "mas.max"]);
  const turned = {}, switched = {}, powered = {}, tickets = {}, saved = [];
  let nextTicket = 1;
  const lockMaster = new URLSearchParams(location.search).get("lock") === "1";
  const automation = new URLSearchParams(location.search).get("auto") === "1";   // ?auto=1: the DAW's automation moves knobs back
  const oldBeta = new URLSearchParams(location.search).get("old") === "1";   // ?old=1: a plug-in from another beta is in the project
  const orderTest = new URLSearchParams(location.search).get("order") === "1";   // ?order=1: Chorus Vox's Link sits before Voxology

  // Beta 26: the Coach (Voxology's Auto-Edit plan): ANALYZE & BUILD listens ~4 s, builds ~1.5 s, then the plan below.
  // ?coach=ready: Lead Vox starts with its plan ready.
  const COACH_STYLES = ["Trap Lead", "Rap", "Melodic", "Robot", "R&B", "Pop", "Folk", "Natural Singer"];
  const COACH_INTS = ["Light", "Balanced", "Strong"];
  const coach = {};
  if (new URLSearchParams(location.search).get("coach") === "ready") coach["vox-lead"] = { state: "ready", t0: 0, canUndo: false };
  const HZ = [100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000];
  const AIM = [-9, -7, -5, -3.5, -2.5, -2, -1.5, -0.8, -0.2, 0, 0.3, 0.6, 0.8, 1.2, 1.8, 2.2, 2.0, 1.0, -0.5, -2.0, -3.5, -5.5, -9];
  const BEFORE = AIM.map((v, i) => +(v + (HZ[i] >= 200 && HZ[i] <= 400 ? 3.4 : 0) + (HZ[i] >= 2000 && HZ[i] <= 5000 ? 2.9 : 0) + (HZ[i] >= 10000 ? -4.1 : 0) + (HZ[i] <= 160 ? 1.2 : 0) + Math.sin(i * 1.7) * 0.5).toFixed(1));
  const AFTER = AIM.map((v, i) => +(v + Math.sin(i * 2.3) * 0.6 + (HZ[i] >= 200 && HZ[i] <= 400 ? 0.8 : 0) + (HZ[i] >= 10000 ? -1.1 : 0)).toFixed(1));
  const step = (key, caption, set, on = true) => ({ key, on, caption, kept: false, set: set.map(([control, value, why]) => ({ control, value, why })) });
  function demoPlan(style, intensity) {
    const name = COACH_STYLES[style];
    return { ok: true, style: name, intensity: COACH_INTS[intensity], reference: "", aim: name + "'s balance",
      summary: `Listened to 12.4 s of voice (${name}, ${COACH_INTS[intensity]}). Your vocal came in at −21.3 LUFS with peaks at −6.2 dB. The plan is set for a ${name} sound: upfront, bright and controlled, with a short wide space around it.`,
      heard: ["Sung: 58 % of the notes are held", "Comes in around −21.3 LUFS, its loudest peak at −6.2 dB", "Between phrases: −61.5 dB of room and noise",
        "The \"s\" sounds peak +0.2 dB against the voice (the aim: −4.0 dB)", "Loud and quiet lines 11.2 dB apart"],
      issues: [
        { key: "mud", name: "Mud", from: 200, to: 400, db: 3.4, warn: true, text: "200 - 400 Hz sits 3.4 dB over " + name + "'s balance" },
        { key: "harsh", name: "Harshness", from: 2000, to: 5000, db: 2.9, warn: true, text: "2 - 5 kHz sits 2.9 dB over " + name + "'s balance" },
        { key: "sibilance", name: "Sibilance", from: 5000, to: 9800, db: 4.2, warn: true, text: "the \"s\" sounds peak +0.2 dB against the voice; the aim is −4.0 dB" },
        { key: "air", name: "Lack of air", from: 10000, to: 16000, db: -4.1, warn: false, text: "10 - 16 kHz sits 4.1 dB under " + name + "'s balance" },
        { key: "swing", name: "Uneven level", from: 0, to: 0, db: 11.2, warn: false, text: "loud and quiet lines are 11.2 dB apart" }],
      chain: [
        step("vox.pitch", "Tune & align", [["Mode", "Classic", "Your take is sung and mostly in key (F minor from the beat): Classic at 25 ms tightens it without the robot sound."], ["Key", "F minor", "Pitch follows the beat's key."]]),
        step("vox.cleanup", "Clean the take", [["Low Cut", "90 Hz", "Rumble under your lowest notes (110 Hz) goes; the voice keeps its weight."], ["Noise", "−8.0 dB", "Steady hiss 40 dB under your voice: a gentle cut keeps it natural."]]),
        step("vox.eq", "Shape tone", [["Mud", "−3.0 dB at 300 Hz", "Your vocal has 3.4 dB more 200 - 400 Hz than a finished " + name + " vocal: clears the boxiness."], ["Air", "+3.5 dB at 12 kHz", "4.1 dB less top than the aim: opens it up."]]),
        step("vox.dyneq", "Tame harsh spots", [["Harsh", "−4.5 dB at 3.2 kHz", "The 2 - 5 kHz zone jumps out on loud words only: cut there, only then."]]),
        step("vox.deess", "Tame sibilance", [["Amount", "58 % at 6.8 kHz", "Your loudest \"s\" sit 4.2 dB over the aim: the De-Esser brings them to it."]]),
        step("vox.rider", "Even levels", [["Range", "±4.0 dB, Medium", "Loud and quiet lines are 11.2 dB apart: the rider evens them word by word."]]),
        step("vox.comp", "Add control", [["Style", "FET", "Grabs the front of each word: upfront, as " + name + " wants."], ["Peak", "−10.5 dB", "Catches the sudden loud syllables."]]),
        step("vox.sat", "Add character", [["Mode", "Tube", "Warmth and a rich forward low-mid."], ["Air", "20 %", "A touch of sparkle above 8 kHz."]]),
        step("vox.voices", "Add width", [["Double", "30 %, Width 80 %", "Two drifting copies left and right for a bigger hook."]]),
        step("vox.dimension", "Movement", [], false),
        step("vox.delay", "Add depth", [["Time", "1/4 at 140 BPM", "Echoes on the beat fill the gaps between lines; Duck keeps them out of the words."]]),
        step("vox.reverb", "Add space", [["Type", "Plate", "Smooth and bright, the classic vocal sound."], ["Mix", "12 %", "A short, wide space around it."]]),
        step("vox.output", "Polish & finish", [["Output", "+0.5 dB", "Matches the loudness it came in at, so it doesn't jump in your mix."]])],
      changes: 31, modulesOn: 12,
      bands: { hz: HZ, before: BEFORE, after: AFTER, target: AIM },
      results: [
        { key: "sibilance", name: "Sibilance (the \"s\" sounds)", before: 4.2, after: 0.2, unit: "dB vs the aim", better: true },
        { key: "swing", name: "Level swing (loud vs quiet lines)", before: 11.2, after: 6.3, unit: "dB apart", better: true },
        { key: "mud", name: "Mud (200 - 400 Hz)", before: 3.4, after: 0.8, unit: "dB vs the aim", better: true },
        { key: "air", name: "Lack of air (10 - 16 kHz)", before: -4.1, after: -1.1, unit: "dB vs the aim", better: true },
        { key: "harsh", name: "Harshness (2 - 5 kHz)", before: 2.9, after: 0.7, unit: "dB vs the aim", better: true }],
      first: ["Compare with A / B and MATCH on: MATCH plays both at the same loudness, so you judge the tone, not the volume.",
        "Tone EQ: the 200 - 400 Hz cut is the biggest move; take it back a little if the voice gets thin.",
        "Reverb and Delay: set them to taste for the space you want on this song."] };
  }
  function coachReport(u) {
    const c = coach[u.id] || { state: "idle" };
    const t = (Date.now() - (c.t0 || 0)) / 1000;
    if (c.state === "listening" && t > 4) c.state = "working";
    if (c.state === "working" && t > 5.5) c.state = "ready";
    const sty = turned[u.id + "|coach|aeStyle"], int = turned[u.id + "|coach|aeIntensity"];
    const style = sty != null ? Math.round(sty * 7) : 0, intensity = int != null ? Math.round(int * 2) : 1;
    const r = { state: c.state, progress: c.state === "listening" ? Math.min(100, Math.round(t * 25)) : 0, hearing: c.state === "listening" && t > 0.4,
      style, intensity, styles: COACH_STYLES, intensities: COACH_INTS, reference: "", canUndo: !!c.canUndo, beat: false, honey: u.id === "vox-chorus" };
    if (c.state === "ready" || c.state === "applied") r.plan = c.wasApplied ? upToDate(demoPlan(style, intensity), "Voxology is already set the way I'd set it for this take: nothing to change. ") : demoPlan(style, intensity);
    if (c.state === "ready" && c.gap == null) c.gap = 2.3;
    return previewFields(c, r);
  }
  // Beta 32: PREVIEW (the plan plays, flip with yours at the same loudness, KEEP or BACK). `gap`: the plan minus yours.
  function previewStep(c, step, gap, refuse) {
    if (step === "preview") { if (c.state !== "ready") return refuse("There's no plan to hear yet: press ANALYZE & BUILD."); c.preview = "plan"; c.gap = gap; return true; }
    if (step === "flip") { if (!c.preview) return refuse("PREVIEW isn't on: press PREVIEW first."); c.preview = c.preview === "plan" ? "yours" : "plan"; return true; }
    if (step === "back") { c.preview = null; return true; }
    if (step === "keep") { if (!c.preview) return refuse("PREVIEW isn't on: press PREVIEW first."); c.preview = null; c.state = "applied"; c.canUndo = true; return true; }
    if (step === "plan" || step === "apply" || step === "undo" || step === "discard" || step === "cancel") c.preview = null;
    return false;
  }
  function previewFields(c, r) {
    if (c.preview) { r.preview = c.preview; r.previewGainDb = c.preview === "plan" ? -Math.max(0, c.gap) : -Math.max(0, -c.gap); }
    if (c.state === "ready") r.planGapDb = c.gap != null ? c.gap : null;
    return r;
  }
  // Beta 30: ANALYZE AGAIN after APPLY: the same take, the same plan, already set (what stands out measured through it).
  function upToDate(p, lead) {
    return Object.assign({}, p, { summary: lead + p.summary, upToDate: true, changes: 0, beforeNow: true,
      issues: (p.issues || []).filter((i) => !i.warn), chain: (p.chain || []).map((m) => Object.assign({}, m, { same: true })),
      bands: p.bands && p.bands.after ? Object.assign({}, p.bands, { before: p.bands.after }) : p.bands,
      results: (p.results || []).map((x) => Object.assign({}, x, { before: x.after, better: false })) });
  }
  // Beta 28: Mixology's Coach on its tracks (Keys, Beat, 808...): the Mixology on the master builds it and sends the report for
  // the track the Rack shows ({"track": <hash>, ...}). Keys was heard by Analyze Mix (plans at once); Kick wasn't (it listens
  // 3 s first). ?mixcoach=ready starts Keys with its plan ready.
  let shownTrack = "";
  const mixCoach = {};
  if (new URLSearchParams(location.search).get("mixcoach") === "ready") mixCoach[H.keys] = { state: "ready", t0: 0, canUndo: false };
  const MHZ = [39, 50, 62, 79, 99, 125, 157, 198, 250, 315, 397, 500, 630, 794, 1000, 1260, 1587, 2000, 2520, 3175, 4000, 5040, 6350, 8000, 10079, 12699, 16000];
  const MBEFORE = MHZ.map((f, i) => +(-24 * Math.max(0, Math.log2(90 / f)) + 6.5 * Math.exp(-Math.pow(Math.log2(f / 315), 2) / 0.5) - 5 * Math.max(0, Math.log2(f / 4000)) + Math.sin(i * 1.3) * 0.6).toFixed(1));
  const MAFTER = MBEFORE.map((v, i) => +(v - 3.0 * Math.exp(-Math.pow(Math.log2(MHZ[i] / 315), 2) / 0.35) + 3.0 * Math.min(1, Math.max(0, Math.log2(MHZ[i] / 5000)))).toFixed(1));
  function mixPlan(name, role) {
    return { ok: true, kind: "mix", role, style: "Hip Hop - Trap", aim: "Hip Hop - Trap",
      summary: `Cut mud at 315 Hz (-3.0 dB), opened the top (+3.0 dB air), gave it a short plate (12 % mix) and opened it a little wider (140 %): '${name}' sounded muddy, dull and narrow and was dry for Hip Hop - Trap`,
      heard: [`Heard as ${role} (from its name)`, "Played 48 s while Mixology listened", "Loud and quiet parts 4.0 dB apart", "Stereo: 4 % on the sides",
        "At 1.6 - 4 kHz it sits −3.1 dB against the lead vocal (it covers its words)"],
      issues: [
        { key: "mud", name: "Mud", from: 250, to: 500, warn: true, text: "250 - 500 Hz is 7.9 dB over its mids: heavy for the keys in Hip Hop - Trap: a dip there clears the mud" },
        { key: "dull", name: "Dull top", from: 8000, to: 16000, warn: false, text: "its top end (8 - 12 kHz) is 9.2 dB under its mids" },
        { key: "dry", name: "Dry", from: 0, to: 0, warn: false, text: "a short plate (1.5 s, mix 12 %) puts the keys in the same space as the song" },
        { key: "narrow", name: "Narrow", from: 0, to: 0, warn: false, text: "it is stereo but narrow (4 % on the sides): a little wider gives the vocal more room in the middle" }],
      chain: [
        { key: "mix.eq", name: "EQ", on: true, was: false, caption: "Shape tone", kept: false, set: [
          { control: "-3.0 dB at 315 Hz", from: "Flat", value: "", why: "250 - 500 Hz is 7.9 dB over its mids: heavy for the keys in Hip Hop - Trap: a dip there clears the mud" },
          { control: "+3.0 dB at 10 kHz (shelf)", from: "Flat", value: "", why: "its top end (8 - 12 kHz) is 9.2 dB under its mids" }] },
        { key: "mix.comp", name: "Compressor", on: true, was: true, caption: "Even levels", kept: true, set: [] },
        { key: "mix.stereo", name: "Width", on: true, was: false, caption: "Width", kept: false, set: [
          { control: "Width 140 %", from: "Width 100 %", value: "", why: "it is stereo but narrow (4 % on the sides): a little wider gives the vocal more room in the middle" }] },
        { key: "mix.space", name: "Space", on: true, was: false, caption: "Add space", kept: false, set: [
          { control: "Plate 1.5 s, mix 12 %", from: "Dry", value: "", why: "a short plate (1.5 s, mix 12 %) puts the keys in the same space as the song" }] }],
      changes: 4, modulesOn: 4,
      bands: { hz: MHZ, before: MBEFORE, after: MAFTER },
      results: [
        { key: "mud", name: "Mud (250 - 500 Hz)", before: 7.9, after: 5.8, unit: "dB vs its mids", better: true },
        { key: "top", name: "Top (8 - 13 kHz)", before: -9.2, after: -6.3, unit: "dB vs its mids", better: true }],
      moves: [{ module: "EQ", from: "Flat", to: "-3.0 dB at 315 Hz" }, { module: "EQ", from: "Flat", to: "+3.0 dB at 10 kHz (shelf)" },
        { module: "Space", from: "Dry", to: "Plate 1.5 s, mix 12 %" }, { module: "Width", from: "Width 100 %", to: "Width 140 %" }],
      first: [`If another plug-in already gives ${name} its reverb, leave Mixology's Space off: one space is enough.`] };
  }
  const ROLE_OF = { [H.keys]: "Keys", [H.beat]: "Beat", [H.bass]: "Bass / 808", [H.kick]: "Kick", [H.snare]: "Snare", [H.hats]: "Hats" };
  function mixCoachReport(hash, name) {
    const c = mixCoach[hash] || { state: "idle" };
    const heardEnough = hash !== H.kick || c.heard;
    const t = (Date.now() - (c.t0 || 0)) / 1000;
    if (c.state === "listening" && t > 3) { c.state = "ready"; c.heard = true; }
    const r = { track: hash, kind: "mix", state: c.state, progress: c.state === "listening" ? Math.min(100, Math.round(t * 34)) : 0, hearing: c.state === "listening" && t > 0.4,
      need: 8, style: "Hip Hop - Trap", role: ROLE_OF[hash] || "Other", heardSeconds: heardEnough ? 48 : 0, heardEnough: !!heardEnough, analyzed: true, canUndo: !!c.canUndo };
    if (c.state === "ready" || c.state === "applied") r.plan = mixPlan(name, r.role);
    if (c.state === "ready" && c.gap == null) c.gap = -1.4;
    return previewFields(c, r);
  }

  // Beta 29: Masterology's Coach on the master (Auto-Master's plan). Listens fast here: BUILD NOW after "12 s" (3 s), the
  // song "ends" at 40 s (10 s), builds 1.5 s. The plan is a real one (polisher tests: "Coach (the master): print a plan").
  // ?mastercoach=ready starts with its plan ready.
  const MASTER_GENRES = ["Pop", "Trap / Hip-Hop", "R&B", "Rock", "EDM", "Acoustic"];
  const MASTER_INTS = ["Gentle", "Balanced", "Punchy"];
  const MASTER_TARGETS = ["Streaming", "Loud release", "Club"];
  const MASTER_PLAN = {"ok": true, "kind": "master", "summary": "Listened to 12 s of your mix. Your mix measured −30.1 LUFS with peaks at −16.5 dBTP. Settings: Trap / Hip-Hop, Balanced, Loud −9. The master now measures −9.0 LUFS with true peak −1.5 dBTP. The full song will read about 1 LU lower than this loud section, which is normal.", "genre": "Trap / Hip-Hop", "intensity": "Balanced", "target": "Loud release", "targetLufs": -9.0, "aim": "Trap / Hip-Hop", "reference": "", "heard": ["Listened to 2:45 of the song (the whole song)", "The song: −19.5 LUFS, loudness range 6.0 LU", "Its loudest 12 s: −30.1 LUFS, peaks −16.5 dBTP", "Punch (peak to loudness): 13.6 dB", "Bass below 150 Hz: 18 % stereo", "Stereo: correlation 0.70, sides of the mids 0 dB"], "issues": [{"key": "lowend", "name": "Too much low end", "from": 20, "to": 65, "warn": true, "text": "Low end (20 - 65 Hz) sits +4.8 dB against Trap / Hip-Hop"}, {"key": "lowmid", "name": "Not enough low mids", "from": 180, "to": 600, "warn": true, "text": "Low mids (180 - 600 Hz) sits −4.3 dB against Trap / Hip-Hop"}, {"key": "air", "name": "Too much air", "from": 8000, "to": 20000, "warn": true, "text": "Air (8.0k - 20k Hz) sits +10.2 dB against Trap / Hip-Hop"}, {"key": "presence", "name": "Too much presence", "from": 3500, "to": 8000, "warn": false, "text": "Presence (3.5k - 8.0k Hz) sits +3.9 dB against Trap / Hip-Hop"}, {"key": "quiet", "name": "Quieter than the target", "from": 0, "to": 0, "warn": false, "text": "its loudest part comes in at −30.1 LUFS; the target is −9.0 LUFS"}], "chain": [{"key": "mas.unlimiter", "name": "Unlimiter", "on": false, "caption": "Restore peaks", "kept": false, "set": [{"control": "Amount", "value": "0 %", "why": "Your mix still has its punch (Squash 14.3 dB, like an unmastered mix: its peaks stand well above the average…"}]}, {"key": "mas.tonaleq", "name": "Tonal EQ", "on": true, "caption": "Balance tone", "kept": false, "set": [{"control": "Low End", "value": "−1.6 dB", "why": "Your mix has about 4.8 dB more sub (below 65 Hz) than typical Trap / Hip-Hop masters, so a 1.6 dB cut closes…"}, {"control": "Bass", "value": "−0.7 dB", "why": "Your mix has about 2.4 dB more bass (65–180 Hz) than typical Trap / Hip-Hop masters, so a 0.7 dB cut closes…"}]}, {"key": "mas.stemeq", "name": "Stem EQ", "on": false, "caption": "Balance stems", "kept": false, "set": [{"control": "Vocals", "value": "try +3.0 dB", "why": "The model hears your vocal at −13.9 dB against the whole mix; in Trap / Hip-Hop masters it usually sits…"}]}, {"key": "mas.steady", "name": "Tone Steady", "on": false, "caption": "Hold the tone", "kept": false, "set": [{"control": "Amount", "value": "0 %", "why": "Your tone stays consistent through what I heard (at full strength Tone Steady would move only 0.09 dB on…"}]}, {"key": "mas.resonance", "name": "Resonance Tamer", "on": false, "caption": "Tame harsh spots", "kept": false, "set": [{"control": "Amount", "value": "0 %", "why": "I hardly hear any tones ringing out of your mix (at full strength the tamer would cut only 0.00 dB on average,…"}]}, {"key": "mas.bass", "name": "Bass Control", "on": true, "caption": "Tighten low end", "kept": false, "set": [{"control": "Mono Below", "value": "100 Hz", "why": "Your bass is 18 % stereo below 150 Hz. Club systems and phones play bass in mono, so stereo bass partly…"}, {"control": "Punch", "value": "25 %", "why": "Your low end is fairly sustained, so kicks can get lost under it. Punch adds thump to the start of each kick…"}]}, {"key": "mas.dynamics", "name": "Dynamics", "on": true, "caption": "Control bands", "kept": false, "set": [{"control": "Low", "value": "2.0:1 at −29.6 dB", "why": "Holds the kick and 808 down by up to about 1.5 dB on the loudest moments, and gives back 0.7 dB so the band's…"}, {"control": "Low Mid", "value": "2.0:1 at −34.6 dB", "why": "Holds the body, snare and vocal warmth down by up to about 1.5 dB on the loudest moments, and gives back 0.4…"}]}, {"key": "mas.imager", "name": "Stereo Imager", "on": true, "caption": "Width", "kept": false, "set": [{"control": "Lows", "value": "100 %", "why": "The lows stay as they are: wide bass costs punch and cancels on mono speakers (Mono Below in Bass Control…"}, {"control": "Mids", "value": "92 %", "why": "Your mids are already very wide (stereo part only 0.4 dB below the center), so they're pulled in a little to…"}]}, {"key": "mas.sat", "name": "Saturation", "on": true, "caption": "Add warmth", "kept": false, "set": [{"control": "Mode", "value": "Tube", "why": "Tube leans on the 2nd harmonic: the 808 sounds fuller and richer, which Trap / Hip-Hop needs."}, {"control": "Focus", "value": "Bass only", "why": "Harmonics go on the 808 and kick only, so the 808 shows up on phone and laptop speakers (they can't play its…"}]}, {"key": "mas.clipper", "name": "Soft Clipper", "on": true, "caption": "Catch peaks", "kept": false, "set": [{"control": "Soft Clipper", "value": "On", "why": "Your mix has 13.6 dB between its peaks and its average loudness. The clipper shaves about 4.1 dB off the drum…"}, {"control": "Clip Level", "value": "+2.7 dB", "why": "Clipping starts 2.7 dB above the ceiling, so only the biggest peaks are touched: about as much clipping as…"}]}, {"key": "mas.max", "name": "Maximizer", "on": true, "caption": "Loudness", "kept": false, "set": [{"control": "Drive", "value": "+23.9 dB", "why": "+23.9 dB of Drive lands this part at −9.0 LUFS, your Loud −9 target. Holding the peaks down costs about…"}, {"control": "Ceiling", "value": "−1.5 dBTP", "why": "Streaming services convert your master to MP3/AAC/Ogg. Masters this loud peak higher after that conversion, so…"}]}], "changes": 97, "modulesOn": 7, "bands": {"hz": [36, 108, 329, 980, 2366, 5292, 12649], "before": [-0.1, 4.5, -3.7, -2.1, -0.2, 1.5, 3.3], "target": [-4.9, 2.1, 0.6, 0.1, -0.4, -2.4, -6.9], "after": [-1.4, 2.9, -1.9, -1.4, -0.2, 0.7, 2.1]}, "results": [{"name": "Loudness (loudest 12 s)", "before": -30.1, "after": -9.0, "unit": "LUFS", "better": true}, {"name": "True peak", "before": -16.5, "after": -1.5, "unit": "dBTP", "better": true}, {"name": "Punch (peak to loudness)", "before": 13.6, "after": 7.5, "unit": "dB", "better": true}, {"name": "Bass stereo (below 150 Hz)", "before": 18, "after": 1, "unit": "%", "better": true}, {"name": "Low end vs the aim", "before": 4.8, "after": 3.5, "unit": "dB", "better": true}, {"name": "Bass vs the aim", "before": 2.4, "after": 0.8, "unit": "dB", "better": true}, {"name": "Low mids vs the aim", "before": -4.3, "after": -2.5, "unit": "dB", "better": true}, {"name": "Mids vs the aim", "before": -2.2, "after": -1.5, "unit": "dB", "better": true}], "moves": [], "first": ["FIX IT IN THE MIX: your mix is 10.2 dB higher in the air (above 8 kHz) than typical Trap / Hip-Hop masters.…", "Compare with A / B and MATCH on: MATCH plays both at the same loudness, so you judge the master, not the…"]};
  const masterCoach = { state: new URLSearchParams(location.search).get("mastercoach") === "ready" ? "ready" : "idle", t0: 0, canUndo: false };
  function masterCoachReport() {
    const c = masterCoach;
    const t = (Date.now() - (c.t0 || 0)) / 1000;
    if (c.state === "listening" && t * 4 >= 40) { c.state = "working"; c.t1 = Date.now(); }
    if (c.state === "working" && (Date.now() - c.t1) / 1000 > 1.5) c.state = "ready";
    const g = turned["masterology|coach|amGenre"], i = turned["masterology|coach|amIntensity"], tg = turned["masterology|coach|amTarget"];
    const heard = c.state === "listening" ? Math.floor(t * 4) : 0;
    const r = { kind: "master", state: c.state, progress: c.state === "listening" ? Math.min(100, Math.round((t * 4 / 12) * 100)) : 0, heardSeconds: heard,
      canFinish: c.state === "listening" && heard >= 12, hearing: c.state === "listening" && t > 0.4,
      genre: g != null ? Math.round(g * 5) : 1, genres: MASTER_GENRES, intensity: i != null ? Math.round(i * 2) : 1, intensities: MASTER_INTS,
      target: tg != null ? Math.round(tg * 2) : 1, targets: MASTER_TARGETS, reference: "", canUndo: !!c.canUndo };
    if (c.state === "ready" || c.state === "applied") r.plan = c.wasApplied ? upToDate(MASTER_PLAN, "Masterology is already set the way I'd master this mix: nothing to change. ") : MASTER_PLAN;
    if (c.state === "ready" && c.gap == null) c.gap = 0.8;   // (Masterology matches the master after its Maximizer: a small gap)
    return previewFields(c, r);
  }

  // Beta 31: Mixology's WHAT TO FIX FIRST (BridgeHub::fixReport): the setup check, then the Mix Assistant's points.
  const fixMode = new URLSearchParams(location.search).get("fix");
  // Beta 35 (GUIDED MIX): Analyze Mix started from the Rack listens fast here ("20 s" in 4 s), finishes at "60 s".
  const mixListen = { state: fixMode === "none" ? "idle" : "done", t0: 0, heard: 48 };
  const listenNow = () => {
    if (mixListen.state !== "listening") return 0;
    const h = Math.floor((Date.now() - mixListen.t0) / 1000 * 5);
    if (h >= 60) { mixListen.state = "done"; mixListen.heard = 60; }
    return Math.min(h, 60);
  };
  const FIX_REPORT = () => (listenNow(), mixListen.state !== "done") ? { kind: "fix", analyzed: false, busy: mixListen.state === "listening", seconds: 0, hearing: listenNow(), enough: 20, playing: true,
      style: "Hip Hop - Trap", items: [{ id: "setup-vocal-link", from: "setup", kind: "warn", title: "Ad-Libs has Voxology but no Mixology Link", need: "Add Mixology Link under Voxology, so Mixology hears the finished ad-libs.", text: "", tracks: ["Ad-Libs"] }], more: 0 }
    : { kind: "fix", analyzed: true, busy: false, seconds: 48, style: "Hip Hop - Trap", fromProject: false, more: 0, items: [
      { id: "setup-vocal-link", from: "setup", kind: "warn", title: "Ad-Libs has Voxology but no Mixology Link", need: "Add Mixology Link under Voxology, so Mixology hears the finished ad-libs.", text: "", tracks: ["Ad-Libs"] },
      { id: "vocal-low", from: "mix", kind: "warn", title: "Lead vocal sits 2.4 dB under the beat", need: "Yes: the words get lost in the chorus. Balance lifts it to where Hip Hop - Trap puts it.", text: "", tracks: ["Lead Vox"], tool: "balance" },
      { id: "kick-bass", from: "mix", kind: "warn", title: "Kick and 808 hit the same low notes", need: "Yes: they blur into one boom. Duck the 808 under the kick, or EQ them apart.", text: "", tracks: ["Kick", "808"] },
      { id: "mud", from: "mix", kind: "info", title: "A little mud around 300 Hz", need: "Have a look: Keys and Chorus Vox both pile up there.", text: "", tracks: ["Keys", "Chorus Vox"], tool: "clean" },
      { id: "harsh", from: "mix", kind: "info", title: "Hats a touch harsh at 6 kHz", need: "", text: "", tracks: ["Hats"], tool: "polish", done: true }] };

  // Beta 33: the modules a plug-in lets you move (as Voxology and Mixology Link mark them), and each unit's order.
  const MOVABLE = new Set(["vox.eq", "vox.dyneq", "vox.deess", "vox.rider", "vox.comp", "vox.sat",
    "mix.eq", "mix.comp", "mix.multi", "mix.trans", "mix.tone", "mix.loud", "mix.harm", "mix.analog", "mix.clip", "mix.stereo"]);
  const orders = {};   // unit id -> its movable keys in the order you set (none: its own)
  function applyOrder(u) {
    const mods = u.modules || [];
    const own = mods.filter((m) => MOVABLE.has(m.key)).map((m) => m.key);
    const want = orders[u.id] ? orders[u.id].filter((k) => own.includes(k)) : own;
    const slots = mods.map((m, i) => (MOVABLE.has(m.key) ? i : -1)).filter((i) => i >= 0);
    const byKey = new Map(mods.map((m) => [m.key, m]));
    const out = mods.slice();
    slots.forEach((slot, j) => { out[slot] = byKey.get(want[j]); });
    out.forEach((m, i) => { if (MOVABLE.has(m.key)) { m.movable = true; m.moved = own[slots.indexOf(i)] !== m.key; } });
    u.modules = out;
  }
  function moveIn(unitId, key, to, own) {
    const list = (orders[unitId] || own).filter((k) => k !== key);
    list.splice(Math.max(0, Math.min(to, list.length)), 0, key);
    orders[unitId] = list;
  }

  function units() {
    const list = rawUnits();
    for (const u of list) applyOrder(u);
    for (const u of list) {
      u.gen = 1;
      if (powered[u.id] === false) u.on = false;
      if (u.product === "masterology" && lockMaster) u.lock = "Auto-Master is running.";
      if (u.product === "masterology") {
        const r = masterCoachReport();
        u.report = JSON.stringify(r);
        if (r.state === "listening") u.lock = "Auto-Master is listening to the mix.";
        else if (r.state === "working") u.lock = "Auto-Master is working out the master.";
      }
      for (const m of u.modules || []) {
        m.switchable = !FIXED.has(m.key);
        const st = switched[u.id + "|" + m.key];
        if (st) { m.state = st; if (st === "off") m.summary = "Off"; }
        for (const x of m.values || []) {
          const key = x.name.toLowerCase().replace(/[^a-z0-9]/g, "");
          if (SHOWN_ONLY.has(m.key + "|" + key)) continue;
          x.key = key;
          x.steps = CHOICES[key] || 0;
          const t = turned[u.id + "|" + m.key + "|" + key];
          if (t != null) { x.norm = t; x.text = x.steps ? "Option " + (Math.round(t * (x.steps - 1)) + 1) : Math.round(t * 100) + " %"; }
        }
      }
    }
    // Beta 26: the Coach's reports on the vocals (locked while Auto-Edit listens and builds, as in Voxology)
    for (const u of list)
      if (u.product === "voxology" && u.role === "vocal") {
        const r = coachReport(u);
        u.report = JSON.stringify(r);
        if (r.state === "listening") u.lock = "Auto-Edit is listening to the vocal.";
        else if (r.state === "working") u.lock = "Auto-Edit is working out the settings.";
      }
    // Beta 25 (?order=1): the Links' input follows Voxology's input on Chorus Vox (inserted before it), its output on Lead Vox.
    if (orderTest) {
      const at = (id) => list.find((u) => u.id === id);
      if (at("vox-chorus") && at("link-chorus")) at("link-chorus").inDb = at("vox-chorus").inDb + 1.5 + wob(31, 0.3);
      if (at("vox-lead") && at("link-lead")) at("link-lead").inDb = at("vox-lead").outDb - 0.5 + wob(32, 0.3);
    }
    // Beta 24: Mixology's faders of the tracks a Rack shows (Lead Vox movable; Chorus Vox not in the bridge's bank: refused).
    const mix = list.find((u) => u.id === "mixology");
    if (mix) {
      const fv = (hash, movable, start) => {
        const k = "mixology|mix.faders|fader." + hash;
        const n = turned[k] != null ? turned[k] : start;
        const db = (n - 0.75) * 48;
        return { name: "Fader", text: n <= 0.001 ? "−∞" : (db < 0 ? "−" : db > 0 ? "+" : "") + Math.abs(db).toFixed(1) + " dB", norm: n, bi: false, key: "fader." + hash, steps: 0 };
      };
      mix.modules.push({ key: "mix.faders", state: "on", summary: "", switchable: false, values: [fv(H.lead, true, 0.70), fv(H.chorus, false, 0.66)] });
      const shown = shownTrack.startsWith("t") ? +shownTrack.slice(1) : 0;
      const link = shown && list.find((u) => u.product === "link" && u.hash === shown);
      if (link) mix.report = JSON.stringify(mixCoachReport(shown, link.track));
      // Beta 31: a Rack showing the Full Mix (the SESSION page) gets Mixology's WHAT TO FIX FIRST (?fix=none: not analyzed).
      else if (shownTrack === "fullmix") mix.report = JSON.stringify(FIX_REPORT());
    }
    return list;
  }

  window.SonicologyRackMock = {
    host: "mixology",
    async request(r) {
      const t = nextTicket++;
      // as the plug-ins' shared link carries it: a knob's value 0..1; a module's new place (order) a whole number
      r = Object.assign({}, r, { value: r.change === "order" ? Math.max(0, Math.min(15, +r.value || 0)) : Math.max(0, Math.min(1, +r.value || 0)) });
      if (lockMaster && r.unit === "masterology") return { ticket: 0, why: "Auto-Master is running." };
      const cs = coach[r.unit] && coach[r.unit].state;
      if (r.change !== "coach" && (cs === "listening" || cs === "working")) return { ticket: 0, why: cs === "listening" ? "Auto-Edit is listening to the vocal." : "Auto-Edit is working out the settings." };
      const ms = r.unit === "masterology" && masterCoach.state;
      if (r.change !== "coach" && (ms === "listening" || ms === "working")) return { ticket: 0, why: "Auto-Master is listening to the mix." };
      setTimeout(() => {
        if (r.module === "mas.tonaleq" && r.control === "air") { tickets[t] = { outcome: "refused", why: "Masterology can't change Air from the Rack." }; return; }
        if (r.change === "coach" && r.unit === "mixology") {
          const [stepName, h] = r.control.split(".");
          const hash = +h;
          if (stepName === "mixanalyze") { mixListen.state = "listening"; mixListen.t0 = Date.now(); tickets[t] = { outcome: "applied", why: "" }; return; }
          if (stepName === "mixfinish") {
            if (listenNow() < 20) { tickets[t] = { outcome: "refused", why: "Let Analyze Mix hear a little more of the song first." }; return; }
            mixListen.state = "done"; mixListen.heard = listenNow(); tickets[t] = { outcome: "applied", why: "" }; return;
          }
          if (stepName === "mixcancel") { if (mixListen.state === "listening") mixListen.state = "idle"; tickets[t] = { outcome: "applied", why: "" }; return; }
          const c = (mixCoach[hash] = mixCoach[hash] || { state: "idle" });
          const refuseMix = (why) => { tickets[t] = { outcome: "refused", why }; return null; };
          const pv = previewStep(c, stepName, -1.4, refuseMix);
          if (pv === null) return;
          if (pv) { tickets[t] = { outcome: "applied", why: "" }; return; }
          if (stepName === "plan") { c.state = hash === H.kick && !c.heard ? "listening" : "ready"; c.t0 = Date.now(); c.canUndo = false; }
          else if (stepName === "apply" && c.state === "ready") { c.state = "applied"; c.canUndo = true; }
          else if (stepName === "undo" && c.canUndo) { c.state = "ready"; c.canUndo = false; }
          else if (stepName === "apply") { tickets[t] = { outcome: "refused", why: "There's no plan to apply yet: press ANALYZE & BUILD." }; return; }
          tickets[t] = { outcome: "applied", why: "" };
          return;
        }
        if (r.change === "coach" && r.unit === "masterology") {
          const c = masterCoach, heard = (Date.now() - c.t0) / 1000 * 4;
          const refuse = (why) => { tickets[t] = { outcome: "refused", why }; };
          const pv = previewStep(c, r.control, 0.8, (why) => { refuse(why); return null; });
          if (pv === null) return;
          if (pv) { tickets[t] = { outcome: "applied", why: "" }; return; }
          if (r.control === "plan") { if (c.state === "listening" || c.state === "working") return refuse("The Coach is already listening to the song."); c.wasApplied = c.state === "applied"; c.state = "listening"; c.t0 = Date.now(); c.canUndo = false; }
          else if (r.control === "finish") { if (c.state !== "listening") return refuse("The Coach isn't listening."); if (heard < 12) return refuse("Let the Coach hear at least 12 seconds of the song first."); c.state = "working"; c.t1 = Date.now(); }
          else if (r.control === "cancel") { if (c.state === "listening" || c.state === "working") c.state = "idle"; }
          else if (r.control === "apply") { if (c.state !== "ready") return refuse("There's no plan to apply yet: press ANALYZE & BUILD and play the song."); c.state = "applied"; c.canUndo = true; }
          else if (r.control === "undo") { if (!c.canUndo) return refuse("Nothing to undo."); c.state = "ready"; c.canUndo = false; }
          tickets[t] = { outcome: "applied", why: "" };
          return;
        }
        if (r.change === "coach") {
          const c = (coach[r.unit] = coach[r.unit] || { state: "idle" });
          const pv = previewStep(c, r.control, 2.3, (why) => { tickets[t] = { outcome: "refused", why }; return null; });
          if (pv === null) return;
          if (pv) { tickets[t] = { outcome: "applied", why: "" }; return; }
          if (r.control === "cancel") { if (c.state === "listening") c.state = "idle"; }
          else if (r.control === "plan") { c.wasApplied = c.state === "applied"; c.state = "listening"; c.t0 = Date.now(); }
          else if (r.control === "apply" && c.state === "ready") { c.state = "applied"; c.canUndo = true; }
          else if (r.control === "undo" && c.canUndo) { c.state = c.state === "applied" ? "ready" : c.state; c.canUndo = false; }
          else if (r.control === "apply") { tickets[t] = { outcome: "refused", why: "There's no plan to apply yet: press ANALYZE & BUILD and play the song." }; return; }
          tickets[t] = { outcome: "applied", why: "" };
          return;
        }
        if (r.module === "mix.faders" && r.control === "fader." + H.chorus) { tickets[t] = { outcome: "refused", why: "Chorus Vox isn't in the tracks Mixology's bridge shows now: open Mixology's Tracks page on it, then move its fader here." }; return; }
        if (r.change === "save") {
          if (!r.unit.startsWith("vox-")) { tickets[t] = { outcome: "refused", why: "Only Voxology keeps chains." }; return; }
          saved.push(r.unit);
        } else if (r.change === "power") {
          if (r.unit.startsWith("ht-")) { tickets[t] = { outcome: "refused", why: "Honey Tune has no bypass of its own." }; return; }
          powered[r.unit] = r.value >= 0.5;
        } else if (r.change === "order") {
          const u = rawUnits().find((x) => x.id === r.unit);
          const own = ((u && u.modules) || []).filter((m) => MOVABLE.has(m.key)).map((m) => m.key);
          if (!own.length) { tickets[t] = { outcome: "refused", why: "This plug-in keeps its modules in their order." }; return; }
          if (r.control === "reset") delete orders[r.unit];
          else if (!own.includes(r.control)) { tickets[t] = { outcome: "refused", why: "That module stays where it is." }; return; }
          else moveIn(r.unit, r.control, Math.round(r.value), own);
        } else if (r.change === "state") switched[r.unit + "|" + r.module] = r.value >= 0.5 ? "on" : "off";
        else if (r.change === "reset" && r.module === "mix.faders") turned[r.unit + "|" + r.module + "|" + r.control] = 0.75;   // 0 dB
        else if (r.change === "reset") delete turned[r.unit + "|" + r.module + "|" + r.control];
        else {
          const k = r.unit + "|" + r.module + "|" + r.control;
          turned[k] = r.value;
          if (automation && r.gesture !== "begin" && r.gesture !== "move") setTimeout(() => { if (turned[k] === r.value) delete turned[k]; }, 700);
        }
        tickets[t] = { outcome: "applied", why: "" };
      }, 180);
      tickets[t] = { outcome: "pending", why: "" };
      return { ticket: t, why: "" };
    },
    async answer(unit, gen, ticket) { return tickets[ticket] || { outcome: "gone", why: "" }; },
    async getRack(track) {
      shownTrack = track || "";
      return {
        host: this.host, hostHash: this.host === "voxology" ? H.lead : 0,
        units: units(),
        together: [
          { hash: H.lead, level: "ok", text: "Voxology compresses this vocal, so Mixology adds no second compressor." },
          { hash: H.lead, level: "ok", text: "Voxology adds the reverb and delay here; Mixology adds none on top." },
          { hash: H.chorus, level: "warn", text: "Two reverbs on this vocal: Voxology's Reverb (plate) and Mixology's Space (hall). Stacked spaces can wash it out: turn one down or off.",
            fixes: [{ unit: "link-chorus", gen: 1, module: "mix.space", on: false, label: "Turn Mixology's Space off (hall)" },
                    { unit: "vox-chorus", gen: 1, module: "vox.reverb", on: false, label: "Turn Voxology's Reverb off" }] },
          { hash: H.adlibs, level: "warn", text: "Mixology can't hear this track: there is no Mixology Link on it." },
          { hash: H.keys, level: "ok", text: "The Space on Keys ducks under Lead Vox and Chorus Vox, so it blooms between the lines." },
          { hash: "fullmix", level: "ok", text: "Room made for Masterology: the Full Mix's Final Output is −3.5 dB, the mix peaks at −5.8 dBTP." },
          { hash: "master", level: "info", text: "Masterology aims for −14 LUFS (Spotify / Apple Music) at −1.0 dBTP; the mix comes in at −17.8 LUFS, so it adds about 3.8 dB." },
          { hash: "master", level: "ok", text: "Style shared by all three: Hip Hop - Trap." },
        ],
        mix: { lufsI: -14.2, lufsS: -13.6 + wob(21, 1.2), tp: -1.1 + wob(22, 0.1), source: "after Masterology" },
        master: { goal: "Spotify / Apple Music", lufs: -14, ceiling: -1 },
        otherLayouts: oldBeta ? [2] : [],
      };
    },
  };
})();
