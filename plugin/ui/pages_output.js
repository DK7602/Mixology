// Tone, Stereo and Master pages. Their meters and scopes are live (what Mixology hears on its channel);
// the processing controls are previews until Milestone 5.

import { el, box, icon, fmt, clamp, Knob, Meter, select, graph, freqToX, moduleList, previewTag, store, cssVar } from "./ui.js";
import { PRESETS } from "./sheets.js";

function knobRow(host, defs, prefix, onChange) {
  host.innerHTML = "";
  const n = defs.length;
  const cw = Math.floor((host.clientWidth || parseFloat(host.style.width)) / n);
  return defs.map((k, i) => {
    const card = el("div", "card", host, box(i * cw + 4, 0, cw - 8, parseFloat(host.style.height)));
    return new Knob(card, { ...k, id: `${prefix}_${k.id}`, x: (cw - 8 - Math.min(cw, 170)) / 2, y: 24, w: Math.min(cw, 170), size: Math.min(112, cw - 46), def: k.value, labelGap: 14, valueGap: 12, onChange });
  });
}

// =============================================================================================
// TONE

const TONE_MODULES = [
  { id: "balance", label: "TONE BALANCE", icon: "gauge" },
  { id: "loud", label: "LOUDNESS", icon: "speaker" },
  { id: "clip", label: "CLIP CONTROL", icon: "clip" },
  { id: "harm", label: "HARMONIC", icon: "harmonic" },
  { id: "analog", label: "ANALOG TOUCH", icon: "analog" },
  { id: "final", label: "FINAL TOUCH", icon: "finalTouch" },
];
const dbKnob = (id, label, value) => ({ id, label, min: -6, max: 6, value, bipolar: true, step: 0.1, format: (v) => fmt.db(v) });
const pctKnob = (id, label, value) => ({ id, label, min: 0, max: 100, value, format: (v) => fmt.pct(v) });
const TONE_KNOBS = {
  balance: [dbKnob("warmth", "WARMTH", -0.8), dbKnob("presence", "PRESENCE", 1.2), dbKnob("air", "AIR", 1.5), dbKnob("glue", "GLUE", 0.6), dbKnob("punch", "PUNCH", 1.0)],
  loud: [{ id: "trim", label: "TRIM", min: -12, max: 12, value: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v) }, pctKnob("density", "DENSITY", 20), dbKnob("lowend", "LOW END", 0), dbKnob("top", "TOP END", 0)],
  clip: [{ id: "thr", label: "THRESHOLD", min: -12, max: 0, value: -2, step: 0.1, format: (v) => fmt.db(v) }, pctKnob("soft", "SOFTNESS", 50), { id: "os", label: "OVERSAMPLE", min: 0, max: 3, value: 2, step: 1, format: (v) => ["1x", "2x", "4x", "8x"][Math.round(v)] }, pctKnob("mix", "MIX", 100)],
  harm: [pctKnob("drive", "DRIVE", 25), { id: "eo", label: "EVEN / ODD", min: 0, max: 100, value: 60, format: (v) => Math.round(v) + " / " + Math.round(100 - v) }, dbKnob("tone", "TONE", 0), pctKnob("mix", "MIX", 50)],
  analog: [pctKnob("tape", "TAPE", 30), pctKnob("console", "CONSOLE", 20), pctKnob("hiss", "NOISE", 0), pctKnob("mix", "MIX", 100)],
  final: [{ id: "width", label: "WIDTH", min: 0, max: 200, value: 100, format: (v) => fmt.pct(v) }, dbKnob("air", "AIR", 0), dbKnob("glue", "GLUE", 0), { id: "out", label: "OUTPUT", min: -12, max: 12, value: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v) }],
};

