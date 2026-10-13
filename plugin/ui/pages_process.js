// EQ, Dynamics and Space pages. Laid out and working as controls; their audio processing goes live in
// Milestone 5 (inside Mixology Link on each track), so each page shows a "Preview" tag until then.

import { el, box, icon, fmt, clamp, Knob, Meter, select, graph, freqToX, xToFreq, moduleList, previewTag, store, cssVar } from "./ui.js";

const TRACK_CHOICES = () => ["Full Mix"];

function trackOptions(app) {
  const names = ((app.bridge && app.bridge.slots) || []).filter((s) => s.present).map((s) => s.title);
  return ["Full Mix", ...names];
}

// =============================================================================================
// EQ

const SR = 48000;
function biquadDb(type, f0, gainDb, q, f) {
  // RBJ cookbook magnitude response, evaluated at f.
  const A = Math.pow(10, gainDb / 40), w0 = (2 * Math.PI * f0) / SR, cw = Math.cos(w0), sw = Math.sin(w0), alpha = sw / (2 * q);
  let b0, b1, b2, a0, a1, a2;
  if (type === "bell") { b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; }
  else if (type === "lowshelf") { const s = 2 * Math.sqrt(A) * alpha; b0 = A * ((A + 1) - (A - 1) * cw + s); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - s); a0 = (A + 1) + (A - 1) * cw + s; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - s; }
  else if (type === "highshelf") { const s = 2 * Math.sqrt(A) * alpha; b0 = A * ((A + 1) + (A - 1) * cw + s); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - s); a0 = (A + 1) - (A - 1) * cw + s; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - s; }
  else if (type === "lowcut") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }
  else { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }   // highcut
  const w = (2 * Math.PI * f) / SR, c1 = Math.cos(w), c2 = Math.cos(2 * w), s1 = Math.sin(w), s2 = Math.sin(2 * w);
  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
  const mag = Math.sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
  let db = 20 * Math.log10(Math.max(mag, 1e-9));
  if (type === "lowcut" || type === "highcut") db *= 2;   // 24 dB / octave (two stages)
  return db;
}

