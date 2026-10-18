// Tone, Stereo and Master pages. Tone and Stereo work on the target you pick (Full Mix or a track's Mixology
// Link, Milestone 5); their meters and scopes are live. Master's finishing controls are still a preview.

import { el, box, icon, fmt, esc, clamp, Knob, Meter, select, graph, freqToX, moduleList, store, cssVar } from "./ui.js";
import { knobFor, biquad, magnitudeDb, MODULE_NAMES } from "./proc.js";
import { dawName, say } from "./daws.js";

function knobRow(host, defs, prefix, onChange) {
  host.innerHTML = "";
  const n = defs.length;
  const cw = Math.floor((host.clientWidth || parseFloat(host.style.width)) / n);
  return defs.map((k, i) => {
    const card = el("div", "card", host, box(i * cw + 4, 0, cw - 8, parseFloat(host.style.height)));
    return new Knob(card, { ...k, id: prefix ? `${prefix}_${k.id}` : undefined, x: (cw - 8 - Math.min(cw, 170)) / 2, y: 24, w: Math.min(cw, 170), size: Math.min(112, cw - 46), def: k.def ?? k.value, labelGap: 14, valueGap: 12, onChange: k.onChange || onChange });
  });
}

// =============================================================================================
// TONE

const TONE_MODULES = [
  { id: "balance", label: "TONE BALANCE", icon: "gauge", lamp: true },
  { id: "loud", label: "LOUDNESS", icon: "speaker", lamp: true },
  { id: "clip", label: "CLIP CONTROL", icon: "clip", lamp: true },
  { id: "harm", label: "HARMONIC", icon: "harmonic", lamp: true },
  { id: "analog", label: "ANALOG TOUCH", icon: "analog", lamp: true },
  { id: "final", label: "FINAL TOUCH", icon: "finalTouch", lamp: true },
];
const dbK = (id, label) => ({ id, label, bipolar: true, format: (v) => fmt.db(v) });
const pctK = (id, label) => ({ id, label, format: (v) => fmt.pct(v) });
const TONE_KNOBS = {
  balance: [dbK("tb_warmth", "WARMTH"), dbK("tb_presence", "PRESENCE"), dbK("tb_air", "AIR"), pctK("tb_glue", "GLUE"),
    { id: "tb_punch", label: "PUNCH", bipolar: true, format: (v) => fmt.signed(v, 0) + "%" }],
  loud: [{ id: "ld_trim", label: "TRIM", bipolar: true, format: (v) => fmt.db(v) }, pctK("ld_density", "DENSITY"), dbK("ld_low", "LOW END"), dbK("ld_top", "TOP END")],
  clip: [{ id: "clip_thr", label: "THRESHOLD", format: (v) => fmt.db(v) }, pctK("clip_soft", "SOFTNESS"), { id: "clip_os", label: "OVERSAMPLE", format: (v) => ["1x", "2x", "4x", "8x"][Math.round(v)] }, pctK("clip_mix", "MIX")],
  harm: [pctK("harm_drive", "DRIVE"), { id: "harm_evenodd", label: "EVEN / ODD", format: (v) => Math.round(v) + " / " + Math.round(100 - v) }, dbK("harm_tone", "TONE"), pctK("harm_mix", "MIX")],
  analog: [pctK("an_tape", "TAPE"), pctK("an_console", "CONSOLE"), pctK("an_noise", "NOISE"), pctK("an_mix", "MIX")],
  final: [{ id: "fin_width", label: "WIDTH", format: (v) => fmt.pct(v) }, dbK("fin_air", "AIR"), pctK("fin_glue", "GLUE"), { id: "fin_out", label: "OUTPUT", bipolar: true, format: (v) => fmt.db(v) }],
};
const TONE_HELP = {
  balance: "Warmth (low mids), Presence (3 kHz), Air (11 kHz) as gentle shelves and bells; Glue holds the sound together; Punch lifts (or softens) the front of each hit.",
  loud: "Trim the level, Density blends in a hard-working compressor (fuller, louder feel), Low End and Top End shelves.",
  clip: "Rounds off peaks over the Threshold (Softness = how round). Oversampling keeps it clean. Mix blends with the original.",
  harm: "Adds harmonics: Even = warm, Odd = edgy. Tone tilts them brighter or darker. Mix blends with the original.",
  analog: "Tape: soft saturation, a little low bump and a softer top. Console: subtle edge and channel bleed. Noise: tape hiss while it plays.",
  final: "Width (100 % = as it is), Air on top, a little Glue, and the Output level.",
};