export function buildTone(root, app) {
  let mod = store.get("toneModule", "balance");
  moduleList(root, TONE_MODULES, 0, 0, 252, 104, 18, mod, (id) => { mod = id; store.set("toneModule", id); renderKnobs(); });

  const top = el("div", "panel", root, box(266, 0, 870, 556));
  previewTag(top, 22, 18);
  const legend = el("div", "", top, { position: "absolute", left: "150px", top: "22px", display: "flex", gap: "22px", fontSize: "17px" });
  const targetSel = select(top, 650, 14, 200, ["Full Mix"], store.get("toneTarget", "Full Mix"), (v) => store.set("toneTarget", v));
  app.on("bridge", () => targetSel.setOptions(["Full Mix", ...((app.bridge && app.bridge.slots) || []).filter((s) => s.present).map((s) => s.title)]));
  const g = graph(top, 18, 70, 834, 470);
  let mix = null;

  // Genre target curves (dB vs a flat, tilted reference): bass weight, low-mid scoop, top sheen.
  function target(f) {
    const p = PRESETS[app.preset] || PRESETS[0];
    const lf = Math.log2(f / 1000);
    const bass = p.bass * Math.exp(-Math.pow(Math.log2(f / 55), 2) / 2.2);
    const scoop = -p.scoop * Math.exp(-Math.pow(Math.log2(f / 350), 2) / 1.2);
    const top = p.air * clamp((lf - 2) / 3, 0, 1);
    return 2 + bass + scoop + top - 0.6 * lf;
  }

  g.draw = (ctx, w, h) => {
    const grid = cssVar("--grid"), strong = cssVar("--grid-strong"), dim = cssVar("--text-dim");
    const L = 50, B = 36, W = w - L - 12, H = h - B - 12;
    const Y = (db) => 12 + (1 - (db + 12) / 24) * H;
    ctx.font = "16px 'Barlow SC', sans-serif";
    for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
      const x = L + freqToX(f, W);
      ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo(x, 12); ctx.lineTo(x, 12 + H); ctx.stroke();
      ctx.fillStyle = dim; ctx.textAlign = "center"; ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), x, h - 10);
    }
    for (const db of [12, 6, 0, -6, -12]) {
      ctx.strokeStyle = db === 0 ? strong : grid; ctx.beginPath(); ctx.moveTo(L, Y(db)); ctx.lineTo(L + W, Y(db)); ctx.stroke();
      ctx.fillStyle = dim; ctx.textAlign = "right"; ctx.fillText(db > 0 ? "+" + db : String(db), L - 8, Y(db) + 5);
    }
    const dark = document.documentElement.dataset.theme !== "light";
    // Your mix (smoothed), filled
    if (mix) {
      const n = mix.length;
      const pts = mix.map((v, i) => [L + (i / (n - 1)) * W, Y(clamp(v, -14, 14))]);
      const fill = ctx.createLinearGradient(0, 12, 0, 12 + H);
      fill.addColorStop(0, dark ? "rgba(150,170,200,0.30)" : "rgba(40,60,100,0.18)");
      fill.addColorStop(1, dark ? "rgba(150,170,200,0.02)" : "rgba(40,60,100,0.02)");
      ctx.fillStyle = fill; ctx.beginPath(); ctx.moveTo(L, 12 + H); pts.forEach(([x, y]) => ctx.lineTo(x, y)); ctx.lineTo(L + W, 12 + H); ctx.fill();
      ctx.strokeStyle = dark ? "#ffffff" : "#1a2233"; ctx.lineWidth = 2;
      ctx.beginPath(); pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y))); ctx.stroke();
    }
    // Target
    ctx.save();
    ctx.strokeStyle = "#ff2a2a"; ctx.lineWidth = 3; ctx.shadowColor = "#ff2a2a"; ctx.shadowBlur = 12;
    ctx.beginPath();
    for (let i = 0; i <= 200; i++) { const f = 20 * Math.pow(1000, i / 200); const x = L + (i / 200) * W, y = Y(target(f)); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
    ctx.stroke(); ctx.restore();
  };
  function renderLegend() {
    legend.innerHTML = `<span><span style="display:inline-block;width:22px;height:3px;background:#ff2a2a;vertical-align:5px;margin-right:8px;box-shadow:0 0 6px #ff2a2a"></span>Target: ${(PRESETS[app.preset] || PRESETS[0]).name}</span>
      <span><span style="display:inline-block;width:22px;height:3px;background:var(--text);vertical-align:5px;margin-right:8px"></span>Your mix</span>`;
  }
  app.on("preset", () => { renderLegend(); g.redraw(); });
  app.on("theme", () => g.redraw());
  renderLegend();

  const bottom = el("div", "", root, box(266, 570, 870, 272));
  function renderKnobs() { knobRow(bottom, TONE_KNOBS[mod], "tone_" + mod); }
  renderKnobs();

  // Right: loudness (live)
  const right = el("div", "panel", root, box(1150, 0, 262, 842));
  const lt = el("div", "", right, { position: "absolute", left: "0", right: "0", top: "18px", textAlign: "center", fontSize: "26px", fontWeight: "600" });
  lt.textContent = "LOUDNESS";
  const big = el("div", "", right, { position: "absolute", left: "0", right: "0", top: "56px", textAlign: "center", fontSize: "62px", fontWeight: "600", lineHeight: "1" });
  const unit = el("div", "", right, { position: "absolute", left: "0", right: "0", top: "124px", textAlign: "center", fontSize: "22px" });
  unit.textContent = "LUFS (short-term)";
  const mL = new Meter(right, 92, 180, 22, 470, [-48, 0]), mS = new Meter(right, 126, 180, 22, 470, [-48, 0]), mR = new Meter(right, 160, 180, 22, 470, [-48, 0]);
  const sc = el("div", "", right, { position: "absolute", left: "40px", top: "172px", width: "40px", height: "480px", fontSize: "16px", color: "var(--text-dim)" });
  for (const db of [0, -6, -12, -24, -36, -48]) { const l = el("div", "", sc, { position: "absolute", right: "0", top: ((-db / 48) * 470) + "px" }); l.textContent = db === 0 ? "0" : "−" + -db; }
  const ml = el("div", "", right, { position: "absolute", left: "88px", top: "660px", width: "110px", display: "flex", justifyContent: "space-between", fontSize: "18px", color: "var(--text-dim)" });
  ml.innerHTML = "<span>L</span><span>S</span><span>R</span>";
  const stat = el("div", "", right, { position: "absolute", left: "22px", right: "22px", top: "712px", height: "66px", borderRadius: "12px", display: "grid", placeItems: "center", fontSize: "24px", fontWeight: "600", textAlign: "center", lineHeight: "1.1" });
  const statNote = el("div", "", right, { position: "absolute", left: "16px", right: "16px", top: "786px", textAlign: "center", fontSize: "15px", color: "var(--text-dim)" });
  statNote.textContent = "Info only: Masterology sets the final loudness";

  return {
    onFrame(f) {
      if (f.spectrum) {
        const s = f.spectrum, k = 6;
        const sm = s.map((_, i) => { let a = 0, c = 0; for (let j = Math.max(0, i - k); j <= Math.min(s.length - 1, i + k); j++) { a += s[j]; c++; } return a / c; });
        const mean = sm.reduce((a, b) => a + b, 0) / sm.length;
        const rel = sm.map((v) => v - mean);
        mix = mix && mix.length === rel.length ? mix.map((v, i) => 0.85 * v + 0.15 * rel[i]) : rel;
        g.redraw();
      }
      const st = f.shortTerm ?? -100, tp = f.truePeak ?? -100;
      big.textContent = fmt.lufs(st);
      mL.set(f.peakL ?? -100); mR.set(f.peakR ?? -100); mS.set(st);
      const silent = st <= -70;
      const hot = tp > -1, warm = tp > -3;
      stat.textContent = silent ? "No audio" : hot ? "Too hot for mastering" : warm ? "A little hot" : "Headroom OK";
      const c = silent ? "var(--text-faint)" : hot ? "var(--red)" : warm ? "var(--amber)" : "var(--green)";
      stat.style.color = c;
      stat.style.border = `2px solid ${c}`;
      stat.style.boxShadow = silent ? "" : `0 0 14px ${c}, inset 0 0 12px rgba(0,0,0,0.25)`;
    },
  };
}