const EQ_BANDS = [
  { colour: "#ff2a2a", f: 80, g: -2.5, q: 0.7, type: "lowshelf", shapes: ["lowcut", "lowshelf", "bell"] },
  { colour: "#ffd21f", f: 250, g: -4.0, q: 1.0, type: "bell", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#3dff5a", f: 800, g: 0.6, q: 0.7, type: "bell", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#1f9bff", f: 2500, g: 3.5, q: 1.0, type: "bell", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#2f5bff", f: 8000, g: -1.8, q: 0.9, type: "bell", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#ff36e1", f: 16000, g: 2.6, q: 0.8, type: "highshelf", shapes: ["bell", "highshelf", "highcut"] },
];
const SHAPE_NAMES = { bell: "Bell", lowshelf: "Low shelf", highshelf: "High shelf", lowcut: "Low cut", highcut: "High cut" };

export function buildEq(root, app) {
  const panel = el("div", "panel", root, box(0, 0, 1412, 842));
  const title = el("div", "panel-title", panel);
  title.textContent = "EQ";
  previewTag(panel, 80, 26);

  const bands = EQ_BANDS.map((b, i) => ({
    ...b,
    f: store.value(`eqF${i}`, b.f), g: store.value(`eqG${i}`, b.g), q: store.value(`eqQ${i}`, b.q), type: store.value(`eqT${i}`, b.type),
  }));
  const save = (i) => { const b = bands[i]; store.setValue(`eqF${i}`, b.f); store.setValue(`eqG${i}`, b.g); store.setValue(`eqQ${i}`, b.q); store.setValue(`eqT${i}`, b.type); };

  // Toolbar: character, target track, settings
  const modes = ["Precision", "Smooth", "Natural"];
  let mode = store.get("eqMode", "Precision");
  const chips = el("div", "", panel, { position: "absolute", right: "290px", top: "16px", display: "flex", gap: "10px" });
  const chipEls = modes.map((m) => {
    const c = el("div", "chip", chips);
    c.textContent = m;
    c.title = { Precision: "Clean digital curves (minimum phase)", Smooth: "Wider, gentler curves", Natural: "Analog-style curves that interact like a console EQ" }[m];
    c.addEventListener("click", () => { mode = m; store.set("eqMode", m); chipEls.forEach((x, j) => x.classList.toggle("on", modes[j] === m)); });
    return c;
  });
  chipEls.forEach((x, j) => x.classList.toggle("on", modes[j] === mode));
  const target = select(panel, 1142, 16, 180, TRACK_CHOICES(), store.get("eqTarget", "Full Mix"), (v) => store.set("eqTarget", v));
  app.on("bridge", () => target.setOptions(trackOptions(app)));
  const gearBtn = el("button", "small-btn", panel, { position: "absolute", left: "1334px", top: "16px", width: "56px", height: "44px", padding: "8px", color: "var(--gold)" });
  gearBtn.innerHTML = icon("gear");
  gearBtn.title = "Show the analyzer behind the curve";
  let showSpectrum = store.get("eqSpectrum", true);
  gearBtn.addEventListener("click", () => { showSpectrum = !showSpectrum; store.set("eqSpectrum", showSpectrum); g.redraw(); });

  // Graph
  const GX = 16, GY = 76, GW = 1380, GH = 432;
  const g = graph(panel, GX, GY, GW, GH);
  const PAD_L = 52, PAD_B = 34, RANGE = 12;
  const plotW = GW - PAD_L - 16, plotH = GH - PAD_B - 14;
  const fx = (f) => PAD_L + freqToX(f, plotW);
  const gy = (db) => 14 + (1 - (db + RANGE) / (2 * RANGE)) * plotH;
  let spectrum = null;

  const response = (f) => bands.reduce((sum, b) => sum + biquadDb(b.type, b.f, b.g, b.q, f), 0);
  g.draw = (ctx, w, h) => {
    const grid = cssVar("--grid"), strong = cssVar("--grid-strong"), dim = cssVar("--text-dim");
    ctx.font = "17px 'Barlow SC', sans-serif";
    for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
      const x = fx(f);
      ctx.strokeStyle = grid; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(x, 14); ctx.lineTo(x, 14 + plotH); ctx.stroke();
      ctx.fillStyle = dim; ctx.textAlign = "center";
      ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), x, h - 10);
    }
    for (const db of [12, 6, 0, -6, -12]) {
      const y = gy(db);
      ctx.strokeStyle = db === 0 ? strong : grid;
      ctx.beginPath(); ctx.moveTo(PAD_L, y); ctx.lineTo(PAD_L + plotW, y); ctx.stroke();
      ctx.fillStyle = dim; ctx.textAlign = "right";
      ctx.fillText(db > 0 ? "+" + db : String(db), PAD_L - 10, y + 6);
    }
    if (showSpectrum && spectrum) {
      ctx.fillStyle = document.documentElement.dataset.theme === "light" ? "rgba(30,60,110,0.10)" : "rgba(120,170,255,0.10)";
      ctx.beginPath();
      ctx.moveTo(PAD_L, 14 + plotH);
      spectrum.forEach((v, i) => ctx.lineTo(PAD_L + (i / (spectrum.length - 1)) * plotW, clamp(gy((v + 40) / 3), 14, 14 + plotH)));
      ctx.lineTo(PAD_L + plotW, 14 + plotH);
      ctx.fill();
    }
    // Curve coloured by the nearest band
    const grad = ctx.createLinearGradient(PAD_L, 0, PAD_L + plotW, 0);
    const sorted = [...bands].sort((a, b) => a.f - b.f);
    sorted.forEach((b) => grad.addColorStop(clamp((fx(b.f) - PAD_L) / plotW, 0, 1), b.colour));
    const pts = [];
    for (let i = 0; i <= 360; i++) {
      const x = PAD_L + (i / 360) * plotW;
      pts.push([x, gy(clamp(response(xToFreq(x - PAD_L, plotW)), -RANGE - 2, RANGE + 2))]);
    }
    ctx.save();
    ctx.globalAlpha = 0.28;
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.moveTo(PAD_L, gy(0));
    pts.forEach(([x, y]) => ctx.lineTo(x, y));
    ctx.lineTo(PAD_L + plotW, gy(0));
    ctx.fill();
    ctx.restore();
    ctx.save();
    ctx.strokeStyle = grad;
    ctx.lineWidth = 3.5;
    ctx.shadowColor = "rgba(255,255,255,0.4)";
    ctx.shadowBlur = 10;
    ctx.beginPath();
    pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
    ctx.stroke();
    ctx.restore();
    // Nodes
    bands.forEach((b) => {
      const x = fx(b.f), y = gy(b.type === "lowcut" || b.type === "highcut" ? 0 : b.g);
      ctx.save();
      ctx.shadowColor = b.colour; ctx.shadowBlur = 18;
      ctx.fillStyle = b.colour;
      ctx.beginPath(); ctx.arc(x, y, 15, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
      ctx.strokeStyle = "rgba(255,255,255,0.9)"; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(x, y, 15, 0, Math.PI * 2); ctx.stroke();
      ctx.fillStyle = "#fff";
      ctx.beginPath(); ctx.arc(x, y, 4.5, 0, Math.PI * 2); ctx.fill();
    });
  };

  // Node dragging (x = frequency, y = gain, wheel = Q)
  let drag = -1;
  const toLocal = (e) => {
    const r = g.canvas.getBoundingClientRect();
    return [((e.clientX - r.left) / r.width) * GW, ((e.clientY - r.top) / r.height) * GH];
  };
  const hit = (x, y) => bands.findIndex((b) => Math.hypot(fx(b.f) - x, gy(b.type === "lowcut" || b.type === "highcut" ? 0 : b.g) - y) < 22);
  g.canvas.addEventListener("pointerdown", (e) => {
    const [x, y] = toLocal(e);
    drag = hit(x, y);
    if (drag >= 0) g.canvas.setPointerCapture(e.pointerId);
  });
  g.canvas.addEventListener("pointermove", (e) => {
    const [x, y] = toLocal(e);
    g.canvas.style.cursor = drag >= 0 || hit(x, y) >= 0 ? "grab" : "default";
    if (drag < 0) return;
    const b = bands[drag];
    b.f = Math.round(clamp(xToFreq(x - PAD_L, plotW), 20, 20000));
    if (b.type !== "lowcut" && b.type !== "highcut") b.g = Math.round(clamp(((14 + plotH - y) / plotH) * 2 * RANGE - RANGE, -RANGE, RANGE) * 10) / 10;
    save(drag);
    updateCards();
    g.redraw();
  });
  g.canvas.addEventListener("pointerup", () => { drag = -1; });
  g.canvas.addEventListener("wheel", (e) => {
    const [x, y] = toLocal(e);
    const i = hit(x, y);
    if (i < 0) return;
    e.preventDefault();
    bands[i].q = Math.round(clamp(bands[i].q * (e.deltaY < 0 ? 1.08 : 1 / 1.08), 0.3, 10) * 10) / 10;
    cards[i].q.set(bands[i].q);
    save(i);
    g.redraw();
  }, { passive: false });

  // Band cards
  const cards = bands.map((b, i) => {
    const c = el("div", "card", panel, box(16 + i * 232, 524, 222, 302));
    const dot = el("div", "", c, { position: "absolute", left: "18px", top: "20px", width: "30px", height: "30px", borderRadius: "50%", background: b.colour, boxShadow: `0 0 12px ${b.colour}`, border: "2px solid rgba(255,255,255,0.8)" });
    const freq = el("div", "value", c, { position: "absolute", left: "64px", top: "16px", fontSize: "27px" });
    const gain = el("div", "value", c, { position: "absolute", left: "64px", top: "60px", fontSize: "27px" });
    const shapeBtns = b.shapes.map((s, k) => {
      const sb = el("button", "small-btn", c, { position: "absolute", left: "12px", top: 104 + k * 62 + "px", width: "62px", height: "52px", padding: "8px 10px", color: "var(--text)" });
      sb.innerHTML = icon(s, 'stroke-width="2"');
      sb.title = SHAPE_NAMES[s];
      sb.addEventListener("click", () => { b.type = s; save(i); updateCards(); g.redraw(); });
      return sb;
    });
    const q = new Knob(c, { x: 84, y: 112, w: 130, size: 104, min: 0.3, max: 10, curve: "log", value: b.q, def: 0.7, colour: b.colour, format: (v) => "Q " + v.toFixed(1), valueGap: 10,
      onChange: (v) => { b.q = Math.round(v * 10) / 10; save(i); g.redraw(); } });
    q.valueEl.style.fontSize = "26px";
    return { c, dot, freq, gain, shapeBtns, q };
  });
  function updateCards() {
    bands.forEach((b, i) => {
      const cd = cards[i];
      cd.freq.textContent = fmt.hz(b.f);
      cd.gain.textContent = b.type === "lowcut" || b.type === "highcut" ? "24 dB/oct" : fmt.db(b.g);
      cd.shapeBtns.forEach((sb, k) => {
        const on = b.shapes[k] === b.type;
        sb.style.borderColor = on ? b.colour : "";
        sb.style.boxShadow = on ? `0 0 10px ${b.colour}` : "";
        sb.style.color = on ? b.colour : "var(--text)";
      });
    });
  }
  updateCards();
  app.on("theme", () => g.redraw());
  g.redraw();

  return {
    onFrame(f) {
      if (showSpectrum && f.spectrum) {
        spectrum = spectrum && spectrum.length === f.spectrum.length ? spectrum.map((v, i) => 0.7 * v + 0.3 * f.spectrum[i]) : f.spectrum.slice();
        g.redraw();
      }
    },
  };
}