// The tonal change each module makes (for the graph), the engine's filters.
function toneChange(proc, mod, f) {
  const sr = proc.sr, g = (id) => proc.get(id);
  const sum = (list) => list.reduce((a, [type, hz, db, q]) => a + (Math.abs(db) > 1e-4 ? magnitudeDb(biquad(type, hz, db, q, sr), f, sr) : 0), 0);
  if (mod === "balance") return sum([["lowshelf", 220, g("tb_warmth"), 0.6], ["bell", 3200, g("tb_presence"), 0.8], ["highshelf", 11000, g("tb_air"), 0.6]]);
  if (mod === "loud") return g("ld_trim") + sum([["lowshelf", 90, g("ld_low"), 0.7], ["highshelf", 9000, g("ld_top"), 0.7]]);
  if (mod === "final") return g("fin_out") + sum([["highshelf", 14000, g("fin_air"), 0.6]]);
  if (mod === "analog") { const t = g("an_tape") / 100 * g("an_mix") / 100; return sum([["bell", 75, 1.5 * t, 1.0], ["highshelf", 14000, -2.5 * t, 0.7]]); }
  return null;
}

export function buildTone(root, app) {
  const proc = app.proc;
  let mod = store.get("toneModule", "balance");
  const list = moduleList(root, TONE_MODULES, 0, 0, 252, 104, 18, mod, (id) => { mod = id; store.set("toneModule", id); renderKnobs(); power.draw(); });

  const top = el("div", "panel", root, box(266, 0, 870, 556));
  const power = proc.powerButton(top, 22, 16, () => mod);
  const legend = el("div", "", top, { position: "absolute", left: "140px", top: "22px", display: "flex", gap: "22px", fontSize: "17px", whiteSpace: "nowrap" });
  proc.targetSelect(top, 620, 14, 230);
  const g = graph(top, 18, 70, 834, 470);
  let mix = null;

  // Genre target curves (dB vs a flat, tilted reference): bass weight, low-mid scoop, top sheen.
  function target(f) {
    const p = app.style;
    const lf = Math.log2(f / 1000);
    const bass = p.bass * Math.exp(-Math.pow(Math.log2(f / 55), 2) / 2.2);
    const scoop = -p.scoop * Math.exp(-Math.pow(Math.log2(f / 350), 2) / 1.2);
    const topEnd = p.air * clamp((lf - 2) / 3, 0, 1);
    return 2 + bass + scoop + topEnd - 0.6 * lf;
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
    if (proc.key === "mix") {   // a genre's target only makes sense for the whole mix
      ctx.save();
      ctx.strokeStyle = "#ff2a2a"; ctx.lineWidth = 3; ctx.shadowColor = "#ff2a2a"; ctx.shadowBlur = 12;
      ctx.beginPath();
      for (let i = 0; i <= 200; i++) { const f = 20 * Math.pow(1000, i / 200); const x = L + (i / 200) * W, y = Y(target(f)); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
      ctx.stroke(); ctx.restore();
    }
    // What this module does to the tone (gold, around the 0 dB line).
    if (toneChange(proc, mod, 1000) !== null) {
      const on = proc.isOn(mod);
      ctx.save();
      ctx.strokeStyle = on ? "#ffd21f" : (dark ? "rgba(220,225,235,0.45)" : "rgba(40,50,70,0.45)"); ctx.lineWidth = 3;
      if (on) { ctx.shadowColor = "#ffd21f"; ctx.shadowBlur = 10; }
      ctx.setLineDash(on ? [] : [8, 6]);
      ctx.beginPath();
      for (let i = 0; i <= 200; i++) { const f = 20 * Math.pow(1000, i / 200); const x = L + (i / 200) * W, y = Y(clamp(toneChange(proc, mod, f), -12, 12)); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
      ctx.stroke(); ctx.restore();
    }
  };
  function renderLegend() {
    const sw = (c) => `<span style="display:inline-block;width:22px;height:3px;background:${c};vertical-align:5px;margin-right:8px"></span>`;
    legend.innerHTML = (proc.key === "mix" ? `<span>${sw("#ff2a2a")}Target: ${esc(app.style.fullName)}</span><span>${sw("var(--text)")}Your mix</span>`
      : `<span>${sw("var(--text)")}${esc(proc.name)}</span>`)
      + (toneChange(proc, mod, 1000) !== null ? `<span>${sw("#ffd21f")}${MODULE_NAMES[mod]}</span>` : "");
  }
  app.on("preset", () => { renderLegend(); g.redraw(); });
  app.on("theme", () => g.redraw());

  const bottom = el("div", "", root, box(266, 570, 870, 272));
  let knobs = [];
  function renderKnobs() {
    knobs = knobRow(bottom, TONE_KNOBS[mod].map((k) => knobFor(proc, k.id, { label: k.label, format: k.format, bipolar: k.bipolar, onChange: (v) => proc.set(k.id, v), pid: k.id })), "");
    list.set(mod);
    renderLegend();
    sync();
  }
  function sync() {
    knobs.forEach((k) => { const v = proc.get(k.o.pid); if (!k.dragging && Math.abs(k.value - v) > 1e-3) k.set(v); });
    list.lamps((m) => proc.switchedOn(m), (m) => proc.running(m));
    power.el.title = TONE_HELP[mod];
    g.redraw();
  }
  proc.on((why) => {
    if (why === "values") sync();
    if (why === "target") { mix = null; renderLegend(); sync(); }
  });

  // Right: loudness of the full mix (live)
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
  statNote.textContent = "Full mix, info only: Masterology sets the final loudness";

  renderKnobs();

  return {
    onFrame(f) {
      const s = proc.key === "mix" ? f.spectrum : proc.spectrum;
      if (s) {
        const k = 6;
        const sm = s.map((_, i) => { let a = 0, c = 0; for (let j = Math.max(0, i - k); j <= Math.min(s.length - 1, i + k); j++) { a += s[j]; c++; } return a / c; });
        const mean = sm.reduce((a, b) => a + b, 0) / sm.length;
        const rel = sm.map((v) => v - mean);
        mix = mix && mix.length === rel.length ? mix.map((v, i) => 0.85 * v + 0.15 * rel[i]) : rel;
        g.redraw();
      }
      list.lamps((m) => proc.switchedOn(m), (m) => proc.running(m));
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
  { id: "width", label: "STEREO WIDTH", icon: "rings", lamp: true },
  { id: "pan", label: "PAN ASSIST", icon: "pan" },
  { id: "mono", label: "MONO CHECK", icon: "mono" },
  { id: "freq", label: "FREQUENCY WIDTH", icon: "freq", lamp: true },
  { id: "corr", label: "CORRELATION", icon: "corr" },
];

export function buildStereo(root, app) {
  const proc = app.proc;
  let mod = store.get("stereoModule", "width");
  if (mod === "mono") mod = "width";
  const list = moduleList(root, STEREO_MODULES, 0, 0, 252, 104, 18, mod, (id) => {
    if (id === "mono") { proc.setMono(!proc.mono); list.set(mod); return; }
    if (id === "pan") { app.toast("Pan Assist: use Stereo Architect on the Overview. It plans pans for every track (pairs spread, low end centred)."); list.set(mod); return; }
    mod = id; store.set("stereoModule", id);
  });

  const centre = el("div", "panel", root, box(266, 0, 790, 842));
  const scope = graph(centre, 18, 18, 754, 640);
  const scopeNote = el("div", "", centre, { position: "absolute", left: "30px", top: "26px", fontSize: "15px", color: "var(--text-dim)" });
  scopeNote.textContent = "Scope: the full mix";
  const monoBadge = el("div", "mono-badge", centre, { left: "50%", top: "24px", transform: "translateX(-50%)", display: "none" });
  monoBadge.textContent = "LISTENING IN MONO";
  monoBadge.title = "Mono Check is on: you hear the full mix in mono (the scope still shows it as it really is). It switches off when you leave this page.";
  const corrBar = el("div", "", centre, { position: "absolute", left: "30px", right: "30px", top: "690px", height: "24px", borderRadius: "6px", background: "linear-gradient(90deg,#ff1d1d,#ff7a12,#ffe11f,#3dff5a,#19d3ff,#1f6bff,#ff2bf0)", boxShadow: "0 0 14px rgba(120,180,255,0.35)" });
  const corrMark = el("div", "", corrBar, { position: "absolute", top: "-8px", width: "6px", height: "40px", borderRadius: "3px", background: "#fff", boxShadow: "0 0 10px #fff, 0 0 0 1.5px #000", left: "50%" });
  const corrLab = el("div", "", centre, { position: "absolute", left: "30px", right: "30px", top: "726px", display: "flex", justifyContent: "space-between", fontSize: "26px", fontWeight: "500" });
  corrLab.innerHTML = "<span>−1</span><span>0</span><span>+1</span>";
  const corrText = el("div", "", centre, { position: "absolute", left: "0", right: "0", top: "772px", textAlign: "center", fontSize: "19px", color: "var(--text-dim)" });

  const right = el("div", "panel", root, box(1070, 0, 342, 842));
  proc.powerButton(right, 22, 16, () => "width");
  proc.targetSelect(right, 22, 60, 298);
  const KNOBS = [["st_width", "WIDTH", 6, 128], ["st_low", "LOW WIDTH", 176, 128], ["st_mid", "MID WIDTH", 6, 336], ["st_high", "HIGH WIDTH", 176, 336]];
  const knobs = KNOBS.map(([id, label, x, y]) => new Knob(right, knobFor(proc, id, { label, x, y, w: 160, size: 104, format: (v) => fmt.pct(v), labelGap: 10, valueGap: 8, onChange: (v) => proc.set(id, v), pid: id })));
  const XKNOBS = [["st_lowx", "LOW BELOW", 6, 548], ["st_highx", "HIGH ABOVE", 176, 548]];
  knobs.push(...XKNOBS.map(([id, label, x, y]) => new Knob(right, knobFor(proc, id, { label, x, y, w: 160, size: 76, format: (v) => fmt.hz(v), labelGap: 8, valueGap: 6, onChange: (v) => proc.set(id, v), pid: id }))));
  const note = el("div", "proc-note", right, { left: "22px", right: "22px", top: "730px", fontSize: "15px" });
  note.textContent = "Low Width under 100 % keeps kick and 808 solid in mono (clubs, phones). High Width over 100 % opens up hats, pads and reverbs.";

  let dots = null, corr = null, shownMono = null;
  function sync() {
    knobs.forEach((k) => { const v = proc.get(k.o.pid); if (!k.dragging && Math.abs(k.value - v) > 1e-3) k.set(v); });
    list.lamps((m) => (m === "width" || m === "freq") && proc.switchedOn("width"), (m) => (m === "width" || m === "freq") && proc.running("width"));
    monoBadge.style.display = proc.mono ? "block" : "none";
    scopeNote.textContent = proc.mono ? "Scope: the full mix as it really is (you hear it in mono)" : "Scope: the full mix";
    list.buttons.find((b) => b.dataset.id === "mono").classList.toggle("active", proc.mono);
    const mono = proc.key !== "mix" && proc.channels === 1;
    if (mono) { note.textContent = `'${proc.name}' is a mono track: width does nothing here (pan it instead).`; note.style.color = "var(--amber)"; shownMono = "track"; }
    else if (shownMono === "track") shownMono = null;
  }
  proc.on((why) => { if (why === "values" || why === "target" || why === "mono") sync(); });
  sync();

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
      list.lamps((m) => (m === "width" || m === "freq") && proc.switchedOn("width"), (m) => (m === "width" || m === "freq") && proc.running("width"));
      const track = proc.key !== "mix";
      corr = track ? proc.corr : f.corr;
      // Mono material: width has nothing to work with (left = right). Say so instead of looking broken.
      const monoNow = corr !== null && corr !== undefined && corr > 0.985;
      // The setup check knows more: stereo tracks folded by a mono group (and which one it most likely is).
      const fold = !track && ((app.bridge && app.bridge.setup) || []).find((s) => s.id === "setup-mono-group" || s.id === "setup-mono-mixology");
      const noteKey = monoNow + "|" + track + "|" + (fold ? fold.id + (fold.tracks || []).join(",") : "");
      if (shownMono !== "track" && noteKey !== shownMono) {   // a mono track keeps its own note (sync)
        shownMono = noteKey;
        note.style.color = monoNow || fold ? "var(--amber)" : "";
        note.textContent = fold && fold.id === "setup-mono-mixology" ? `Mixology sits on a mono channel, so it hears the whole mix in mono. Put it on the ${say(app.daw, "master")} channel (Overview > Mix Assistant says how).`
          : fold ? `Your tracks are stereo, but the mix arrives in mono${fold.tracks && fold.tracks.length ? `: most likely '${fold.tracks[0]}' is a mono group` : " (a mono group on the way)"}. Make it stereo in ${dawName(app.daw)} (Overview > Mix Assistant says how).`
          : monoNow
          ? `${track ? `'${proc.name}'` : "The full mix"} is mono right now (left = right${track ? "" : ": e.g. a mono group on the way"}), so width can't change it. Widen a stereo track instead, or make that group / output stereo.`
          : "Low Width under 100 % keeps kick and 808 solid in mono (clubs, phones). High Width over 100 % opens up hats, pads and reverbs.";
      }
      const who = track ? `'${proc.name}'` : "Full mix";
      if (corr === null || corr === undefined) { corrMark.style.left = "calc(50% - 3px)"; corrMark.style.opacity = "0.35"; corrText.textContent = track ? `${who}: no audio` : "No audio"; }
      else {
        corrMark.style.opacity = "1";
        corrMark.style.left = `calc(${((corr + 1) / 2) * 100}% - 3px)`;
        corrText.textContent = `${who} correlation ${fmt.signed(corr, 2)}: ` + (corr < 0 ? "out of phase: parts will cancel in mono" : corr < 0.3 ? "very wide: check the mix in mono" : corr > 0.9 ? "almost mono" : "healthy stereo");
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

  // Finishing on the Full Mix (Mixology on the Stereo Out, before Masterology): Glue, Tilt, Width, Trim.
  const bottom = el("div", "", root, box(266, 570, 790, 232));
  let master = null, movedAt = -1e9;
  const send = (changes) => { movedAt = performance.now(); app.native.procSet("mix", changes); };
  const finishing = knobRow(bottom, [
    { id: "glue", label: "GLUE", min: 0, max: 100, value: 0, step: 1, format: (v) => fmt.pct(v), onChange: (v) => send({ fin_on: 1, fin_glue: v }) },
    { id: "tilt", label: "TILT", min: -3, max: 3, value: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v),
      onChange: (v) => { const c = master && master.tbOn ? (master.air + master.warmth) / 2 : 0; send({ tb_on: 1, tb_warmth: c - v, tb_air: c + v }); } },
    { id: "width", label: "WIDTH", min: 50, max: 150, value: 100, step: 1, format: (v) => fmt.pct(v), onChange: (v) => send({ fin_on: 1, fin_width: v }) },
    { id: "trim", label: "TRIM", min: -12, max: 12, value: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v), onChange: (v) => send({ fin_on: 1, fin_out: v }) },
  ]);
  finishing[0].root.title = "Bus glue: gentle compression that makes the mix hang together (Final Touch on the Full Mix)";
  finishing[1].root.title = "Tilt: + brighter and lighter, − warmer and darker (Tone Balance on the Full Mix: Air and Warmth in opposite directions)";
  finishing[2].root.title = "Stereo width of the whole mix (100 % = as it is)";
  finishing[3].root.title = "The mix's level into Masterology";
  const finNote = el("div", "", root, { position: "absolute", left: "270px", top: "806px", width: "780px", fontSize: "15px", color: "var(--text-dim)", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" });

  const right = el("div", "panel", root, box(1070, 0, 342, 842));
  const rt = el("div", "panel-title gold", right);
  rt.textContent = "HAND-OFF";
  const rsub = el("div", "", right, { position: "absolute", left: "22px", right: "22px", top: "56px", fontSize: "17px", color: "var(--text-dim)", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" });
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
      if (f.master) {
        master = f.master;
        const m = master;
        const want = [m.finOn ? m.glue : 0, m.tbOn ? (m.air - m.warmth) / 2 : 0, m.finOn ? m.width : 100, m.finOn ? m.out : 0];
        if (now - movedAt > 800)
          finishing.forEach((k, i) => { if (!k.dragging && Math.abs(k.value - want[i]) > 0.05) k.set(want[i]); });
        finNote.textContent = `These work on the Full Mix (Mixology on the ${say(app.daw, "master")} channel), before Masterology.`
          + (m.finOn && m.glue > 0 ? ` Glue now ${m.glueGr >= 0.1 ? fmt.db(-m.glueGr) : "0.0 dB"}.` : "");
      }
      hist.redraw();
      rows.st.textContent = fmt.lufs(f.shortTerm ?? -100) + " LUFS";
      rows.int.textContent = fmt.lufs(f.integrated ?? -100) + " LUFS";
      rows.tp.textContent = (f.truePeak ?? -100) <= -99 ? "−∞" : fmt.num(f.truePeak, 1) + " dBTP";
      mL.set(f.peakL ?? -100); mR.set(f.peakR ?? -100);
      const tp = f.truePeak ?? -100, silent = (f.shortTerm ?? -100) <= -70;
      // M6: Masterology after Mixology tells what it aims for (the suite link).
      const g = f.master && f.master.masterology;
      if (f.master) {
        rsub.textContent = g ? `Masterology: ${fmt.num(g.target, 0)} LUFS · ${fmt.num(g.ceiling, 1)} dBTP` : "Is the mix ready for Masterology?";
        rsub.title = g ? `Masterology aims for ${fmt.num(g.target, 0)} LUFS with peaks up to ${fmt.num(g.ceiling, 1)} dBTP` + (g.goal || g.genre ? ` (${[g.goal, g.genre].filter(Boolean).join(", ")})` : "") : "";
      }
      // The peaks need room under Masterology's ceiling (3 dB, more when its ceiling is lower), and a mix 3 dB louder
      // than Masterology's target was squashed for nothing (Masterology turns it down): say so with its own numbers.
      const room = g && Number.isFinite(g.ceiling) ? Math.min(-3, g.ceiling - 2) : -3, st = f.shortTerm ?? -100;
      const loudLimit = g && Number.isFinite(g.target) ? Math.min(-8, g.target + 3) : -8;
      const items = [
        [silent ? null : tp <= room, tp <= room ? `Peaks leave ${fmt.num(-room, 0)} dB+ for Masterology` : `Peaks are above ${fmt.num(room, 0)} dBTP: pull the mix down`],
        f.master && f.master.limOn ? [false, "Mixology's Limiter is on the mix: switch it off and let Masterology limit"] : [true, "No limiter on the mix bus (Masterology does that)"],
        [silent ? null : st < loudLimit, st < loudLimit ? `Mix isn't squashed (short-term under ${fmt.num(loudLimit, 0)} LUFS)`
          : g && st < -8 ? `Mix is louder than Masterology's ${fmt.num(g.target, 0)} LUFS target: Masterology would only turn it down. Ease off bus compression and limiting`
          : "Mix is already very loud: ease off bus processing"],
      ];
      if (f.master) items.push(g
        ? [true, `Masterology is on${g.goal ? " (" + g.goal + ")" : ""}: it gets this mix's style and tone`]
        : [null, `No Masterology playing after Mixology on the ${say(app.daw, "master")} channel (not loaded, or bypassed): there it picks up this mix's style and tone`]);
      checks.innerHTML = items.map(([ok, text]) => `<div style="display:flex;gap:10px;align-items:flex-start"><span style="width:22px;height:22px;flex:none;color:${ok === null ? "var(--text-faint)" : ok ? "var(--green)" : "var(--amber)"}">${icon(ok === false ? "headroom" : "check", 'stroke-width="2.4"')}</span><span>${esc(text)}</span></div>`).join("");
    },
  };
}
