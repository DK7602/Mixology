// EQ, Dynamics and Space pages. EQ and Dynamics work on the target you pick (Full Mix or a track's Mixology
// Link, Milestone 5); Space goes live with the FX channels (next round), so it still shows "Preview".

import { el, box, icon, fmt, clamp, Knob, Meter, select, graph, freqToX, xToFreq, moduleList, previewTag, store, cssVar } from "./ui.js";
import { knobFor, eqBand, magnitudeDb, EQ_SHAPES, MODULE_NAMES } from "./proc.js";

const SR = 48000;
function biquadDb(type, f0, gainDb, q, f) {
  // RBJ cookbook magnitude response, evaluated at f (Space page's mini EQ).
  const A = Math.pow(10, gainDb / 40), w0 = (2 * Math.PI * f0) / SR, cw = Math.cos(w0), sw = Math.sin(w0), alpha = sw / (2 * q);
  let b0, b1, b2, a0, a1, a2;
  if (type === "lowcut") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }
  else { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; }   // highcut
  const w = (2 * Math.PI * f) / SR, c1 = Math.cos(w), c2 = Math.cos(2 * w), s1 = Math.sin(w), s2 = Math.sin(2 * w);
  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
  return 2 * 10 * Math.log10(Math.max((nr * nr + ni * ni) / (dr * dr + di * di), 1e-18));
}

// =============================================================================================
// EQ: six bands on the target (graph: drag a point = frequency and gain, wheel = width; cards below).