// =============================================================================================
// DYNAMICS

const DYN_MODULES = [
  { id: "comp", label: "COMPRESSOR", icon: "compressor" },
  { id: "multi", label: "MULTIBAND", icon: "bars" },
  { id: "trans", label: "TRANSIENT", icon: "transient" },
  { id: "gate", label: "GATE / EXPAND", icon: "gate" },
  { id: "limit", label: "LIMITER", icon: "clock" },
];

const DYN_KNOBS = {
  comp: [
    { id: "thr", label: "THRESHOLD", min: -60, max: 0, value: -28, step: 0.1, format: (v) => fmt.db(v) },
    { id: "ratio", label: "RATIO", min: 1, max: 20, curve: "log", value: 3, format: (v) => fmt.ratio(v) },
    { id: "att", label: "ATTACK", min: 0.1, max: 100, curve: "log", value: 10, format: (v) => fmt.ms(v) },
    { id: "rel", label: "RELEASE", min: 10, max: 1000, curve: "log", value: 100, format: (v) => fmt.ms(v) },
    { id: "mk", label: "MAKEUP", min: 0, max: 24, value: 2.5, step: 0.1, format: (v) => fmt.db(v) },
  ],
  multi: [
    { id: "x1", label: "LOW / MID", min: 40, max: 1000, curve: "log", value: 160, format: (v) => fmt.hz(v) },
    { id: "x2", label: "MID / HIGH", min: 800, max: 12000, curve: "log", value: 3500, format: (v) => fmt.hz(v) },
    { id: "lo", label: "LOW", min: 0, max: 100, value: 40, format: (v) => fmt.pct(v) },
    { id: "mid", label: "MID", min: 0, max: 100, value: 25, format: (v) => fmt.pct(v) },
    { id: "hi", label: "HIGH", min: 0, max: 100, value: 30, format: (v) => fmt.pct(v) },
  ],
  trans: [
    { id: "att", label: "ATTACK", min: -100, max: 100, value: 25, bipolar: true, format: (v) => fmt.signed(v, 0) + "%" },
    { id: "sus", label: "SUSTAIN", min: -100, max: 100, value: -15, bipolar: true, format: (v) => fmt.signed(v, 0) + "%" },
    { id: "spd", label: "SPEED", min: 1, max: 50, curve: "log", value: 8, format: (v) => fmt.ms(v) },
    { id: "out", label: "OUTPUT", min: -12, max: 12, value: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v) },
    { id: "mix", label: "MIX", min: 0, max: 100, value: 100, format: (v) => fmt.pct(v) },
  ],
  gate: [
    { id: "thr", label: "THRESHOLD", min: -80, max: 0, value: -45, step: 0.1, format: (v) => fmt.db(v) },
    { id: "range", label: "RANGE", min: 0, max: 80, value: 18, step: 0.1, format: (v) => fmt.db(-v) },
    { id: "att", label: "ATTACK", min: 0.1, max: 50, curve: "log", value: 1, format: (v) => fmt.ms(v) },
    { id: "hold", label: "HOLD", min: 0, max: 500, value: 40, format: (v) => fmt.ms(v) },
    { id: "rel", label: "RELEASE", min: 10, max: 2000, curve: "log", value: 150, format: (v) => fmt.ms(v) },
  ],
  limit: [
    { id: "in", label: "INPUT", min: 0, max: 12, value: 0, step: 0.1, format: (v) => fmt.db(v) },
    { id: "ceil", label: "CEILING", min: -12, max: 0, value: -3, step: 0.1, format: (v) => fmt.db(v) },
    { id: "rel", label: "RELEASE", min: 5, max: 1000, curve: "log", value: 80, format: (v) => fmt.ms(v) },
    { id: "look", label: "LOOKAHEAD", min: 0, max: 5, value: 1.5, step: 0.1, format: (v) => v.toFixed(1) + " ms" },
    { id: "char", label: "CHARACTER", min: 0, max: 100, value: 30, format: (v) => (v < 50 ? "Clean " : "Punchy ") + Math.round(v) + "%" },
  ],
};
const DYN_EXTRA = {
  comp: { label: "KNEE", options: ["Soft", "Medium", "Hard"], value: "Soft" },
  multi: { label: "BANDS", options: ["3 bands", "2 bands"], value: "3 bands" },
  trans: { label: "FOCUS", options: ["Full", "Lows", "Highs"], value: "Full" },
  gate: { label: "MODE", options: ["Gate", "Expander"], value: "Gate" },
  limit: { label: "STAGE", options: ["Bus safety", "Track peak"], value: "Bus safety" },
};