// =============================================================================================
// STEREO

const STEREO_MODULES = [
  { id: "width", label: "STEREO WIDTH", icon: "rings" },
  { id: "pan", label: "PAN ASSIST", icon: "pan" },
  { id: "mono", label: "MONO CHECK", icon: "mono" },
  { id: "freq", label: "FREQUENCY WIDTH", icon: "freq" },
  { id: "corr", label: "CORRELATION", icon: "corr" },
];

export function buildStereo(root, app) {
  let mod = store.get("stereoModule", "width");
  moduleList(root, STEREO_MODULES, 0, 0, 252, 104, 18, mod, (id) => {
    mod = id; store.set("stereoModule", id);
    if (id === "mono" || id === "pan" || id === "freq") app.toast({ mono: "Mono Check (listen in mono) goes live with the processing in Milestone 5. The scope and correlation below are live now.", pan: "Pan Assist arrives with Auto Mix (Milestone 4).", freq: "Frequency Width view arrives in Milestone 3." }[id]);
  });

  const centre = el("div", "panel", root, box(266, 0, 790, 842));
  const scope = graph(centre, 18, 18, 754, 640);
  const corrBar = el("div", "", centre, { position: "absolute", left: "30px", right: "30px", top: "690px", height: "24px", borderRadius: "6px", background: "linear-gradient(90deg,#ff1d1d,#ff7a12,#ffe11f,#3dff5a,#19d3ff,#1f6bff,#ff2bf0)", boxShadow: "0 0 14px rgba(120,180,255,0.35)" });
  const corrMark = el("div", "", corrBar, { position: "absolute", top: "-8px", width: "6px", height: "40px", borderRadius: "3px", background: "#fff", boxShadow: "0 0 10px #fff, 0 0 0 1.5px #000", left: "50%" });
  const corrLab = el("div", "", centre, { position: "absolute", left: "30px", right: "30px", top: "726px", display: "flex", justifyContent: "space-between", fontSize: "26px", fontWeight: "500" });
  corrLab.innerHTML = "<span>−1</span><span>0</span><span>+1</span>";
  const corrText = el("div", "", centre, { position: "absolute", left: "0", right: "0", top: "772px", textAlign: "center", fontSize: "19px", color: "var(--text-dim)" });

  const right = el("div", "panel", root, box(1070, 0, 342, 842));
  previewTag(right, 22, 22);
  const src = select(right, 22, 60, 298, ["Full Mix"], store.get("stTarget", "Full Mix"), (v) => store.set("stTarget", v));
  app.on("bridge", () => src.setOptions(["Full Mix", ...((app.bridge && app.bridge.slots) || []).filter((s) => s.present).map((s) => s.title)]));
  const widthKnob = (id, label, value, x, y) => new Knob(right, { id: "st_" + id, label, x, y, w: 160, size: 112, min: 0, max: 200, def: value, format: (v) => fmt.pct(v), labelGap: 12, valueGap: 10 });
  widthKnob("width", "WIDTH", 125, 6, 140);
  widthKnob("low", "LOW WIDTH", 60, 176, 140);
  widthKnob("mid", "MID WIDTH", 100, 6, 360);
  widthKnob("high", "HIGH WIDTH", 145, 176, 360);
  const note = el("div", "", right, { position: "absolute", left: "22px", right: "22px", top: "600px", fontSize: "17px", color: "var(--text-dim)", lineHeight: "1.4" });
  note.innerHTML = "Low Width under 100% keeps kick and 808 solid in mono (clubs, phones). High Width above 100% opens up hats, pads and reverbs.";

  let dots = null, corr = null;
  const trail = [];
  scope.draw = (ctx, w, h) => {
    const dark = document.documentElement.dataset.theme !== "light";
    const cx = w / 2, by = h - 34, R = Math.min(w / 2 - 20, h - 60);
    ctx.strokeStyle = cssVar("--grid");
    for (let i = 1; i <= 5; i++) { ctx.beginPath(); ctx.arc(cx, by, (R * i) / 5, Math.PI, 2 * Math.PI); ctx.stroke(); }
    for (let a = -90; a <= 90; a += 15) { const r = (a * Math.PI) / 180; ctx.beginPath(); ctx.moveTo(cx, by); ctx.lineTo(cx + R * Math.sin(r), by - R * Math.cos(r)); ctx.stroke(); }
    ctx.strokeStyle = dark ? "rgba(25,211,255,0.75)" : "rgba(0,120,200,0.75)"; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(cx - R * 0.7071, by - R * 0.7071); ctx.lineTo(cx, by); ctx.lineTo(cx + R * 0.7071, by - R * 0.7071); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(cx, by); ctx.lineTo(cx, by - R); ctx.stroke();
    ctx.lineWidth = 1;
    ctx.fillStyle = dark ? "#19d3ff" : "#0088cc"; ctx.font = "600 28px 'Barlow SC', sans-serif";
    ctx.textAlign = "left"; ctx.fillText("L", 16, h - 12); ctx.textAlign = "right"; ctx.fillText("R", w - 16, h - 12);
    if (!dots) return;
    ctx.globalCompositeOperation = dark ? "lighter" : "source-over";
    const frames = [...trail, dots];
    frames.forEach((d, fi) => {
      const age = (frames.length - fi) / frames.length;
      ctx.fillStyle = dark ? `rgba(60,170,255,${0.10 + 0.5 * (1 - age)})` : `rgba(0,90,200,${0.08 + 0.4 * (1 - age)})`;
      for (let i = 0; i + 1 < d.length; i += 2) {
        let x = d[i], y = d[i + 1];
        if (y < 0) { x = -x; y = -y; }   // fold the lower half up (polar sample view)
        const px = cx + clamp(x * 1.6, -1, 1) * R, py = by - clamp(y * 1.6, 0, 1) * R;
        ctx.fillRect(px - 1.2, py - 1.2, 2.4, 2.4);
      }
    });
    ctx.globalCompositeOperation = "source-over";
  };

  return {
    onShow() { scope.redraw(); },
    onFrame(f) {
      if (f.scope) {
        if (dots) { trail.push(dots); if (trail.length > 5) trail.shift(); }
        dots = f.scope;
        scope.redraw();
      }
      corr = f.corr;
      if (corr === null || corr === undefined) { corrMark.style.left = "calc(50% - 3px)"; corrMark.style.opacity = "0.35"; corrText.textContent = "No audio"; }
      else {
        corrMark.style.opacity = "1";
        corrMark.style.left = `calc(${((corr + 1) / 2) * 100}% - 3px)`;
        corrText.textContent = `Correlation ${fmt.signed(corr, 2)}: ` + (corr < 0 ? "out of phase: parts will cancel in mono" : corr < 0.3 ? "very wide: check the mix in mono" : corr > 0.9 ? "almost mono" : "healthy stereo");
      }
    },
  };
}

