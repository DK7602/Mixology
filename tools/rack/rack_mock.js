// Sonicology Rack: a made-up project for previewing the view in a browser (tools/rack/preview.html; not in the
// plug-ins). Lead Vox with Voxology + a Link, Chorus Vox with Honey Tune + Voxology + a
// Link, Ad-Libs with Voxology alone, the Beat with Voxology in Beat mode + a Link, drums / 808 / keys with Links, the
// Full Mix (Mixology) and the Master (Masterology). Meters move. Stage 2: knobs turn and modules switch (request /
// answer, about a fifth of a second later, as a plug-in would); ?lock=1 locks Masterology (Auto-Master running);
// Masterology's Tonal EQ Air refuses (to see a refusal); ?old=1 adds a plug-in from another
// beta; ?auto=1 has the DAW's automation move a turned knob back.
(function () {
  "use strict";
  const t0 = Date.now();
  const wob = (seed, depth = 1) => {
    const t = (Date.now() - t0) / 1000;
    return depth * (Math.sin(t * 2.1 + seed) * 0.55 + Math.sin(t * 5.3 + seed * 1.7) * 0.3 + Math.sin(t * 11.7 + seed * 0.3) * 0.15);
  };
  const v = (name, text, norm, bi) => ({ name, text, norm, bi: !!bi });
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
          { key: "mas.dynamics", state: "on", summary: "4 bands · GR 1 - 2 dB", meterKind: "gr", meter: -1.2 + wob(18, 0.6), values: [v("Low", "−1.5 dB", 0.4), v("Lo-Mid", "−1.0 dB", 0.45), v("Hi-Mid", "−0.5 dB", 0.48), v("High", "−1.0 dB", 0.45)] },
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
  const SHOWN_ONLY = new Set(["ht.tune|key", "ht.tune|scale", "vox.beat|key", "vox.beat|scale"]);
  const FIXED = new Set(["vox.output", "vox.beat", "ht.tune", "mix.assist", "mas.max"]);
  const turned = {}, switched = {}, tickets = {};
  let nextTicket = 1;
  const lockMaster = new URLSearchParams(location.search).get("lock") === "1";
  const automation = new URLSearchParams(location.search).get("auto") === "1";   // ?auto=1: the DAW's automation moves knobs back
  const oldBeta = new URLSearchParams(location.search).get("old") === "1";   // ?old=1: a plug-in from another beta is in the project
  function units() {
    const list = rawUnits();
    for (const u of list) {
      u.gen = 1;
      if (u.product === "masterology" && lockMaster) u.lock = "Auto-Master is running.";
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
    return list;
  }

  window.SonicologyRackMock = {
    host: "mixology",
    async request(r) {
      const t = nextTicket++;
      if (lockMaster && r.unit === "masterology") return { ticket: 0, why: "Auto-Master is running." };
      setTimeout(() => {
        if (r.module === "mas.tonaleq" && r.control === "air") { tickets[t] = { outcome: "refused", why: "Masterology can't change Air from the Rack." }; return; }
        if (r.change === "state") switched[r.unit + "|" + r.module] = r.value >= 0.5 ? "on" : "off";
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
    async getRack() {
      return {
        host: this.host, hostHash: this.host === "voxology" ? H.lead : 0,
        units: units(),
        together: [
          { hash: H.lead, level: "ok", text: "Voxology compresses this vocal, so Mixology's Link adds no second compressor." },
          { hash: H.lead, level: "ok", text: "Voxology adds the reverb and delay here; Mixology adds none on top." },
          { hash: H.chorus, level: "warn", text: "Two reverbs on this vocal: Voxology's Reverb (plate) and the Link's Space (hall). Stacked spaces can wash it out: turn one down or off.",
            fixes: [{ unit: "link-chorus", gen: 1, module: "mix.space", on: false, label: "Turn the Link's Space off (hall)" },
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