export function buildDynamics(root, app) {
  let mod = store.get("dynModule", "comp");
  moduleList(root, DYN_MODULES, 0, 0, 252, 92, 16, mod, (id) => { mod = id; store.set("dynModule", id); renderModule(); });
  el("div", "card", root, box(0, 544, 252, 298));

  const top = el("div", "panel", root, box(266, 0, 1146, 556));
  previewTag(top, 22, 18);
  const source = select(top, 836, 14, 290, TRACK_CHOICES(), store.get("dynTarget", "Full Mix"), (v) => store.set("dynTarget", v), { dot: "#1fd1ff" });
  app.on("bridge", () => source.setOptions(trackOptions(app)));

  const GW = 790, GH = 474;
  const g = graph(top, 18, 66, GW, GH);
  const meters = [new Meter(top, 858, 94, 20, 396, [-48, 0]), new Meter(top, 894, 94, 20, 396, [-48, 0]), new Meter(top, 990, 94, 20, 396, [-48, 0]), new Meter(top, 1026, 94, 20, 396, [-48, 0])];
  const grMeter = el("div", "meter", top, box(1080, 94, 20, 396));
  const grFill = el("div", "", grMeter, { position: "absolute", left: 0, right: 0, top: 0, height: "0%", background: "linear-gradient(180deg,#ffb020,#ff5a10)", boxShadow: "0 0 10px rgba(255,140,30,0.6)" });
  const mlab = el("div", "", top, { position: "absolute", left: "846px", top: "498px", width: "290px", display: "flex", fontSize: "18px", color: "var(--text-dim)" });
  mlab.innerHTML = `<span style="width:36px;text-align:center">L</span><span style="width:96px;text-align:left">R</span><span style="width:36px;text-align:center">IN</span><span style="width:54px">OUT</span><span>GR</span>`;
  const mscale = el("div", "", top, { position: "absolute", left: "818px", top: "86px", width: "32px", height: "410px", fontSize: "15px", color: "var(--text-dim)" });
  for (const db of [0, -6, -12, -24, -36, -48]) {
    const lab = el("div", "", mscale, { position: "absolute", right: "4px", top: ((-db / 48) * 396) + "px" });
    lab.textContent = db === 0 ? "0" : "−" + -db;
  }

  const bottom = el("div", "panel", root, box(266, 570, 1146, 272));
  const knobHost = el("div", "", bottom, box(0, 0, 900, 272));
  const extraCard = el("div", "card", bottom, box(910, 16, 220, 240));
  const extraLabel = el("div", "label", extraCard, { position: "absolute", left: "0", right: "0", top: "28px", textAlign: "center" });
  let extraSel = null;
  let knobs = {};

  function v(id) { return knobs[id] ? knobs[id].value : 0; }

  function renderModule() {
    knobHost.innerHTML = "";
    knobs = {};
    DYN_KNOBS[mod].forEach((k, i) => {
      knobs[k.id] = new Knob(knobHost, { ...k, id: `dyn_${mod}_${k.id}`, x: 16 + i * 176, y: 28, w: 170, size: 118, def: k.value, labelGap: 14, valueGap: 12, onChange: () => g.redraw() });
    });
    const ex = DYN_EXTRA[mod];
    extraLabel.textContent = ex.label;
    if (extraSel) extraSel.el.remove();
    extraSel = select(extraCard, 26, 98, 168, ex.options, store.get(`dyn_${mod}_extra`, ex.value), (val) => { store.set(`dyn_${mod}_extra`, val); g.redraw(); }, { h: 64 });
    extraSel.el.style.fontSize = "24px";
    g.redraw();
  }

  // Graphs
  g.draw = (ctx, w, h) => {
    const grid = cssVar("--grid"), strong = cssVar("--grid-strong"), dim = cssVar("--text-dim");
    const L = 60, T = 16, W = w - L - 18, H = h - T - 40;
    ctx.font = "16px 'Barlow SC', sans-serif";
    const dark = document.documentElement.dataset.theme !== "light";
    const line = "#ffd21f";

    if (mod === "multi") {
      // Frequency view: three bands split at the crossovers, height = amount
      for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
        const x = L + freqToX(f, W);
        ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, T + H); ctx.stroke();
        ctx.fillStyle = dim; ctx.textAlign = "center"; ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), x, h - 12);
      }
      const xs = [L, L + freqToX(v("x1"), W), L + freqToX(v("x2"), W), L + W];
      const amounts = [v("lo"), v("mid"), v("hi")], colours = ["#ff7a12", "#3dff5a", "#1f9bff"], names = ["LOW", "MID", "HIGH"];
      for (let i = 0; i < 3; i++) {
        const top = T + H - (amounts[i] / 100) * H * 0.85;
        const gr = ctx.createLinearGradient(0, top, 0, T + H);
        gr.addColorStop(0, colours[i] + "aa"); gr.addColorStop(1, colours[i] + "10");
        ctx.fillStyle = gr; ctx.fillRect(xs[i] + 2, top, xs[i + 1] - xs[i] - 4, T + H - top);
        ctx.strokeStyle = colours[i]; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(xs[i] + 2, top); ctx.lineTo(xs[i + 1] - 2, top); ctx.stroke();
        ctx.fillStyle = cssVar("--text"); ctx.textAlign = "center"; ctx.font = "20px 'Barlow SC', sans-serif";
        ctx.fillText(names[i], (xs[i] + xs[i + 1]) / 2, top - 12);
        ctx.font = "16px 'Barlow SC', sans-serif";
      }
      ctx.fillStyle = dim; ctx.textAlign = "left"; ctx.fillText("Bar height = how much each band is compressed", L + 8, T + 22);
      return;
    }
    if (mod === "trans") {
      // Envelope sketch: original hit (grey) vs shaped (yellow)
      const att = v("att") / 100, sus = v("sus") / 100;
      for (let i = 0; i <= 8; i++) { const y = T + (i / 8) * H; ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + W, y); ctx.stroke(); }
      const env = (t, a, s) => { const rise = Math.min(1, t / 0.04); const decay = Math.exp(-(t - 0.04) * (6 - 3.5 * s)); return t < 0.04 ? rise * (1 + 0.6 * a) : (1 + 0.6 * a) * Math.exp(-(t - 0.04) * 25) * 0.45 + 0.55 * decay * (1 + 0.5 * s); };
      const draw = (a, s, col, width) => {
        ctx.strokeStyle = col; ctx.lineWidth = width; ctx.beginPath();
        for (let i = 0; i <= 300; i++) { const t = (i / 300) * 0.8; const y = T + H - clamp(env(t, a, s) / 1.8, 0, 1) * H; const x = L + (i / 300) * W; i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
        ctx.stroke();
      };
      draw(0, 0, dark ? "rgba(255,255,255,0.35)" : "rgba(20,30,50,0.35)", 2);
      ctx.save(); ctx.shadowColor = line; ctx.shadowBlur = 12; draw(att, sus, line, 3.5); ctx.restore();
      ctx.fillStyle = dim; ctx.textAlign = "left"; ctx.fillText("Grey = original hit, yellow = shaped (Attack = front, Sustain = tail)", L + 8, T + 22);
      return;
    }

    // Transfer curve: input level (across) -> output level (up)
    const lo = -60;
    const X = (db) => L + ((db - lo) / -lo) * W, Y = (db) => T + (1 - (db - lo) / -lo) * H;
    for (const db of [0, -6, -12, -24, -36, -48, -60]) {
      ctx.strokeStyle = db === 0 ? strong : grid; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(L, Y(db)); ctx.lineTo(L + W, Y(db)); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(X(db), T); ctx.lineTo(X(db), T + H); ctx.stroke();
      ctx.fillStyle = dim; ctx.textAlign = "right"; ctx.fillText(db === 0 ? "0" : "−" + -db, L - 10, Y(db) + 5);
      ctx.textAlign = "center"; ctx.fillText(db === 0 ? "0" : "−" + -db, X(db), h - 14);
    }
    ctx.save(); ctx.translate(16, T + H / 2); ctx.rotate(-Math.PI / 2); ctx.textAlign = "center"; ctx.fillText("OUT (dB)", 0, 0); ctx.restore();
    ctx.textAlign = "right"; ctx.fillText("IN (dB)", L + W - 4, T + H - 10);
    ctx.strokeStyle = dark ? "rgba(255,255,255,0.18)" : "rgba(20,30,50,0.2)";
    ctx.setLineDash([5, 5]); ctx.beginPath(); ctx.moveTo(X(lo), Y(lo)); ctx.lineTo(X(0), Y(0)); ctx.stroke(); ctx.setLineDash([]);

    let curve, threshold;
    if (mod === "comp") {
      const t = v("thr"), r = v("ratio"), mk = v("mk"), kneeName = store.get("dyn_comp_extra", "Soft");
      const knee = { Soft: 12, Medium: 6, Hard: 0 }[kneeName] ?? 12;
      threshold = t;
      curve = (x) => {
        let y;
        if (knee > 0 && Math.abs(x - t) <= knee / 2) y = x + ((1 / r - 1) * (x - t + knee / 2) ** 2) / (2 * knee);
        else y = x > t ? t + (x - t) / r : x;
        return y + mk;
      };
    } else if (mod === "gate") {
      const t = v("thr"), range = v("range"), exp = store.get("dyn_gate_extra", "Gate") === "Expander";
      threshold = t;
      curve = (x) => (x >= t ? x : exp ? t + (x - t) * 2 : x - range);
    } else {
      const gain = v("in"), ceil = v("ceil");
      threshold = ceil;
      curve = (x) => Math.min(x + gain, ceil);
    }
    const pts = [];
    for (let i = 0; i <= 240; i++) { const x = lo + (i / 240) * -lo; pts.push([X(x), Y(clamp(curve(x), lo - 6, 6))]); }
    const fill = ctx.createLinearGradient(0, T, 0, T + H);
    fill.addColorStop(0, "rgba(255,210,31,0.32)"); fill.addColorStop(1, "rgba(255,210,31,0.04)");
    ctx.save();
    ctx.beginPath(); ctx.rect(L, T, W, H); ctx.clip();
    ctx.fillStyle = fill; ctx.beginPath(); ctx.moveTo(L, T + H); pts.forEach(([x, y]) => ctx.lineTo(x, y)); ctx.lineTo(L + W, T + H); ctx.fill();
    ctx.shadowColor = line; ctx.shadowBlur = 14; ctx.strokeStyle = line; ctx.lineWidth = 3.5;
    ctx.beginPath(); pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y))); ctx.stroke();
    ctx.restore();
    // Threshold node + marks
    ctx.strokeStyle = "rgba(255,210,31,0.55)"; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.moveTo(X(threshold), T); ctx.lineTo(X(threshold), T + H); ctx.stroke();
    for (const x of [-48, -36, -24, -12].filter((x) => x !== threshold)) {
      ctx.fillStyle = line; ctx.beginPath(); ctx.arc(X(x), Y(clamp(curve(x), lo, 6)), 6, 0, Math.PI * 2); ctx.fill();
    }
    ctx.save(); ctx.shadowColor = line; ctx.shadowBlur = 16; ctx.fillStyle = line;
    ctx.beginPath(); ctx.arc(X(threshold), Y(clamp(curve(threshold), lo, 6)), 11, 0, Math.PI * 2); ctx.fill(); ctx.restore();
  };

  renderModule();
  app.on("theme", () => g.redraw());

  return {
    onFrame(f) {
      const l = f.peakL ?? -100, r = f.peakR ?? -100;
      meters[0].set(l); meters[1].set(r); meters[2].set(Math.max(l, r)); meters[3].set(Math.max(l, r));
      grFill.style.height = "0%";   // no processing yet: no gain reduction
    },
  };
}