// =============================================================================================
// MASTER: mix-bus monitoring and a hand-off check for Masterology (no limiting here).

const MASTER_MODULES = [
  { id: "monitor", label: "MONITOR", icon: "monitor" },
  { id: "glue", label: "BUS GLUE", icon: "glue" },
  { id: "headroom", label: "HEADROOM", icon: "headroom" },
  { id: "handoff", label: "HAND-OFF", icon: "handoff" },
];

export function buildMaster(root, app) {
  let mod = store.get("masterModule", "monitor");
  moduleList(root, MASTER_MODULES, 0, 0, 252, 104, 18, mod, (id) => { mod = id; store.set("masterModule", id); });

  const centre = el("div", "panel", root, box(266, 0, 790, 556));
  const ct = el("div", "panel-title", centre);
  ct.textContent = "LOUDNESS HISTORY";
  const sub = el("div", "", centre, { position: "absolute", right: "22px", top: "26px", fontSize: "17px", color: "var(--text-dim)" });
  sub.textContent = "short-term LUFS, last 60 s";
  const hist = graph(centre, 18, 70, 754, 470);
  const history = [];

  hist.draw = (ctx, w, h) => {
    const grid = cssVar("--grid"), dim = cssVar("--text-dim");
    const L = 50, H = h - 40, W = w - L - 12;
    const Y = (l) => 10 + (1 - (l + 48) / 48) * H;
    ctx.font = "16px 'Barlow SC', sans-serif";
    for (const l of [0, -6, -12, -18, -24, -36, -48]) {
      ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo(L, Y(l)); ctx.lineTo(L + W, Y(l)); ctx.stroke();
      ctx.fillStyle = dim; ctx.textAlign = "right"; ctx.fillText(l === 0 ? "0" : "−" + -l, L - 8, Y(l) + 5);
    }
    for (let s = 0; s <= 60; s += 10) { const x = L + W - (s / 60) * W; ctx.strokeStyle = grid; ctx.beginPath(); ctx.moveTo(x, 10); ctx.lineTo(x, 10 + H); ctx.stroke(); ctx.fillStyle = dim; ctx.textAlign = "center"; ctx.fillText(s === 0 ? "now" : "−" + s + " s", x, h - 10); }
    if (history.length < 2) return;
    const pts = history.map((v, i) => [L + W - ((history.length - 1 - i) / 1800) * W, Y(clamp(v, -48, 0))]);
    const grad = ctx.createLinearGradient(0, 10, 0, 10 + H);
    grad.addColorStop(0, "#ff2a2a"); grad.addColorStop(0.25, "#ffd21f"); grad.addColorStop(0.5, "#3dff5a"); grad.addColorStop(0.8, "#19d3ff"); grad.addColorStop(1, "#1f6bff");
    ctx.save();
    ctx.globalAlpha = 0.22; ctx.fillStyle = grad;
    ctx.beginPath(); ctx.moveTo(pts[0][0], 10 + H); pts.forEach(([x, y]) => ctx.lineTo(x, y)); ctx.lineTo(pts[pts.length - 1][0], 10 + H); ctx.fill();
    ctx.globalAlpha = 1; ctx.strokeStyle = grad; ctx.lineWidth = 2.5; ctx.shadowColor = "rgba(255,255,255,0.3)"; ctx.shadowBlur = 8;
    ctx.beginPath(); pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y))); ctx.stroke();
    ctx.restore();
  };

  const bottom = el("div", "", root, box(266, 570, 790, 240));
  knobRow(bottom, [
    { id: "glue", label: "GLUE", min: 0, max: 6, value: 1, step: 0.1, format: (v) => v.toFixed(1) + " dB GR" },
    { id: "tilt", label: "TILT", min: -3, max: 3, value: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v) },
    { id: "width", label: "WIDTH", min: 50, max: 150, value: 100, format: (v) => fmt.pct(v) },
    { id: "trim", label: "TRIM", min: -12, max: 12, value: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v) },
  ], "master");
  previewTag(root, 270, 816);

  const right = el("div", "panel", root, box(1070, 0, 342, 842));
  const rt = el("div", "panel-title gold", right);
  rt.textContent = "HAND-OFF";
  const rsub = el("div", "", right, { position: "absolute", left: "22px", right: "22px", top: "56px", fontSize: "17px", color: "var(--text-dim)" });
  rsub.textContent = "Is the mix ready for Masterology?";
  const readouts = el("div", "", right, { position: "absolute", left: "22px", right: "22px", top: "96px", display: "grid", gridTemplateColumns: "1fr auto", rowGap: "10px", fontSize: "20px" });
  const rows = {};
  for (const [id, label] of [["st", "Short-term"], ["int", "Integrated"], ["tp", "True peak (max)"]]) {
    el("div", "dim", readouts).textContent = label;
    rows[id] = el("div", "", readouts, { fontWeight: "600", textAlign: "right" });
  }
  const mL = new Meter(right, 110, 230, 26, 360, [-48, 0]), mR = new Meter(right, 150, 230, 26, 360, [-48, 0]);
  const sc = el("div", "", right, { position: "absolute", left: "56px", top: "222px", width: "44px", height: "370px", fontSize: "16px", color: "var(--text-dim)" });
  for (const db of [0, -6, -12, -24, -36, -48]) { const l = el("div", "", sc, { position: "absolute", right: "0", top: ((-db / 48) * 360) + "px" }); l.textContent = db === 0 ? "0" : "−" + -db; }
  const lr = el("div", "", right, { position: "absolute", left: "112px", top: "596px", width: "70px", display: "flex", justifyContent: "space-between", fontSize: "18px", color: "var(--text-dim)" });
  lr.innerHTML = "<span>L</span><span>R</span>";
  const checks = el("div", "", right, { position: "absolute", left: "18px", right: "18px", top: "636px", display: "flex", flexDirection: "column", gap: "8px", fontSize: "17px" });

  let lastPush = 0;
  return {
    always: true,
    onFrame(f) {
      const now = performance.now();
      if (now - lastPush > 33) { lastPush = now; history.push(f.shortTerm ?? -100); if (history.length > 1800) history.shift(); }
      if (!root.classList.contains("shown")) return;
      hist.redraw();
      rows.st.textContent = fmt.lufs(f.shortTerm ?? -100) + " LUFS";
      rows.int.textContent = fmt.lufs(f.integrated ?? -100) + " LUFS";
      rows.tp.textContent = (f.truePeak ?? -100) <= -99 ? "−∞" : fmt.num(f.truePeak, 1) + " dBTP";
      mL.set(f.peakL ?? -100); mR.set(f.peakR ?? -100);
      const tp = f.truePeak ?? -100, silent = (f.shortTerm ?? -100) <= -70;
      const items = [
        [silent ? null : tp <= -3, tp <= -3 ? "Peaks leave 3 dB+ for Masterology" : "Peaks are above −3 dBTP: pull the mix down"],
        [true, "No limiter on the mix bus (Masterology does that)"],
        [silent ? null : (f.shortTerm ?? -100) < -8, (f.shortTerm ?? -100) < -8 ? "Mix isn't squashed (short-term under −8 LUFS)" : "Mix is already very loud: ease off bus processing"],
      ];
      checks.innerHTML = items.map(([ok, text]) => `<div style="display:flex;gap:10px;align-items:flex-start"><span style="width:22px;height:22px;flex:none;color:${ok === null ? "var(--text-faint)" : ok ? "var(--green)" : "var(--amber)"}">${icon(ok === false ? "headroom" : "check", 'stroke-width="2.4"')}</span><span>${text}</span></div>`).join("");
    },
  };
}