const EQ_BANDS = [
  { colour: "#ff2a2a", shapes: ["lowcut", "lowshelf", "bell"] },
  { colour: "#ffd21f", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#3dff5a", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#1f9bff", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#2f5bff", shapes: ["lowshelf", "bell", "highshelf"] },
  { colour: "#ff36e1", shapes: ["bell", "highshelf", "highcut"] },
];
const SHAPE_NAMES = { bell: "Bell", lowshelf: "Low shelf", highshelf: "High shelf", lowcut: "Low cut", highcut: "High cut" };

export function buildEq(root, app) {
  const proc = app.proc;
  const panel = el("div", "panel", root, box(0, 0, 1412, 842));
  const title = el("div", "panel-title", panel);
  title.textContent = "EQ";
  proc.powerButton(panel, 76, 20, () => "eq");
  const status = el("div", "proc-note", panel, { left: "190px", top: "26px", width: "560px", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" });

  const id = (i, field) => `eq${i + 1}_${field}`;
  const band = (i) => ({ type: Math.round(proc.get(id(i, "type"))), shape: EQ_SHAPES[Math.round(proc.get(id(i, "type")))], f: proc.get(id(i, "freq")), g: proc.get(id(i, "gain")), q: proc.get(id(i, "q")) });
  const isCut = (shape) => shape === "lowcut" || shape === "highcut";

  // Toolbar: character, target, analyzer
  const modes = ["Precision", "Smooth", "Natural"];
  const chips = el("div", "", panel, { position: "absolute", right: "330px", top: "16px", display: "flex", gap: "10px" });
  const chipEls = modes.map((m, k) => {
    const c = el("div", "chip", chips);
    c.textContent = m;
    c.title = { Precision: "Exact curves, steep 24 dB / octave cuts", Smooth: "Wider, gentler curves and 12 dB / octave cuts", Natural: "Console style: small moves broad, big moves narrower" }[m];
    c.addEventListener("click", () => proc.set("eq_mode", k));
    return c;
  });
  proc.targetSelect(panel, 1102, 16, 220);
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

  const designs = () => {
    const mode = Math.round(proc.get("eq_mode"));
    return EQ_BANDS.map((_, i) => { const b = band(i); return eqBand(b.type, b.f, b.g, b.q, mode, proc.sr); });
  };
  const responseOf = (ds) => (f) => ds.reduce((sum, d) => sum + (d ? d.stages * magnitudeDb(d.k, f, proc.sr) : 0), 0);
  const nodeY = (b) => gy(isCut(b.shape) ? 0 : b.g);

  g.draw = (ctx, w, h) => {
    const grid = cssVar("--grid"), strong = cssVar("--grid-strong"), dim = cssVar("--text-dim");
    const on = proc.isOn("eq");
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
    const bands = EQ_BANDS.map((_, i) => band(i));
    const response = responseOf(designs());
    const grad = ctx.createLinearGradient(PAD_L, 0, PAD_L + plotW, 0);
    [...bands.map((b, i) => ({ f: b.f, colour: EQ_BANDS[i].colour }))].sort((a, b) => a.f - b.f)
      .forEach((b) => grad.addColorStop(clamp((fx(b.f) - PAD_L) / plotW, 0, 1), b.colour));
    const pts = [];
    for (let i = 0; i <= 360; i++) {
      const x = PAD_L + (i / 360) * plotW;
      pts.push([x, gy(clamp(response(xToFreq(x - PAD_L, plotW)), -RANGE - 2, RANGE + 2))]);
    }
    const ink = on ? grad : (document.documentElement.dataset.theme === "light" ? "rgba(40,50,70,0.45)" : "rgba(200,210,230,0.4)");
    ctx.save();
    ctx.globalAlpha = on ? 0.28 : 0.12;
    ctx.fillStyle = ink;
    ctx.beginPath();
    ctx.moveTo(PAD_L, gy(0));
    pts.forEach(([x, y]) => ctx.lineTo(x, y));
    ctx.lineTo(PAD_L + plotW, gy(0));
    ctx.fill();
    ctx.restore();
    ctx.save();
    ctx.strokeStyle = ink;
    ctx.lineWidth = 3.5;
    if (on) { ctx.shadowColor = "rgba(255,255,255,0.4)"; ctx.shadowBlur = 10; }
    ctx.beginPath();
    pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
    ctx.stroke();
    ctx.restore();
    bands.forEach((b, i) => {
      const x = fx(b.f), y = nodeY(b), colour = EQ_BANDS[i].colour;
      const flat = !isCut(b.shape) && Math.abs(b.g) < 0.05;
      ctx.save();
      ctx.globalAlpha = on ? (flat ? 0.55 : 1) : 0.4;
      ctx.shadowColor = colour; ctx.shadowBlur = flat ? 6 : 18;
      ctx.fillStyle = colour;
      ctx.beginPath(); ctx.arc(x, y, 15, 0, Math.PI * 2); ctx.fill();
      ctx.shadowBlur = 0;
      ctx.strokeStyle = "rgba(255,255,255,0.9)"; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(x, y, 15, 0, Math.PI * 2); ctx.stroke();
      ctx.fillStyle = "#fff";
      ctx.beginPath(); ctx.arc(x, y, 4.5, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
    });
  };

  // Node dragging (x = frequency, y = gain, wheel = Q, double-click = flat)
  let drag = -1;
  const toLocal = (e) => {
    const r = g.canvas.getBoundingClientRect();
    return [((e.clientX - r.left) / r.width) * GW, ((e.clientY - r.top) / r.height) * GH];
  };
  const hit = (x, y) => EQ_BANDS.findIndex((_, i) => Math.hypot(fx(band(i).f) - x, nodeY(band(i)) - y) < 22);
  g.canvas.addEventListener("pointerdown", (e) => {
    const [x, y] = toLocal(e);
    drag = hit(x, y);
    if (drag >= 0) g.canvas.setPointerCapture(e.pointerId);
  });
  g.canvas.addEventListener("pointermove", (e) => {
    const [x, y] = toLocal(e);
    g.canvas.style.cursor = drag >= 0 || hit(x, y) >= 0 ? "grab" : "default";
    if (drag < 0) return;
    const b = band(drag);
    const changes = { [id(drag, "freq")]: Math.round(clamp(xToFreq(x - PAD_L, plotW), 20, 20000)) };
    if (!isCut(b.shape)) changes[id(drag, "gain")] = Math.round(clamp(((14 + plotH - y) / plotH) * 2 * RANGE - RANGE, -RANGE, RANGE) * 10) / 10;
    if (!proc.switchedOn("eq")) changes.eq_on = 1;
    proc.setMany(changes);
  });
  g.canvas.addEventListener("pointerup", () => { drag = -1; });
  g.canvas.addEventListener("dblclick", (e) => {
    const [x, y] = toLocal(e);
    const i = hit(x, y);
    if (i >= 0 && !isCut(band(i).shape)) proc.set(id(i, "gain"), 0, { autoOn: false });
  });
  g.canvas.addEventListener("wheel", (e) => {
    const [x, y] = toLocal(e);
    const i = hit(x, y);
    if (i < 0) return;
    e.preventDefault();
    proc.set(id(i, "q"), Math.round(clamp(band(i).q * (e.deltaY < 0 ? 1.08 : 1 / 1.08), 0.3, 10) * 100) / 100);
  }, { passive: false });

  // Band cards
  const cards = EQ_BANDS.map((b, i) => {
    const c = el("div", "card", panel, box(16 + i * 232, 524, 222, 302));
    el("div", "", c, { position: "absolute", left: "18px", top: "20px", width: "30px", height: "30px", borderRadius: "50%", background: b.colour, boxShadow: `0 0 12px ${b.colour}`, border: "2px solid rgba(255,255,255,0.8)" });
    const freq = el("div", "value", c, { position: "absolute", left: "64px", top: "16px", fontSize: "27px" });
    const gain = el("div", "value", c, { position: "absolute", left: "64px", top: "60px", fontSize: "27px" });
    const shapeBtns = b.shapes.map((s, k) => {
      const sb = el("button", "small-btn", c, { position: "absolute", left: "12px", top: 104 + k * 62 + "px", width: "62px", height: "52px", padding: "8px 10px", color: "var(--text)" });
      sb.innerHTML = icon(s, 'stroke-width="2"');
      sb.title = SHAPE_NAMES[s];
      sb.addEventListener("click", () => proc.set(id(i, "type"), EQ_SHAPES.indexOf(s)));
      return sb;
    });
    const q = new Knob(c, knobFor(proc, id(i, "q"), { x: 84, y: 112, w: 130, size: 104, colour: b.colour, format: (v) => "Q " + v.toFixed(1), valueGap: 10,
      onChange: (v) => proc.set(id(i, "q"), Math.round(v * 100) / 100) }));
    q.valueEl.style.fontSize = "26px";
    return { freq, gain, shapeBtns, q };
  });
  function update() {
    const mode = Math.round(proc.get("eq_mode"));
    chipEls.forEach((x, k) => x.classList.toggle("on", k === mode));
    EQ_BANDS.forEach((def, i) => {
      const b = band(i), cd = cards[i];
      cd.freq.textContent = fmt.hz(b.f);
      cd.gain.textContent = isCut(b.shape) ? (mode === 1 ? "12" : "24") + " dB/oct" : fmt.db(b.g);
      cd.shapeBtns.forEach((sb, k) => {
        const on = def.shapes[k] === b.shape;
        sb.style.borderColor = on ? def.colour : "";
        sb.style.boxShadow = on ? `0 0 10px ${def.colour}` : "";
        sb.style.color = on ? def.colour : "var(--text)";
      });
      if (!cd.q.dragging && Math.abs(cd.q.value - b.q) > 0.005) cd.q.set(b.q);
    });
    const target = proc.key === "mix" ? "the full mix (Stereo Out)" : `'${proc.name}'`;
    status.textContent = !proc.ok ? `${proc.name}: its Mixology Link isn't answering (track removed?). Pick another target.`
      : proc.switchedOn("eq") ? (proc.isOn("eq") ? `Working on ${target}` : `On, but the processing of ${target} is switched off in its Link window`)
      : `Off on ${target}. Drag a point (or press ON) to start`;
    g.redraw();
  }
  proc.on((why) => { if (why === "values" || why === "target") update(); });
  update();
  app.on("theme", () => g.redraw());

  return {
    onFrame(f) {
      const s = proc.key === "mix" ? f.spectrum : proc.spectrum;
      if (showSpectrum && s) {
        spectrum = spectrum && spectrum.length === s.length ? spectrum.map((v, i) => 0.7 * v + 0.3 * s[i]) : s.slice();
        g.redraw();
      } else if (spectrum && !s) { spectrum = null; g.redraw(); }
    },
  };
}

// =============================================================================================
// DYNAMICS

const DYN_MODULES = [
  { id: "comp", label: "COMPRESSOR", icon: "compressor", lamp: true },
  { id: "multi", label: "MULTIBAND", icon: "bars", lamp: true },
  { id: "trans", label: "TRANSIENT", icon: "transient", lamp: true },
  { id: "gate", label: "GATE / EXPAND", icon: "gate", lamp: true },
  { id: "limit", label: "LIMITER", icon: "clock", lamp: true },
];

// Each knob: the control it moves, its label and how it reads.
const DYN_KNOBS = {
  comp: [
    { id: "comp_thr", label: "THRESHOLD", format: (v) => fmt.db(v) },
    { id: "comp_ratio", label: "RATIO", format: (v) => fmt.ratio(v) },
    { id: "comp_att", label: "ATTACK", format: (v) => fmt.ms(v) },
    { id: "comp_rel", label: "RELEASE", format: (v) => fmt.ms(v) },
    { id: "comp_makeup", label: "MAKEUP", format: (v) => fmt.db(v) },
  ],
  multi: [
    { id: "multi_x1", label: "LOW / MID", format: (v) => fmt.hz(v) },
    { id: "multi_x2", label: "MID / HIGH", format: (v) => fmt.hz(v) },
    { id: "multi_low", label: "LOW", format: (v) => fmt.pct(v) },
    { id: "multi_mid", label: "MID", format: (v) => fmt.pct(v) },
    { id: "multi_high", label: "HIGH", format: (v) => fmt.pct(v) },
  ],
  trans: [
    { id: "trans_attack", label: "ATTACK", bipolar: true, format: (v) => fmt.signed(v, 0) + "%" },
    { id: "trans_sustain", label: "SUSTAIN", bipolar: true, format: (v) => fmt.signed(v, 0) + "%" },
    { id: "trans_speed", label: "SPEED", format: (v) => fmt.ms(v) },
    { id: "trans_out", label: "OUTPUT", bipolar: true, format: (v) => fmt.db(v) },
    { id: "trans_mix", label: "MIX", format: (v) => fmt.pct(v) },
  ],
  gate: [
    { id: "gate_thr", label: "THRESHOLD", format: (v) => fmt.db(v) },
    { id: "gate_range", label: "RANGE", format: (v) => fmt.db(-v) },
    { id: "gate_att", label: "ATTACK", format: (v) => fmt.ms(v) },
    { id: "gate_hold", label: "HOLD", format: (v) => fmt.ms(v) },
    { id: "gate_rel", label: "RELEASE", format: (v) => fmt.ms(v) },
  ],
  limit: [
    { id: "lim_in", label: "INPUT", format: (v) => fmt.db(v) },
    { id: "lim_ceil", label: "CEILING", format: (v) => fmt.db(v) },
    { id: "lim_rel", label: "RELEASE", format: (v) => fmt.ms(v) },
    { id: "lim_look", label: "LOOKAHEAD", format: (v) => v.toFixed(1) + " ms" },
    { id: "lim_char", label: "CHARACTER", format: (v) => (v < 50 ? "Clean " : "Punchy ") + Math.round(v) + "%" },
  ],
};
const DYN_EXTRA = {
  comp: { label: "KNEE", id: "comp_knee" },
  multi: { label: "BANDS", id: "multi_bands" },
  trans: { label: "FOCUS", id: "trans_focus" },
  gate: { label: "MODE", id: "gate_mode" },
  limit: { label: "DETECT", id: "lim_detect" },
};
const DYN_HELP = {
  comp: "Evens out the level: whatever goes over the Threshold is turned down by the Ratio. Attack = how fast it grabs, Release = how fast it lets go. Makeup brings the level back up.",
  multi: "Three compressors, one per range (lows, mids, highs). Each knob says how much that range is held together: it finds its own threshold. Good for a boomy low end or harsh highs.",
  trans: "Shapes every hit. Attack + makes the front snap, − softens it. Sustain + brings up the tail (ring, room), − tightens it.",
  gate: "Turns the track down between notes (noise, bleed). Gate closes by the full Range; Expander turns quiet parts down gently.",
  limit: "Catches peaks so nothing passes the Ceiling. Adds 5 ms of delay while on (Cubase lines it up). On the full mix, Masterology does the final limiting: use this only as a safety.",
};
const GR_OF = { comp: (m) => m.comp, multi: (m) => Math.max(...m.multi), trans: (m) => Math.abs(m.trans), gate: (m) => m.gate, limit: (m) => m.limit };

export function buildDynamics(root, app) {
  const proc = app.proc;
  let mod = store.get("dynModule", "comp");
  const list = moduleList(root, DYN_MODULES, 0, 0, 252, 92, 16, mod, (id) => { mod = id; store.set("dynModule", id); renderModule(); power.draw(); });
  const helpCard = el("div", "card", root, box(0, 544, 252, 298));
  const help = el("div", "proc-note", helpCard, { left: "16px", right: "16px", top: "16px" });
  const resetBtn = el("button", "small-btn", helpCard, { position: "absolute", left: "16px", right: "16px", bottom: "16px", height: "42px", fontSize: "17px" });
  resetBtn.textContent = "Reset this module";
  resetBtn.addEventListener("click", () => proc.reset(mod));

  const top = el("div", "panel", root, box(266, 0, 1146, 556));
  const power = proc.powerButton(top, 22, 16, () => mod);
  const note = el("div", "proc-note", top, { left: "140px", top: "22px", width: "560px", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" });
  proc.targetSelect(top, 836, 14, 290, { dot: "#1fd1ff" });

  const GW = 790, GH = 474;
  const g = graph(top, 18, 66, GW, GH);
  const meters = [new Meter(top, 858, 94, 20, 396, [-48, 0]), new Meter(top, 894, 94, 20, 396, [-48, 0]), new Meter(top, 990, 94, 20, 396, [-48, 0]), new Meter(top, 1026, 94, 20, 396, [-48, 0])];
  const grMeter = el("div", "meter", top, box(1080, 94, 20, 396));
  const grFill = el("div", "", grMeter, { position: "absolute", left: 0, right: 0, top: 0, height: "0%", background: "linear-gradient(180deg,#ffb020,#ff5a10)", boxShadow: "0 0 10px rgba(255,140,30,0.6)" });
  const grText = el("div", "", top, { position: "absolute", left: "1062px", top: "60px", width: "60px", textAlign: "center", fontSize: "17px", fontWeight: "600", color: "#ffa21f" });
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
  let inLevel = -100, grNow = 0;

  const v = (id) => proc.get(id);

  function renderModule() {
    knobHost.innerHTML = "";
    knobs = {};
    DYN_KNOBS[mod].forEach((k, i) => {
      knobs[k.id] = new Knob(knobHost, knobFor(proc, k.id, { label: k.label, format: k.format, bipolar: k.bipolar, x: 16 + i * 176, y: 28, w: 170, size: 118, labelGap: 14, valueGap: 12,
        onChange: (val) => proc.set(k.id, val) }));
    });
    const ex = DYN_EXTRA[mod], spec = proc.spec(ex.id);
    extraLabel.textContent = ex.label;
    if (extraSel) extraSel.el.remove();
    extraSel = select(extraCard, 26, 98, 168, spec.choices, spec.choices[Math.round(v(ex.id))], (val) => proc.set(ex.id, spec.choices.indexOf(val)), { h: 64 });
    extraSel.el.style.fontSize = "24px";
    help.textContent = DYN_HELP[mod];
    list.set(mod);
    sync();
  }

  function sync() {
    for (const [id, k] of Object.entries(knobs)) if (!k.dragging && Math.abs(k.value - v(id)) > 1e-3) k.set(v(id));
    const ex = DYN_EXTRA[mod];
    if (extraSel) extraSel.set(proc.spec(ex.id).choices[Math.round(v(ex.id))]);
    list.lamps((m) => proc.switchedOn(m), (m) => proc.running(m));
    const target = proc.key === "mix" ? "the full mix" : `'${proc.name}'`;
    note.textContent = !proc.ok ? `${proc.name}: its Mixology Link isn't answering. Pick another target.`
      : proc.switchedOn(mod) ? `${MODULE_NAMES[mod]} on ${target}` + (mod === "limit" && proc.latencyMs > 0 ? ` · ${fmt.num(proc.latencyMs, 1)} ms lookahead` : "")
      : `${MODULE_NAMES[mod]} is off on ${target}. Turn a knob (or press ON) to start`;
    g.redraw();
  }
  proc.on((why) => { if (why === "values" || why === "target") sync(); });

  // Graphs
  g.draw = (ctx, w, h) => {
    const grid = cssVar("--grid"), strong = cssVar("--grid-strong"), dim = cssVar("--text-dim");
    const L = 60, T = 16, W = w - L - 18, H = h - T - 40;
    ctx.font = "16px 'Barlow SC', sans-serif";
    const dark = document.documentElement.dataset.theme !== "light";
    const on = proc.isOn(mod);
    const line = on ? "#ffd21f" : (dark ? "rgba(220,225,235,0.45)" : "rgba(40,50,70,0.45)");

    if (mod === "multi") {
      for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
        const x = L + freqToX(f, W);
        ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo(x, T); ctx.lineTo(x, T + H); ctx.stroke();
        ctx.fillStyle = dim; ctx.textAlign = "center"; ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), x, h - 12);
      }
      const three = Math.round(v("multi_bands")) === 0;
      const xs = three ? [L, L + freqToX(v("multi_x1"), W), L + freqToX(Math.max(v("multi_x2"), v("multi_x1") * 1.5), W), L + W] : [L, L + freqToX(v("multi_x1"), W), L + W];
      const amounts = three ? [v("multi_low"), v("multi_mid"), v("multi_high")] : [v("multi_low"), v("multi_high")];
      const colours = three ? ["#ff7a12", "#3dff5a", "#1f9bff"] : ["#ff7a12", "#1f9bff"], names = three ? ["LOW", "MID", "HIGH"] : ["LOW", "HIGH"];
      const gr = proc.meters ? (three ? proc.meters.multi : [proc.meters.multi[0], proc.meters.multi[2]]) : null;
      for (let i = 0; i < amounts.length; i++) {
        const topY = T + H - (amounts[i] / 100) * H * 0.85;
        const fill = ctx.createLinearGradient(0, topY, 0, T + H);
        fill.addColorStop(0, colours[i] + (on ? "aa" : "44")); fill.addColorStop(1, colours[i] + "10");
        ctx.fillStyle = fill; ctx.fillRect(xs[i] + 2, topY, xs[i + 1] - xs[i] - 4, T + H - topY);
        ctx.strokeStyle = colours[i]; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(xs[i] + 2, topY); ctx.lineTo(xs[i + 1] - 2, topY); ctx.stroke();
        ctx.fillStyle = cssVar("--text"); ctx.textAlign = "center"; ctx.font = "20px 'Barlow SC', sans-serif";
        const label = names[i] + (on && gr && gr[i] > 0.05 ? `  −${gr[i].toFixed(1)} dB` : "");
        ctx.fillText(label, (xs[i] + xs[i + 1]) / 2, topY - 12);
        ctx.font = "16px 'Barlow SC', sans-serif";
      }
      ctx.fillStyle = dim; ctx.textAlign = "left"; ctx.fillText("Bar height = how much each range is held together (live: how much it takes off)", L + 8, T + 22);
      return;
    }
    if (mod === "trans") {
      const att = v("trans_attack") / 100, sus = v("trans_sustain") / 100;
      for (let i = 0; i <= 8; i++) { const y = T + (i / 8) * H; ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + W, y); ctx.stroke(); }
      const env = (t, a, s) => { const rise = Math.min(1, t / 0.04); const decay = Math.exp(-(t - 0.04) * (6 - 3.5 * s)); return t < 0.04 ? rise * (1 + 0.6 * a) : (1 + 0.6 * a) * Math.exp(-(t - 0.04) * 25) * 0.45 + 0.55 * decay * (1 + 0.5 * s); };
      const draw = (a, s, col, width) => {
        ctx.strokeStyle = col; ctx.lineWidth = width; ctx.beginPath();
        for (let i = 0; i <= 300; i++) { const t = (i / 300) * 0.8; const y = T + H - clamp(env(t, a, s) / 1.8, 0, 1) * H; const x = L + (i / 300) * W; i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
        ctx.stroke();
      };
      draw(0, 0, dark ? "rgba(255,255,255,0.35)" : "rgba(20,30,50,0.35)", 2);
      ctx.save(); if (on) { ctx.shadowColor = line; ctx.shadowBlur = 12; } draw(att, sus, line, 3.5); ctx.restore();
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
      const t = v("comp_thr"), r = v("comp_ratio"), mk = v("comp_makeup");
      const knee = [12, 6, 0][Math.round(v("comp_knee"))] ?? 12;
      threshold = t;
      curve = (x) => {
        let y;
        if (knee > 0 && Math.abs(x - t) <= knee / 2) y = x + ((1 / r - 1) * (x - t + knee / 2) ** 2) / (2 * knee);
        else y = x > t ? t + (x - t) / r : x;
        return y + mk;
      };
    } else if (mod === "gate") {
      const t = v("gate_thr"), range = v("gate_range"), exp = Math.round(v("gate_mode")) === 1;
      threshold = t;
      curve = (x) => (x >= t ? x : exp ? x - Math.min(range, t - x) : x - range);
    } else {
      const gain = v("lim_in"), ceil = v("lim_ceil");
      threshold = ceil;
      curve = (x) => Math.min(x + gain, ceil);
    }
    const pts = [];
    for (let i = 0; i <= 240; i++) { const x = lo + (i / 240) * -lo; pts.push([X(x), Y(clamp(curve(x), lo - 6, 6))]); }
    const fill = ctx.createLinearGradient(0, T, 0, T + H);
    fill.addColorStop(0, on ? "rgba(255,210,31,0.32)" : "rgba(160,170,190,0.16)"); fill.addColorStop(1, "rgba(255,210,31,0.04)");
    ctx.save();
    ctx.beginPath(); ctx.rect(L, T, W, H); ctx.clip();
    ctx.fillStyle = fill; ctx.beginPath(); ctx.moveTo(L, T + H); pts.forEach(([x, y]) => ctx.lineTo(x, y)); ctx.lineTo(L + W, T + H); ctx.fill();
    if (on) { ctx.shadowColor = line; ctx.shadowBlur = 14; }
    ctx.strokeStyle = line; ctx.lineWidth = 3.5;
    ctx.beginPath(); pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y))); ctx.stroke();
    ctx.restore();
    ctx.strokeStyle = on ? "rgba(255,210,31,0.55)" : "rgba(160,170,190,0.4)"; ctx.lineWidth = 1.5;
    ctx.beginPath(); ctx.moveTo(X(threshold), T); ctx.lineTo(X(threshold), T + H); ctx.stroke();
    ctx.save(); if (on) { ctx.shadowColor = line; ctx.shadowBlur = 16; } ctx.fillStyle = line;
    ctx.beginPath(); ctx.arc(X(threshold), Y(clamp(curve(threshold), lo, 6)), 11, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    // Where the sound is right now: a white ball riding the curve.
    if (inLevel > lo) {
      const x = clamp(inLevel, lo, 0), y = clamp(on ? curve(x) : x, lo, 6);
      const dot = dark ? "#fff" : "#1a2233";
      ctx.save(); ctx.fillStyle = dot; ctx.shadowColor = dot; ctx.shadowBlur = dark ? 12 : 4;
      ctx.beginPath(); ctx.arc(X(x), Y(y), 7, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    }
  };

  renderModule();
  app.on("theme", () => g.redraw());

  return {
    onFrame() {
      const m = proc.meters;
      if (m) {
        meters[0].set(m.outL); meters[1].set(proc.channels === 1 ? m.outL : m.outR);
        meters[2].set(Math.max(m.inL, m.inR)); meters[3].set(Math.max(m.outL, m.outR));
        inLevel = Math.max(m.inL, m.inR);
        const gr = proc.isOn(mod) ? GR_OF[mod](m) : 0;
        grNow = gr > grNow ? gr : Math.max(gr, grNow - 0.6);
      } else {
        meters.forEach((x) => x.set(-100));
        inLevel = -100;
        grNow = Math.max(0, grNow - 0.6);
      }
      grFill.style.height = clamp(grNow / 24, 0, 1) * 100 + "%";
      grText.textContent = grNow >= 0.1 ? (mod === "trans" && m && m.trans > 0 ? "+" : "−") + grNow.toFixed(1) : "";
      list.lamps((x) => proc.switchedOn(x), (x) => proc.running(x));
      if (mod !== "trans") g.redraw();
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
