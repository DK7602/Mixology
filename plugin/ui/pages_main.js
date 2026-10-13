// Overview and Tracks pages.

import { el, box, icon, fmt, clamp, Knob, Meter, select, graph, freqToX, previewTag, store } from "./ui.js";

// Track roles: icon + colour (the mockup's palette). Guessed from the channel name; Milestone 3 adds
// guessing from the sound and lets you correct it.
export const ROLES = {
  kick:   { icon: "kick",   colour: "#ff2b2b", label: "Kick" },
  snare:  { icon: "snare",  colour: "#3dff8a", label: "Snare" },
  hats:   { icon: "hats",   colour: "#ff3fa4", label: "Hats" },
  perc:   { icon: "perc",   colour: "#26d4ff", label: "Perc" },
  bass:   { icon: "bass",   colour: "#ffd21f", label: "Bass" },
  keys:   { icon: "keys",   colour: "#28c8ff", label: "Keys" },
  synth:  { icon: "synth",  colour: "#2fb6ff", label: "Synth" },
  guitar: { icon: "guitar", colour: "#b04dff", label: "Guitar" },
  vox:    { icon: "vox",    colour: "#3dff6e", label: "Vocals" },
  fx:     { icon: "fx",     colour: "#ffe23a", label: "FX" },
  adlibs: { icon: "adlibs", colour: "#ff9a1f", label: "Ad-libs" },
  other:  { icon: "other",  colour: "#c0c8d6", label: "Other" },
};

export function guessRole(name) {
  const n = (name || "").toLowerCase();
  const rules = [
    ["adlibs", /ad.?lib|adlib|hype|dub vox|backing|bgv|double/],
    ["vox", /vox|vocal|voc\b|lead v|verse|hook|rap|sing|acap/],
    ["kick", /kick|kik|\bbd\b|bassdrum/],
    ["snare", /snare|snr|clap|\bsd\b|rim/],
    ["hats", /hat|\bhh\b|cymbal|ride|crash|open h/],
    ["perc", /perc|shaker|tamb|conga|bongo|tom|cowbell|drum/],
    ["bass", /808|bass|sub\b|subs/],
    ["keys", /key|piano|rhodes|organ|\bep\b|wurl/],
    ["guitar", /guitar|gtr|acoustic|strat/],
    ["synth", /synth|pad|lead|pluck|arp|string|brass|bell|choir|sample|melod|loop/],
    ["fx", /\bfx\b|riser|sweep|impact|reverse|noise|verb|delay|transition/],
  ];
  for (const [role, re] of rules) if (re.test(n)) return role;
  return "other";
}

// =============================================================================================
// OVERVIEW

const SOURCES = [
  ["Kick", "#ff2424"], ["Bass", "#ff7a12"], ["Drums", "#ffe11f"], ["Vocals", "#3dff5a"], ["Instruments", "#1f6bff"], ["Other", "#e62bff"],
];