// =============================================================================================
// SPACE: each effect has its own picture and its own, correct controls.

const SPACE = [
  { id: "reverb", label: "REVERB", icon: "cube", presets: ["Vocal Plate", "Bright Plate", "Dark Plate", "Chamber"],
    knobs: [["PRE-DELAY", 0, 200, 20, "lin", fmt.ms], ["DECAY", 0.2, 10, 1.8, "log", fmt.sec], ["DAMPING", 1000, 20000, 6500, "log", fmt.hz], ["WIDTH", 0, 100, 100, "lin", fmt.pct], ["MIX", 0, 100, 22, "lin", fmt.pct]] },
  { id: "delay", label: "DELAY", icon: "clock", presets: ["Analog Echo", "Tape Slap", "Ping-Pong 1/8", "Dotted 1/8", "Quarter Throw"],
    knobs: [["TIME", 0, 8, 3, "lin", (v) => ["1/32", "1/16", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2"][Math.round(v)]], ["FEEDBACK", 0, 95, 35, "lin", fmt.pct], ["TONE", 800, 20000, 6000, "log", fmt.hz], ["PING-PONG", 0, 100, 60, "lin", fmt.pct], ["MIX", 0, 100, 18, "lin", fmt.pct]] },
  { id: "chorus", label: "CHORUS", icon: "chorus", presets: ["Wide Vocal Chorus", "Subtle Doubler", "80s Chorus"],
    knobs: [["RATE", 0.05, 5, 0.8, "log", (v) => v.toFixed(2) + " Hz"], ["DEPTH", 0, 100, 40, "lin", fmt.pct], ["DELAY", 2, 30, 12, "lin", fmt.ms], ["WIDTH", 0, 100, 100, "lin", fmt.pct], ["MIX", 0, 100, 30, "lin", fmt.pct]] },
  { id: "phaser", label: "PHASER", icon: "phaser", presets: ["Slow Swirl", "Fast Phase", "Stereo Sweep"],
    knobs: [["RATE", 0.05, 10, 0.4, "log", (v) => v.toFixed(2) + " Hz"], ["DEPTH", 0, 100, 60, "lin", fmt.pct], ["FEEDBACK", 0, 90, 40, "lin", fmt.pct], ["CENTER", 200, 5000, 1000, "log", fmt.hz], ["MIX", 0, 100, 40, "lin", fmt.pct]] },
  { id: "hall", label: "HALL", icon: "hall", presets: ["Concert Hall", "Large Hall", "Cathedral"],
    knobs: [["PRE-DELAY", 0, 200, 35, "lin", fmt.ms], ["DECAY", 1, 12, 3.2, "log", fmt.sec], ["SIZE", 0, 100, 80, "lin", fmt.pct], ["DAMPING", 1000, 20000, 5000, "log", fmt.hz], ["MIX", 0, 100, 18, "lin", fmt.pct]] },
  { id: "room", label: "ROOM", icon: "room", presets: ["Studio Room", "Drum Room", "Small Booth"],
    knobs: [["PRE-DELAY", 0, 100, 8, "lin", fmt.ms], ["DECAY", 0.1, 3, 0.7, "log", fmt.sec], ["SIZE", 0, 100, 45, "lin", fmt.pct], ["EARLY / LATE", 0, 100, 60, "lin", (v) => Math.round(100 - v) + " / " + Math.round(v)], ["MIX", 0, 100, 20, "lin", fmt.pct]] },
  { id: "special", label: "SPECIAL FX", icon: "star", presets: ["Cosmic Drift", "Shimmer +12", "Reverse Swell", "Freeze Pad"],
    knobs: [["SHIFT", -12, 12, 12, "lin", (v) => fmt.signed(v, 0) + " st"], ["FEEDBACK", 0, 95, 55, "lin", fmt.pct], ["MOTION", 0, 100, 35, "lin", fmt.pct], ["TONE", 800, 20000, 8000, "log", fmt.hz], ["MIX", 0, 100, 25, "lin", fmt.pct]] },
];

export function buildSpace(root, app) {
  let fx = store.get("spaceFx", "reverb");
  moduleList(root, SPACE, 0, 0, 252, 98, 14, fx, (id) => { fx = id; store.set("spaceFx", id); render(); });

  const centre = el("div", "panel", root, box(266, 0, 792, 842));
  const title = el("div", "panel-title", centre, { top: "16px" });
  const tag = previewTag(centre, 0, 30);
  const scene = el("div", "", centre, { position: "absolute", left: "18px", top: "76px", width: "756px", height: "390px", borderRadius: "10px", overflow: "hidden", border: "1.5px solid var(--panel-edge)", boxShadow: "0 0 20px rgba(255,170,60,0.18), inset 0 0 30px rgba(0,0,0,0.5)" });
  const img = el("img", "", scene, { width: "100%", height: "100%", objectFit: "cover", display: "block" });
  const knobPanel = el("div", "card", centre, box(18, 484, 756, 342));
  let presetSel = null;

  const right = el("div", "panel", root, box(1072, 0, 340, 420));
  const eqTitle = el("div", "panel-title", right);
  eqTitle.textContent = "EQ";
  const eqg = graph(right, 18, 70, 304, 300);
  const eqNote = el("div", "", right, { position: "absolute", left: "18px", top: "378px", right: "18px", fontSize: "16px", color: "var(--text-dim)" });
  eqNote.textContent = "Drag the points: low cut and high cut on the effect";

  const duck = el("div", "panel", root, box(1072, 434, 340, 408));
  const dTitle = el("div", "panel-title", duck);
  dTitle.textContent = "DUCKING";
  const dLabel = el("div", "label", duck, { position: "absolute", left: "22px", top: "82px", fontSize: "17px", color: "var(--text-dim)" });
  dLabel.textContent = "DUCK UNDER";
  const duckSel = select(duck, 22, 110, 296, ["Lead Vox", "All vocals", "Off"], store.get("duckSrc", "Lead Vox"), (v) => store.set("duckSrc", v));
  app.on("bridge", () => duckSel.setOptions(["Off", "All vocals", ...((app.bridge && app.bridge.slots) || []).filter((s) => s.present).map((s) => s.title)]));
  const duckKnob = new Knob(duck, { x: 80, y: 172, w: 180, size: 112, label: "AMOUNT", id: "duckAmt", min: 0, max: 24, def: 12, step: 0.1, format: (v) => fmt.db(-v), labelGap: 10, valueGap: 10 });
  const dNote = el("div", "", duck, { position: "absolute", left: "22px", right: "22px", bottom: "14px", fontSize: "15px", lineHeight: "1.3", color: "var(--text-dim)" });
  dNote.textContent = "The effect gets quieter while the vocal sings, and blooms in the gaps.";
  void duckKnob;

  // Mini EQ (low cut + high cut) per effect
  const eqState = () => ({ lc: store.value(`sp_${fx}_lc`, 200), hc: store.value(`sp_${fx}_hc`, 9000) });
  const ePL = 8, ePW = 288, ePH = 284;
  eqg.draw = (ctx, w, h) => {
    const grid = cssVar("--grid");
    for (let i = 1; i < 6; i++) { ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo((i / 6) * w, 0); ctx.lineTo((i / 6) * w, h); ctx.stroke(); ctx.beginPath(); ctx.moveTo(0, (i / 6) * h); ctx.lineTo(w, (i / 6) * h); ctx.stroke(); }
    const { lc, hc } = eqState();
    const yOf = (db) => 50 + (-db / 30) * (h - 64);
    const resp = (f) => biquadDb("lowcut", lc, 0, 0.707, f) / 2 + biquadDb("highcut", hc, 0, 0.707, f) / 2;
    ctx.save();
    ctx.strokeStyle = document.documentElement.dataset.theme === "light" ? "#1a2233" : "#ffffff";
    ctx.lineWidth = 3; ctx.shadowColor = "rgba(25,211,255,0.8)"; ctx.shadowBlur = 10;
    ctx.beginPath();
    for (let i = 0; i <= 200; i++) { const x = ePL + (i / 200) * ePW; const y = clamp(yOf(resp(xToFreq(i / 200 * ePW, ePW))), 0, h); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
    ctx.stroke(); ctx.restore();
    for (const f of [lc, hc]) {
      const x = ePL + freqToX(f, ePW), y = yOf(resp(f));
      ctx.fillStyle = "#fff"; ctx.strokeStyle = "rgba(25,211,255,0.9)"; ctx.lineWidth = 3;
      ctx.beginPath(); ctx.arc(x, y, 9, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    }
    ctx.fillStyle = cssVar("--text-dim"); ctx.font = "15px 'Barlow SC', sans-serif"; ctx.textAlign = "left";
    ctx.fillText("Low cut " + fmt.hz(lc), 10, 20); ctx.textAlign = "right"; ctx.fillText("High cut " + fmt.hz(hc), w - 10, 20);
  };
  let eqDrag = null;
  eqg.canvas.addEventListener("pointerdown", (e) => {
    const r = eqg.canvas.getBoundingClientRect();
    const x = ((e.clientX - r.left) / r.width) * 304;
    const { lc, hc } = eqState();
    eqDrag = Math.abs(x - (ePL + freqToX(lc, ePW))) < Math.abs(x - (ePL + freqToX(hc, ePW))) ? "lc" : "hc";
    eqg.canvas.setPointerCapture(e.pointerId);
  });
  eqg.canvas.addEventListener("pointermove", (e) => {
    if (!eqDrag) return;
    const r = eqg.canvas.getBoundingClientRect();
    const f = Math.round(clamp(xToFreq(((e.clientX - r.left) / r.width) * 304 - ePL, ePW), 20, 20000));
    const s = eqState();
    if (eqDrag === "lc") store.setValue(`sp_${fx}_lc`, Math.min(f, s.hc / 1.5));
    else store.setValue(`sp_${fx}_hc`, Math.max(f, s.lc * 1.5));
    eqg.redraw();
  });
  eqg.canvas.addEventListener("pointerup", () => { eqDrag = null; });

  function render() {
    const d = SPACE.find((s) => s.id === fx);
    title.textContent = d.label;
    tag.style.left = (title.getBoundingClientRect().width ? 40 + d.label.length * 15 : 200) + "px";
    img.src = `assets/scene_${d.id}.jpg`;
    img.alt = d.label;
    if (presetSel) presetSel.el.remove();
    presetSel = select(centre, 474, 16, 300, d.presets, store.get(`sp_${fx}_preset`, d.presets[0]), (v) => store.set(`sp_${fx}_preset`, v));
    knobPanel.innerHTML = "";
    d.knobs.forEach(([label, min, max, value, curve, format], i) => {
      new Knob(knobPanel, { x: 4 + i * 150, y: 66, w: 148, size: 130, label, id: `sp_${fx}_${i}`, min, max, def: value, curve, format, step: d.id === "delay" && i === 0 ? 1 : undefined, labelGap: 16, valueGap: 14 });
    });
    eqg.redraw();
  }
  render();
  app.on("theme", () => eqg.redraw());
  return {};
}