export function buildOverview(root, app) {
  // ---- left: operations
  const ops = [
    { id: "auto", label: "AUTO MIX", icon: "wand", when: "Milestone 4" },
    { id: "balance", label: "BALANCE", icon: "sliders", when: "Milestone 4" },
    { id: "clean", label: "CLEAN UP", icon: "wave", when: "Milestone 4" },
    { id: "pocket", label: "VOCAL POCKET", icon: "mic", when: "Milestone 4" },
    { id: "stereo", label: "STEREO ARCHITECT", icon: "rings", when: "Milestone 4" },
    { id: "ref", label: "REFERENCE MATCH", icon: "target", when: "Milestone 4" },
  ];
  const analyze = el("button", "btn big active", root, box(0, 0, 252, 112));
  analyze.innerHTML = `ANALYZE MIX<span class="sub">Scan tracks and build a mix</span>`;
  analyze.addEventListener("click", () =>
    app.toast("Analyze Mix arrives in Milestone 3: it listens through Mixology Link on each track. Right now Mixology hears your full mix and can move one locked Cubase fader (Tracks page)."));
  ops.forEach((o, i) => {
    const b = el("button", "btn", root, box(0, 130 + i * 120, 252, 104));
    b.innerHTML = icon(o.icon) + `<span>${o.label}</span>`;
    b.addEventListener("click", () => app.toast(`${o.label.charAt(0) + o.label.slice(1).toLowerCase()} arrives in ${o.when}.`));
  });

  // ---- centre: analyzer
  const an = el("div", "panel", root, box(266, 0, 830, 556));
  const t = el("div", "panel-title", an);
  t.textContent = "MIX ANALYZER";
  const legend = el("div", "", an, { position: "absolute", left: "218px", top: "27px", display: "flex", gap: "11px", fontSize: "15px" });
  legend.title = "Per-track colours arrive with Mixology Link (Milestone 3). Today the analyzer shows your whole mix, coloured by frequency.";
  for (const [name, c] of SOURCES) {
    const item = el("span", "", legend, { display: "flex", alignItems: "center", gap: "7px", opacity: "0.9" });
    el("span", "", item, { width: "14px", height: "14px", background: c, boxShadow: `0 0 6px ${c}` });
    item.appendChild(document.createTextNode(name));
  }
  const viewSel = select(an, 690, 16, 126, ["Live Mix", "Slow", "Peak Hold"], store.get("anView", "Live Mix"), (v) => { store.set("anView", v); hold = null; });

  const g = graph(an, 18, 72, 680, 466);
  const meters = { l: new Meter(an, 742, 92, 16, 400, [-48, 0]), r: new Meter(an, 764, 92, 16, 400, [-48, 0]), lufs: new Meter(an, 790, 92, 16, 400, [-48, 0]) };
  const scale = el("div", "", an, { position: "absolute", left: "708px", top: "84px", width: "30px", height: "420px", fontSize: "15px", color: "var(--text-dim)" });
  for (const db of [0, -6, -12, -24, -36, -48]) {
    const lab = el("div", "", scale, { position: "absolute", right: "2px", top: ((-db / 48) * 400 - 1) + "px" });
    lab.textContent = db === 0 ? "0" : fmt.num(db, 0);
  }
  const ml = el("div", "", an, { position: "absolute", left: "736px", top: "502px", width: "90px", display: "flex", gap: "8px", fontSize: "16px", color: "var(--text-dim)" });
  ml.innerHTML = "<span style='width:14px;text-align:center'>L</span><span style='width:14px;text-align:center'>R</span><span>LUFS</span>";

  let spectrum = null, hold = null, smooth = null;
  const DB_MIN = -48, DB_MAX = 12, OFFSET = 40;   // tilted spectrum reads about -40 for a balanced mix: centre it on 0
  const yOf = (db, h) => 12 + (1 - (db - DB_MIN) / (DB_MAX - DB_MIN)) * (h - 12);
  g.draw = (ctx, w, h) => {
    const plotH = h - 34;
    ctx.font = "15px 'Barlow SC', sans-serif";
    ctx.lineWidth = 1;
    for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
      const x = freqToX(f, w - 40) + 34;
      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue("--grid");
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, plotH); ctx.stroke();
      ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--text-dim");
      ctx.textAlign = "center";
      ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), clamp(x, 14, w - 14), h - 10);
    }
    for (const db of [12, 6, 0, -6, -12, -24, -36, -48]) {
      const y = yOf(db, plotH);
      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue(db === 0 ? "--grid-strong" : "--grid");
      ctx.beginPath(); ctx.moveTo(34, y); ctx.lineTo(w, y); ctx.stroke();
      ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--text-dim");
      ctx.textAlign = "right";
      ctx.fillText(db > 0 ? "+" + db : String(db), 28, y + 5);
    }
    if (!spectrum) return;
    const n = spectrum.length, W = w - 40;
    const pts = spectrum.map((v, i) => [34 + (i / (n - 1)) * W, yOf(clamp(v + OFFSET, DB_MIN - 6, DB_MAX), plotH)]);
    const rainbow = ctx.createLinearGradient(34, 0, w, 0);
    ["#ff1d1d", "#ff7a12", "#ffe11f", "#3dff5a", "#1fd1ff", "#1f6bff", "#9b3dff", "#ff2bf0"].forEach((c, i, a) => rainbow.addColorStop(i / (a.length - 1), c));

    // Filled "mountains", fading towards the floor.
    ctx.save();
    ctx.beginPath();
    ctx.moveTo(pts[0][0], plotH);
    for (const [x, y] of pts) ctx.lineTo(x, y);
    ctx.lineTo(pts[n - 1][0], plotH);
    ctx.closePath();
    ctx.clip();
    ctx.globalAlpha = 0.75;
    ctx.fillStyle = rainbow;
    ctx.fillRect(34, 0, W, plotH);
    ctx.globalAlpha = 1;
    const fade = ctx.createLinearGradient(0, 0, 0, plotH);
    const dark = document.documentElement.dataset.theme !== "light";
    fade.addColorStop(0, dark ? "rgba(6,10,16,0)" : "rgba(250,251,253,0)");
    fade.addColorStop(1, dark ? "rgba(6,10,16,0.92)" : "rgba(250,251,253,0.9)");
    ctx.fillStyle = fade;
    ctx.fillRect(34, 0, W, plotH);
    ctx.restore();

    ctx.save();
    ctx.lineWidth = 2.6;
    ctx.strokeStyle = rainbow;
    ctx.shadowColor = dark ? "rgba(255,255,255,0.35)" : "rgba(0,0,0,0.15)";
    ctx.shadowBlur = dark ? 10 : 3;
    ctx.beginPath();
    pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
    ctx.stroke();
    ctx.restore();

    // Smoothed overall balance line (white), like the mockup's mix line.
    if (smooth) {
      ctx.lineWidth = 2;
      ctx.strokeStyle = dark ? "rgba(255,255,255,0.92)" : "rgba(20,28,40,0.85)";
      ctx.beginPath();
      smooth.forEach((v, i) => {
        const x = 34 + (i / (n - 1)) * W, y = yOf(clamp(v + OFFSET, DB_MIN - 6, DB_MAX), plotH);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
    }
    if (hold) {
      ctx.setLineDash([4, 4]);
      ctx.lineWidth = 1.2;
      ctx.strokeStyle = dark ? "rgba(255,255,255,0.5)" : "rgba(20,28,40,0.5)";
      ctx.beginPath();
      hold.forEach((v, i) => {
        const x = 34 + (i / (n - 1)) * W, y = yOf(clamp(v + OFFSET, DB_MIN - 6, DB_MAX), plotH);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
      ctx.setLineDash([]);
    }
  };

  // ---- centre bottom: quick tone knobs (preview)
  const qt = el("div", "", root, box(266, 570, 830, 272));
  // Preview values from the approved mockup (double-click resets to 0). They become a real tone stage in
  // Milestone 5 and then start flat.
  const quick = [["LOW END", "qtLow", -0.5], ["LOW MID", "qtLowMid", -1.2], ["MID", "qtMid", 0.8], ["UPPER MID", "qtUpMid", 1.4], ["HIGH END", "qtHigh", 0.6], ["AIR", "qtAir", 1.2]];
  quick.forEach(([label, id, value], i) => {
    const card = el("div", "card", qt, box(i * 140, 0, 130, 272));
    new Knob(card, { x: -5, y: 24, w: 140, size: 106, label, id, min: -6, max: 6, value, def: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v), labelGap: 18, valueGap: 14 });
  });

  // ---- right: Mix Assistant
  const ma = el("div", "panel", root, box(1110, 0, 302, 842));
  ma.insertAdjacentHTML("beforeend", `<div style="position:absolute;left:18px;top:18px;width:44px;height:44px;color:var(--amber)">${icon("bulb")}</div>`);
  const mt = el("div", "panel-title gold", ma, { left: "72px", top: "16px", fontSize: "28px" });
  mt.textContent = "MIX ASSISTANT";
  const sub = el("div", "", ma, { position: "absolute", left: "72px", top: "54px", right: "14px", fontSize: "17px", color: "var(--text)" });
  sub.textContent = "What Mixology hears right now:";
  const list = el("div", "", ma, { position: "absolute", left: "12px", top: "92px", right: "12px", bottom: "12px", display: "flex", flexDirection: "column", gap: "10px" });

  function card(iconName, colour, title, hint, onClick) {
    const c = el("div", "card", list, { position: "relative", minHeight: "104px", padding: "14px 34px 12px 70px", cursor: onClick ? "pointer" : "default" });
    c.innerHTML = `<div style="position:absolute;left:12px;top:22px;width:46px;height:46px;color:${colour};filter:drop-shadow(0 0 6px ${colour})">${icon(iconName)}</div>
      <div style="font-size:19px;font-weight:500;line-height:1.2">${title}</div>
      <div style="font-size:16px;margin-top:5px;line-height:1.25;color:var(--cyan)">${hint}</div>
      ${onClick ? `<div style="position:absolute;right:8px;top:36px;width:24px;height:24px;color:var(--text)">${icon("chevR", 'stroke-width="2.4"')}</div>` : ""}`;
    if (onClick) c.addEventListener("click", onClick);
    return c;
  }

  let lastKey = "";
  function renderAssistant() {
    const f = app.frame || {};
    const b = app.bridge || {};
    const st = f.shortTerm ?? -100, tp = f.truePeak ?? -100;
    const key = [b.link, (b.slots || []).filter((s) => s.present).length, b.lock, Math.round(st), Math.round(tp)].join("|");
    if (key === lastKey) return;
    lastKey = key;
    list.innerHTML = "";
    const n = (b.slots || []).filter((s) => s.present).length;
    if (b.link === "connected")
      card("check", "#39e26b", `Cubase connected: ${n} channel${n === 1 ? "" : "s"} in view`, b.lock === "locked" ? `Test channel locked: ${b.lockedTitle}` : "Lock a test channel on the Tracks page", () => app.showPage("tracks"));
    else
      card("link", "#ffc04a", "Not connected to Cubase", b.portProblem ? "Ports missing: open the setup steps" : "Open Settings > Cubase Bridge for the setup", () => app.openSettings("bridge"));
    if (st > -70)
      card("speaker", "#19d3ff", `Mix loudness ${fmt.lufs(st)} LUFS (short-term)`, "Information only: Polisher sets the final loudness");
    else
      card("speaker", "#19d3ff", "No audio yet", "Press play in Cubase to see the mix here");
    if (tp > -70) {
      if (tp > -1) card("headroom", "#ff2b2b", `Peaks reach ${fmt.num(tp, 1)} dBTP`, "Pull the faders down a little: leave 3-6 dB for Polisher");
      else if (tp > -3) card("headroom", "#ffc04a", `Peaks reach ${fmt.num(tp, 1)} dBTP`, "A bit hot for mastering: aim for peaks around -6 to -3");
      else card("check", "#39e26b", `Headroom is good (peaks ${fmt.num(tp, 1)} dBTP)`, "Plenty of room for Polisher");
    }
    card("bulb", "#ff36e1", "Mix suggestions arrive in Milestone 3", "Kick / bass clashes, harsh hats, vocal pocket... need Mixology Link on each track");
  }

  return {
    onShow() { renderAssistant(); },
    onFrame(f) {
      meters.l.set(f.peakL ?? -100);
      meters.r.set(f.peakR ?? -100);
      meters.lufs.set((f.momentary ?? -100) + 0);
      if (f.spectrum) {
        const s = f.spectrum;
        const mode = viewSel.value;
        if (mode === "Slow") spectrum = spectrum && spectrum.length === s.length ? spectrum.map((v, i) => 0.85 * v + 0.15 * s[i]) : s.slice();
        else spectrum = s;
        hold = mode === "Peak Hold" ? (hold && hold.length === s.length ? hold.map((v, i) => Math.max(v - 0.05, s[i])) : s.slice()) : null;
        const k = 7;
        smooth = spectrum.map((_, i) => {
          let sum = 0, c = 0;
          for (let j = Math.max(0, i - k); j <= Math.min(spectrum.length - 1, i + k); j++) { sum += spectrum[j]; c++; }
          return sum / c - 3;
        });
        g.redraw();
      }
      renderAssistant();
    },
    always: false,
  };
}

// =============================================================================================
// TRACKS: the Cubase bank, live. Milestone 1: one locked channel can be moved from here.

const STRIPS = 12;

export function buildTracks(root, app) {
  const panel = el("div", "panel", root, box(0, 0, 1412, 842));

  // Top bar: link status, bank buttons, hint
  const bar = el("div", "", panel, { position: "absolute", left: "16px", right: "16px", top: "10px", height: "48px", display: "flex", alignItems: "center", gap: "12px", fontSize: "18px" });
  const status = el("div", "", bar, { minWidth: "360px" });
  const bankBtns = el("div", "", bar, { display: "flex", gap: "8px" });
  for (const [label, action, tip] of [["◀◀", 1, "Previous 12 channels"], ["◀", 3, "Shift one channel left"], ["▶", 4, "Shift one channel right"], ["▶▶", 2, "Next 12 channels"]]) {
    const b = el("button", "small-btn", bankBtns);
    b.textContent = label;
    b.title = tip;
    b.addEventListener("click", () => app.command("bank", action));
  }
  const hint = el("div", "", bar, { flex: "1", textAlign: "right", color: "var(--text-dim)", fontSize: "17px" });

  const W = 100, GAP = 6, X0 = 10, TOP = 66;
  const FADER_TOP = 160, FADER_H = 430;
  const strips = [];

  for (let i = 0; i < STRIPS; i++) {
    const s = el("div", "card", panel, box(X0 + i * (W + GAP), TOP, W, 764));
    const colourBar = el("div", "", s, { position: "absolute", left: "10px", right: "10px", top: "5px", height: "3px", borderRadius: "2px" });
    const name = el("div", "", s, { position: "absolute", left: "4px", right: "4px", top: "12px", textAlign: "center", fontSize: "20px", fontWeight: "500", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" });
    const lock = el("button", "", s, { position: "absolute", right: "4px", top: "44px", width: "28px", height: "28px", border: "0", padding: "3px", borderRadius: "7px", background: "transparent", cursor: "pointer", color: "var(--text-faint)" });
    lock.title = "Lock: let Mixology move this channel (test channel)";
    const ic = el("div", "", s, { position: "absolute", left: "26px", top: "46px", width: "48px", height: "48px" });
    const sBtn = el("button", "small-btn", s, { position: "absolute", left: "8px", top: "106px", width: "40px", padding: "0", fontSize: "20px" });
    sBtn.textContent = "S";
    const mBtn = el("button", "small-btn", s, { position: "absolute", left: "52px", top: "106px", width: "40px", padding: "0", fontSize: "20px" });
    mBtn.textContent = "M";

    // Fader: slot with meters inside, cap on top, scale on the right.
    const zone = el("div", "", s, box(6, FADER_TOP, 88, FADER_H));
    const slot = el("div", "", zone, { position: "absolute", left: "22px", top: "0", width: "26px", height: FADER_H + "px", borderRadius: "6px", background: "var(--well)", boxShadow: "inset 0 0 0 1px var(--panel-edge), inset 0 2px 6px rgba(0,0,0,0.6)" });
    const mL = new Meter(slot, 4, 4, 8, FADER_H - 8, [-60, 6]);
    const mR = new Meter(slot, 14, 4, 8, FADER_H - 8, [-60, 6]);
    const scale = el("div", "", zone, { position: "absolute", left: "54px", top: "0", width: "34px", height: FADER_H + "px", fontSize: "14px", color: "var(--text-dim)" });
    const cap = el("div", "", zone, {
      position: "absolute", left: "13px", width: "44px", height: "34px", borderRadius: "5px", cursor: "ns-resize", touchAction: "none",
      background: "linear-gradient(180deg, #f4f6f9 0%, #c9ced6 45%, #9aa2ad 50%, #d9dde3 56%, #eef1f4 100%)",
      boxShadow: "0 4px 8px rgba(0,0,0,0.6), inset 0 1px 0 #fff", border: "1px solid #6b737e",
    });
    el("div", "", cap, { position: "absolute", left: "4px", right: "4px", top: "16px", height: "2px", background: "#30343a" });

    const val = el("div", "", s, { position: "absolute", left: "0", right: "0", top: "602px", textAlign: "center", fontSize: "26px", fontWeight: "600" });
    const pan = new Knob(s, { x: 0, y: 640, w: 100, size: 66, min: -100, max: 100, def: 0, bipolar: true, step: 1, format: (v) => fmt.pan(v), valueGap: 2 });

    const strip = { s, colourBar, name, lock, ic, sBtn, mBtn, zone, cap, val, pan, mL, mR, scale, index: i, data: null };
    strips.push(strip);

    // Interaction (only the locked channel writes; everything goes through the bridge's safety checks)
    const isLocked = () => app.bridge && app.bridge.lock === "locked" && app.bridge.lockedSlot === i;
    const refuse = () => app.toast(app.bridge && app.bridge.lock === "paused"
      ? "Paused: " + app.bridge.pauseReason
      : "Click the padlock on this channel first: while we test the bridge, Mixology only moves the one channel you lock.");
    lock.addEventListener("click", () => {
      if (!strip.data || !strip.data.present) return;
      if (app.bridge.lockedSlot === i && app.bridge.lock !== "none") app.command("unlock");
      else app.command("lock", i);
    });
    sBtn.addEventListener("click", () => { if (!isLocked()) return refuse(); app.command("solo", !strip.data.solo); });
    mBtn.addEventListener("click", () => { if (!isLocked()) return refuse(); app.command("mute", !strip.data.mute); });

    let dragging = false, lastSent = 0, pending = null, startY = 0, startPos = 0;
    const capTop = (pos) => (1 - pos) * (FADER_H - 34);
    cap.addEventListener("pointerdown", (e) => {
      if (!isLocked()) return refuse();
      dragging = true;
      startY = e.clientY;
      startPos = strip.data.volume;
      cap.setPointerCapture(e.pointerId);
    });
    cap.addEventListener("pointermove", (e) => {
      if (!dragging) return;
      const k = document.getElementById("stage").getBoundingClientRect().width / 1440;
      const pos = clamp(startPos - (e.clientY - startY) / k / (FADER_H - 34) * (e.shiftKey ? 0.25 : 1), 0, 1);
      cap.style.top = capTop(pos) + "px";
      pending = pos;
      const now = performance.now();
      if (now - lastSent > 35) { lastSent = now; app.command("fader", pos); pending = null; }
    });
    const end = () => {
      if (!dragging) return;
      dragging = false;
      if (pending !== null) app.command("fader", pending);
      setTimeout(() => app.command("faderEnd"), 120);
    };
    cap.addEventListener("pointerup", end);
    cap.addEventListener("pointercancel", end);
    cap.addEventListener("dblclick", () => { if (isLocked()) app.command("setDb", 0); });
    strip.isDragging = () => dragging;
    strip.capTop = capTop;

    pan.o.onChange = (v) => {
      if (!isLocked()) { refuse(); pan.set(strip.data ? Math.round((strip.data.pan - 0.5) * 200) : 0); return; }
      app.command("pan", (v + 100) / 200);
    };
  }

  // Master strip: Mixology's own input (it sits on the Stereo Out)
  const ms = el("div", "card", panel, box(X0 + STRIPS * (W + GAP) + 4, TOP, 116, 764));
  const mn = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "12px", textAlign: "center", fontSize: "20px", fontWeight: "500" });
  mn.textContent = "Master";
  const msub = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "40px", textAlign: "center", fontSize: "14px", color: "var(--text-dim)", lineHeight: "1.15" });
  msub.innerHTML = "what Mixology<br>hears";
  const mmL = new Meter(ms, 30, 106, 22, 484, [-60, 6]);
  const mmR = new Meter(ms, 62, 106, 22, 484, [-60, 6]);
  const mval = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "602px", textAlign: "center", fontSize: "26px", fontWeight: "600" });
  const mlufs = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "650px", textAlign: "center", fontSize: "18px", color: "var(--text-dim)", lineHeight: "1.3" });
  let peakHold = -100, peakTime = 0;

  // Not-connected cover
  const cover = el("div", "", panel, { position: "absolute", left: "10px", top: "66px", width: "1266px", height: "764px", borderRadius: "12px", display: "none", placeItems: "center", textAlign: "center", background: "rgba(0,0,0,0.0)" });
  const coverBox = el("div", "card", cover, { position: "relative", width: "640px", padding: "30px 34px", fontSize: "20px", lineHeight: "1.45" });
  const coverText = el("div", "", coverBox);
  const coverBtn = el("button", "small-btn", coverBox, { marginTop: "18px" });
  coverBtn.textContent = "Open the Cubase Bridge setup";
  coverBtn.addEventListener("click", () => app.openSettings("bridge"));

  function render(b) {
    const connected = b && b.link === "connected";
    const slots = (b && b.slots) || [];
    status.innerHTML = connected
      ? `<span class="status-dot ok"></span>Cubase connected <span class="dim">(Mixology Bridge v${b.script})</span>`
      : `<span class="status-dot ${b && b.link === "waiting" ? "warn" : "bad"}"></span>${b && b.link === "waiting" ? "Waiting for Cubase..." : b && b.link === "lost" ? "Lost contact with Cubase" : "Not connected to Cubase"}`;
    bankBtns.style.opacity = connected ? "1" : "0.4";
    hint.innerHTML = !connected ? "" : b.lock === "locked" ? `<span style="color:var(--green)">●</span> Test channel: <b style="color:var(--text)">${b.lockedTitle}</b>. Drag its fader, pan, S / M.`
      : b.lock === "paused" ? `<span style="color:var(--amber)">● Paused:</span> ${b.pauseReason}` : "Click a padlock to pick the test channel Mixology may move.";
    cover.style.display = connected ? "none" : "grid";
    coverText.innerHTML = b && b.portProblem
      ? `<b>Mixology can't reach Cubase yet.</b><br>${b.portProblem}`
      : `<b>Waiting for Cubase.</b><br>Mixology talks to the real MixConsole through a small Cubase MIDI Remote script and two virtual MIDI ports. It takes about five minutes to set up once.`;

    const marks = (b && b.marks) || [];
    strips.forEach((st, i) => {
      const d = slots[i];
      st.data = d;
      const present = connected && d && d.present;
      st.s.style.opacity = present ? "1" : "0.35";
      const role = present ? guessRole(d.title) : "other";
      const R = ROLES[role];
      st.name.textContent = present ? d.title : "—";
      st.name.title = present ? d.title : "";
      st.colourBar.style.background = present && d.colour ? d.colour : "transparent";
      if (st.role !== role) { st.ic.innerHTML = icon(R.icon); st.role = role; }
      st.ic.style.color = R.colour;
      st.ic.style.filter = `drop-shadow(0 0 6px ${R.colour})`;
      const locked = connected && b.lockedSlot === i && b.lock !== "none";
      st.lock.innerHTML = icon(locked ? "lock" : "unlock", 'stroke-width="2.2"');
      st.lock.style.color = locked ? (b.lock === "locked" ? "var(--green)" : "var(--amber)") : "var(--text-faint)";
      st.s.style.boxShadow = locked && b.lock === "locked" ? "0 0 0 2px var(--green), 0 0 16px rgba(57,226,107,0.35)" : "";
      st.sBtn.classList.toggle("red", !!(present && d.solo));
      st.sBtn.style.background = present && d.solo ? "linear-gradient(180deg,#ffe36b,#d8a400)" : "";
      st.sBtn.style.color = present && d.solo ? "#1a1300" : "";
      st.mBtn.classList.toggle("red", !!(present && d.mute));
      if (!st.isDragging()) st.cap.style.top = st.capTop(present && d.hasVolume ? d.volume : 0.78) + "px";
      st.val.textContent = present && d.hasVolume ? (d.volumeText.match(/oo|inf|∞/i) ? MINUS_INF : formatFaderText(d.volumeText)) : "";
      st.val.style.color = document.documentElement.dataset.theme === "light" ? `color-mix(in srgb, ${R.colour} 62%, #000)` : R.colour;
      if (present && document.activeElement !== st.pan.root) st.pan.set(Math.round((d.pan - 0.5) * 200));
      st.pan.root.style.opacity = present ? "1" : "0.5";
      if (st.marksKey !== JSON.stringify(marks)) {
        st.marksKey = JSON.stringify(marks);
        st.scale.innerHTML = "";
        for (const m of marks) {
          if (m.db > 0) continue;
          const lab = el("div", "", st.scale, { position: "absolute", left: "2px", top: (st.capTop(m.pos) + 8) + "px" });
          lab.textContent = m.db === 0 ? "0" : "−" + Math.abs(m.db);
        }
      }
    });
  }
  const MINUS_INF = "−∞";
  function formatFaderText(t) {
    const v = parseFloat(String(t).replace(",", ".").replace("−", "-"));
    if (!isFinite(v)) return t;
    const r = Math.round(v * 10) / 10;
    return (r > 0 ? "+" : r < 0 ? "−" : "") + Math.abs(r).toFixed(1);
  }

  app.on("bridge", render);
  app.on("theme", () => render(app.bridge));
  render(app.bridge);

  return {
    onFrame(f) {
      // Per-channel meters arrive with Mixology Link (Milestone 2/3). The preview simulates them.
      if (f.trackMeters) strips.forEach((st, i) => { const m = f.trackMeters[i]; if (m) { st.mL.set(m[0]); st.mR.set(m[1]); } });
      mmL.set(f.peakL ?? -100);
      mmR.set(f.peakR ?? -100);
      const p = Math.max(f.peakL ?? -100, f.peakR ?? -100);
      const now = performance.now();
      if (p >= peakHold || now - peakTime > 2000) { peakHold = p; peakTime = now; }
      mval.textContent = peakHold > -99 ? fmt.signed(peakHold, 1) : "−∞";
      mval.style.color = peakHold > -1 ? "var(--red)" : "var(--text)";
      mlufs.innerHTML = `${fmt.lufs(f.shortTerm ?? -100)}<br>LUFS S`;
    },
  };
}
