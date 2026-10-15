// Shared building blocks: elements, icons, knob, meter, dropdown, module list, graph canvas,
// number formatting and the saved UI state.

export const $ = (id) => document.getElementById(id);
export const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
export const MINUS = "−";

export function el(tag, cls, parent, style) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (style) Object.assign(e.style, style);
  if (parent) parent.appendChild(e);
  return e;
}

/** Absolute box in design pixels. */
export function box(x, y, w, h) {
  return { position: "absolute", left: x + "px", top: y + "px", width: w + "px", height: h + "px" };
}

// ---------------------------------------------------------------------------------------------
// Formatting

export const fmt = {
  num(v, d = 1) { return (v < 0 ? MINUS : "") + Math.abs(v).toFixed(d); },
  signed(v, d = 1) { return (v > 0.00001 ? "+" : v < -0.00001 ? MINUS : "") + Math.abs(v).toFixed(d); },
  db(v, d = 1) { return v <= -99 ? MINUS + "∞ dB" : fmt.signed(v, d) + " dB"; },
  hz(v) { return v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2).replace(/\.?0+$/, "") + " kHz" : Math.round(v) + " Hz"; },
  ms(v) { return v >= 1000 ? (v / 1000).toFixed(2).replace(/\.?0+$/, "") + " s" : (v < 10 ? v.toFixed(1) : Math.round(v)) + " ms"; },
  sec(v) { return v.toFixed(1) + " s"; },
  pct(v) { return Math.round(v) + "%"; },
  ratio(v) { return v.toFixed(1) + ":1"; },
  pan(v) { const p = Math.round(v); return p === 0 ? "C" : (p < 0 ? "L" : "R") + Math.abs(p); },
  q(v) { return v.toFixed(1); },
  lufs(v) { return v <= -99 ? MINUS + "∞" : fmt.num(v, 1); },
};

// ---------------------------------------------------------------------------------------------
// Icons (24 x 24 viewBox, stroke = currentColor)

const P = {
  chevL: '<path d="M15 18l-6-6 6-6"/>',
  chevR: '<path d="M9 18l6-6-6-6"/>',
  chevD: '<path d="M6 9l6 6 6-6"/>',
  close: '<path d="M6 6l12 12M18 6L6 18"/>',
  gear: '<circle cx="12" cy="12" r="3.2"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
  help: '<path d="M9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3"/><path d="M12 17h.01"/>',
  history: '<path d="M3 12a9 9 0 1 0 2.7-6.4"/><path d="M3 4v5h5"/><path d="M12 7.5V12l3.2 2"/>',
  wand: '<path d="M15 4V2M15 16v-2M8 9h2M20 9h2M17.8 11.8L19 13M17.8 6.2L19 5M12.2 6.2L11 5"/><path d="M15 9l-12 12"/>',
  sliders: '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12M20 18h0"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
  wave: '<path d="M2 12h3l2-6 3 12 3-14 3 16 2-8h4"/>',
  mic: '<rect x="9" y="2" width="6" height="12" rx="3"/><path d="M5 10a7 7 0 0 0 14 0M12 17v5M8 22h8"/>',
  rings: '<circle cx="8.5" cy="12" r="5.5"/><circle cx="15.5" cy="12" r="5.5"/>',
  target: '<circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2.5"/><path d="M12 1v4M12 19v4M1 12h4M19 12h4"/>',
  bulb: '<path d="M9 18h6M10 22h4"/><path d="M12 2a7 7 0 0 0-4 12.7c.6.5 1 1.3 1 2.1V17h6v-.2c0-.8.4-1.6 1-2.1A7 7 0 0 0 12 2z"/>',
  check: '<circle cx="12" cy="12" r="9.5"/><path d="M7.5 12.5l3 3 6-6.5"/>',
  link: '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1"/><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
  lock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
  unlock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 7.5-2"/>',
  compressor: '<circle cx="12" cy="12" r="9.5"/><path d="M5 12h3l1.5-4 2.5 8 2.5-8 1.5 4h3"/>',
  bars: '<path d="M4 9v6M7 6v12M10 9v6M13 4v16M16 8v8M19 10v4"/>',
  transient: '<path d="M2 12h4l2-8 2 16 2-12 2 6 2-2h6"/>',
  gate: '<circle cx="12" cy="12" r="6.5"/><circle cx="12" cy="12" r="2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/>',
  clock: '<circle cx="12" cy="12" r="9.5"/><path d="M12 7v5l3 2"/>',
  cube: '<path d="M12 2l9 5v10l-9 5-9-5V7z"/><path d="M12 22V12M21 7l-9 5-9-5"/>',
  chorus: '<circle cx="12" cy="12" r="9.5"/><path d="M6 13c1.5-4 3-4 4 0s2.5 4 4 0 3-4 4 0"/>',
  phaser: '<circle cx="12" cy="12" r="8.5"/><path d="M5 19L19 5"/>',
  star: '<circle cx="12" cy="12" r="9.5"/><path d="M12 6.5l1.7 3.5 3.8.5-2.8 2.6.7 3.8L12 15l-3.4 1.9.7-3.8-2.8-2.6 3.8-.5z"/>',
  hall: '<path d="M3 21V9l9-6 9 6v12"/><path d="M7 21v-8M12 21v-8M17 21v-8M3 21h18"/>',
  room: '<path d="M4 20V8l8-4 8 4v12z"/><path d="M4 8l8 4 8-4M12 12v8"/>',
  gauge: '<circle cx="12" cy="13" r="8.5"/><path d="M12 13l4-4M7 13h.01M12 8h.01M17 13h.01"/>',
  speaker: '<path d="M4 9h4l5-4v14l-5-4H4z"/><path d="M16.5 9a4 4 0 0 1 0 6M19 6.5a7.5 7.5 0 0 1 0 11"/>',
  clip: '<circle cx="12" cy="12" r="9.5"/><path d="M8 15c0-4 1.5-6 4-6s4 2 4 6"/>',
  harmonic: '<path d="M2 12h3l2-5 2 10 2-7 2 4 2-2 2 1h5"/>',
  analog: '<circle cx="12" cy="12" r="9.5"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1.5"/>',
  finalTouch: '<circle cx="12" cy="12" r="9.5"/><path d="M10 16V8h5M10 12h4"/>',
  pan: '<circle cx="12" cy="12" r="6.5"/><circle cx="12" cy="12" r="1.8"/><path d="M12 1v5M12 18v5M1 12h5M18 12h5"/>',
  mono: '<circle cx="9" cy="12" r="6"/><circle cx="15" cy="12" r="6"/><path d="M12 7.5v9"/>',
  freq: '<path d="M3 15v-2M6 18V9M9 15v-4M12 20V4M15 16V8M18 14v-4M21 13v-2"/>',
  corr: '<circle cx="5" cy="12" r="2.2"/><circle cx="12" cy="5" r="2.2"/><circle cx="19" cy="12" r="2.2"/><circle cx="12" cy="19" r="2.2"/><path d="M6.6 10.4l3.8-3.8M13.6 6.6l3.8 3.8M17.4 13.6l-3.8 3.8M10.4 17.4l-3.8-3.8"/>',
  headroom: '<path d="M4 20h16M6 16h12M8 12h8"/><path d="M12 3v6M9 6l3-3 3 3"/>',
  glue: '<rect x="4" y="7" width="16" height="10" rx="3"/><path d="M8 12h8"/>',
  monitor: '<rect x="3" y="4" width="18" height="12" rx="2"/><path d="M8 20h8M12 16v4"/>',
  handoff: '<path d="M4 12h12M12 6l6 6-6 6"/><path d="M20 4v16"/>',
  // track roles
  kick: '<circle cx="12" cy="12.5" r="8.5"/><circle cx="12" cy="12.5" r="4.2"/><circle cx="12" cy="12.5" r="1"/><path d="M5.5 19.5L3.5 22.5M18.5 19.5l2 3"/>',
  snare: '<ellipse cx="12" cy="11.5" rx="8.5" ry="3"/><path d="M3.5 11.5v5.5c0 1.7 3.8 3 8.5 3s8.5-1.3 8.5-3v-5.5"/><path d="M7.5 14.3v5.2M12 14.5v5.5M16.5 14.3v5.2"/><path d="M4.5 2.5l6 7M19.5 2.5l-6 7"/>',
  drums: '<circle cx="12" cy="15.5" r="6"/><circle cx="12" cy="15.5" r="2"/><path d="M1.5 7.2l6-2.2M16.5 5l6 2.2M4.6 6.1v3.3M19.4 6.1v3.3"/><ellipse cx="12" cy="5.3" rx="2.5" ry="1.1"/><path d="M9.5 5.3v1.8c0 .6 1.1 1.1 2.5 1.1s2.5-.5 2.5-1.1V5.3"/>',
  hats: '<path d="M3 9h18M5 9c1-2 4-3 7-3s6 1 7 3M12 9v13M8 22h8M12 4v2"/>',
  perc: '<path d="M4 12h3l1.5-5 2 10 2-7 1.5 3 1.5-1h5"/><circle cx="20" cy="12" r="1"/>',
  bass: '<rect x="5" y="2.5" width="14" height="19" rx="2"/><circle cx="12" cy="15" r="3.5"/><circle cx="12" cy="7" r="1.8"/>',
  keys: '<rect x="2.5" y="5" width="19" height="14" rx="1.5"/><path d="M7 5v9M12 5v9M17 5v9M2.5 14h19"/>',
  synth: '<path d="M2 13c2-6 4-6 5 0s3 6 5 0 3-6 5 0 3 6 5 0"/>',
  guitar: '<path d="M14 10l7-7M18 3l3 3"/><path d="M14.5 9.5a3.5 3.5 0 0 0-4.6.4l-.5.5a2 2 0 0 1-1.6.5 4 4 0 0 0-3.6 6.6l.3.3a4 4 0 0 0 6.6-3.6 2 2 0 0 1 .5-1.6l.5-.5a3.5 3.5 0 0 0 .4-4.6"/>',
  vox: '<rect x="9" y="2" width="6" height="12" rx="3"/><path d="M5 10a7 7 0 0 0 14 0M12 17v5M8 22h8"/>',
  fx: '<path d="M12 2l2.4 6.6L21 9l-5 4.4L17.5 21 12 17.3 6.5 21 8 13.4 3 9l6.6-.4z"/>',
  adlibs: '<path d="M3 12h2l2-4 2 9 2-12 2 14 2-9 2 5 1-3h3"/>',
  other: '<circle cx="12" cy="12" r="3"/><circle cx="12" cy="12" r="8.5"/>',
  // genres
  hat: '<path d="M2.5 15.5c3 2 16 2 19 0"/><path d="M5 15c.5-4 1.5-8 3.5-8 1.2 0 2 1 3.5 1s2.3-1 3.5-1c2 0 3 4 3.5 8"/>',
  choir: '<circle cx="6" cy="8" r="2.4"/><circle cx="12" cy="6.5" r="2.4"/><circle cx="18" cy="8" r="2.4"/><path d="M2.5 19c0-3.5 1.5-6 3.5-6s3 1.2 3.5 2.5M8.5 19c0-4.5 1.5-7.5 3.5-7.5s3.5 3 3.5 7.5M14.5 15.5c.5-1.3 1.5-2.5 3.5-2.5s3.5 2.5 3.5 6"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1"/>',
  note: '<path d="M9 18V5l11-2v13"/><circle cx="6.5" cy="18" r="2.8"/><circle cx="17.5" cy="16" r="2.8"/>',
  film: '<rect x="3" y="9" width="18" height="12" rx="1.5"/><path d="M3 9l17.2-4.6.7 2.7L3.7 11.7M8 5.8l2 2.9M13 4.5l2 2.9"/>',
  power: '<path d="M12 3v8"/><path d="M6.3 6.8a8 8 0 1 0 11.4 0"/>',
  // eq shapes
  bell: '<path d="M2 18c4 0 5-12 10-12s6 12 10 12"/>',
  lowshelf: '<path d="M2 7h5c3 0 5 10 9 10h6"/>',
  highshelf: '<path d="M2 17h6c4 0 6-10 9-10h5"/>',
  lowcut: '<path d="M3 20c2-12 5-13 9-13h10"/>',
  highcut: '<path d="M2 7h10c4 0 7 1 9 13"/>',
};

export function icon(name, extra = "") {
  return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" ${extra}>${P[name] || P.other}</svg>`;
}

// ---------------------------------------------------------------------------------------------
// Saved UI state (per project, through the plug-in): page, preset, control values.

const saved = { values: {} };
let saveTimer = null;
let saveFn = null;
export const store = {
  load(json) { try { Object.assign(saved, JSON.parse(json || "{}")); saved.values = saved.values || {}; } catch (e) { /* fresh */ } },
  setSaver(fn) { saveFn = fn; },
  get(key, def) { return key in saved ? saved[key] : def; },
  set(key, value) { saved[key] = value; store.dirty(); },
  value(id, def) { return id in saved.values ? saved.values[id] : def; },
  setValue(id, v) { saved.values[id] = v; store.dirty(); },
  dirty() { clearTimeout(saveTimer); saveTimer = setTimeout(() => saveFn && saveFn(JSON.stringify(saved)), 400); },
};

// ---------------------------------------------------------------------------------------------
// Canvas helper: crisp at any window size / display scaling.

let renderScale = 1;
const canvases = new Set();
export function setRenderScale(s) {
  renderScale = s;
  for (const c of canvases) c.resize();
}

export function graph(parent, x, y, w, h, cls = "graph") {
  const wrap = el("div", cls, parent, box(x, y, w, h));
  const canvas = el("canvas", "", wrap);
  const ctx = canvas.getContext("2d");
  const g = {
    wrap, canvas, ctx, w, h, draw: null,
    resize() {
      const k = renderScale * (window.devicePixelRatio || 1);
      canvas.width = Math.round(w * k);
      canvas.height = Math.round(h * k);
      ctx.setTransform(k, 0, 0, k, 0, 0);
      if (g.draw) g.draw(ctx, w, h);
    },
    redraw() { ctx.clearRect(0, 0, w, h); if (g.draw) g.draw(ctx, w, h); },
  };
  canvases.add(g);
  g.resize();
  return g;
}

export function cssVar(name) {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

// Log-frequency axis helpers (20 Hz - 20 kHz).
export const freqToX = (f, w) => (Math.log10(f / 20) / 3) * w;
export const xToFreq = (x, w) => 20 * Math.pow(1000, x / w);

// ---------------------------------------------------------------------------------------------
// Knob: black (or white) metal body, gold edge, glowing arc, white pointer.

let knobCount = 0;
function ensureDefs() {
  if (document.getElementById("mx-defs")) return;
  const s = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  s.id = "mx-defs";
  s.setAttribute("width", "0");
  s.setAttribute("height", "0");
  s.style.position = "absolute";
  s.innerHTML = `<defs>
    <filter id="mx-glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="3.2" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
    <filter id="mx-glow-soft" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="2"/></filter>
  </defs>`;
  document.body.appendChild(s);
}

const START = -135, SWEEP = 270;
function polar(c, r, deg) {
  const a = (deg * Math.PI) / 180;
  return [c + r * Math.sin(a), c - r * Math.cos(a)];
}
function arcPath(c, r, a0, a1) {
  if (Math.abs(a1 - a0) < 0.01) return "";
  const lo = Math.min(a0, a1), hi = Math.max(a0, a1);
  const [x0, y0] = polar(c, r, lo), [x1, y1] = polar(c, r, hi);
  return `M ${x0.toFixed(2)} ${y0.toFixed(2)} A ${r} ${r} 0 ${hi - lo > 180 ? 1 : 0} 1 ${x1.toFixed(2)} ${y1.toFixed(2)}`;
}

/** opts: x, y (top-left of the whole control), size (dial px), label, min, max, value, def, curve ("lin" | "log"),
    format(v), bipolar (arc from the middle; red when below centre), colour (override arc colour), step,
    onChange(v), id (saved under this id), labelGap, valueGap. */
export class Knob {
  constructor(parent, o) {
    ensureDefs();
    this.o = Object.assign({ size: 96, min: 0, max: 1, curve: "lin", format: (v) => v.toFixed(1), labelGap: 8, valueGap: 6 }, o);
    const { size } = this.o;
    this.value = this.o.id ? store.value(this.o.id, this.o.value ?? this.o.def ?? this.o.min) : this.o.value ?? this.o.def ?? this.o.min;
    if (this.o.def === undefined) this.o.def = this.value;

    const root = el("div", "knob", parent, { left: o.x + "px", top: o.y + "px", width: (o.w || size + 40) + "px" });
    root.tabIndex = 0;
    this.root = root;
    if (o.label !== undefined) {
      this.labelEl = el("div", "k-label", root, { marginBottom: this.o.labelGap + "px" });
      this.labelEl.textContent = o.label;
    }
    const dial = el("div", "k-dial", root, { width: size + "px", height: size + "px" });
    this.dial = dial;
    const c = size / 2;
    const ringR = size * 0.445, tickR0 = size * 0.485, tickR1 = size * 0.51;
    const svgNs = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(svgNs, "svg");
    svg.setAttribute("class", "k-ring");
    svg.setAttribute("viewBox", `0 0 ${size} ${size}`);
    let ticks = "";
    for (let i = 0; i <= 30; i++) {
      const a = START + (SWEEP * i) / 30;
      const [x0, y0] = polar(c, tickR0, a), [x1, y1] = polar(c, tickR1, a);
      ticks += `<line x1="${x0.toFixed(1)}" y1="${y0.toFixed(1)}" x2="${x1.toFixed(1)}" y2="${y1.toFixed(1)}"/>`;
    }
    const sw = Math.max(3, size * 0.055);
    svg.innerHTML = `
      <g stroke="var(--tick)" stroke-width="1.2" stroke-linecap="round">${ticks}</g>
      <path d="${arcPath(c, ringR, START, START + SWEEP)}" stroke="var(--knob-track)" stroke-width="${sw}" fill="none" stroke-linecap="round"/>
      <path class="rest" stroke="#2f86ff" stroke-opacity="0.75" stroke-width="${sw * 0.8}" fill="none" stroke-linecap="round" filter="url(#mx-glow-soft)"/>
      <path class="rest2" stroke="#4aa0ff" stroke-opacity="0.85" stroke-width="${sw * 0.55}" fill="none" stroke-linecap="round"/>
      <path class="lit-glow" stroke-width="${sw * 1.6}" fill="none" stroke-linecap="round" filter="url(#mx-glow-soft)" opacity="0.9"/>
      <path class="lit" stroke-width="${sw}" fill="none" stroke-linecap="round"/>`;
    dial.appendChild(svg);
    this.svg = svg;
    this.geom = { c, ringR };

    const gripD = size * 0.76, bodyD = size * 0.64;
    el("div", "k-grip", dial, { left: c - gripD / 2 + "px", top: c - gripD / 2 + "px", width: gripD + "px", height: gripD + "px" });
    el("div", "k-body", dial, { left: c - bodyD / 2 + "px", top: c - bodyD / 2 + "px", width: bodyD + "px", height: bodyD + "px" });
    this.pointer = el("div", "k-pointer", dial);
    this.pointer.style.setProperty("--r", (gripD / 2 - 2) + "px");

    this.valueEl = el("div", "k-value", root, { marginTop: this.o.valueGap + "px" });
    this.bind();
    this.render();
  }

  toNorm(v) {
    const { min, max, curve } = this.o;
    if (curve === "log") return Math.log(v / min) / Math.log(max / min);
    return (v - min) / (max - min);
  }
  fromNorm(n) {
    const { min, max, curve, step } = this.o;
    n = clamp(n, 0, 1);
    let v = curve === "log" ? min * Math.pow(max / min, n) : min + n * (max - min);
    if (step) v = Math.round(v / step) * step;
    return clamp(v, Math.min(min, max), Math.max(min, max));
  }

  set(v, notify = false) {
    this.value = v;
    this.render();
    if (this.o.id) store.setValue(this.o.id, v);
    if (notify && this.o.onChange) this.o.onChange(v);
  }

  setColour(c) { this.o.colour = c; this.render(); }
  setLabel(t) { if (this.labelEl) this.labelEl.textContent = t; }

  render() {
    const n = clamp(this.toNorm(this.value), 0, 1);
    const a = START + SWEEP * n;
    const { c, ringR } = this.geom;
    const mid = START + SWEEP / 2;
    const from = this.o.bipolar ? mid : START;
    const cut = this.o.bipolar && a < mid - 0.5;
    const colour = this.o.colour || (cut ? "#ff2a2a" : "#ffa21f");
    const lit = this.svg.querySelector(".lit"), glow = this.svg.querySelector(".lit-glow");
    const d = arcPath(c, ringR, from, a);
    lit.setAttribute("d", d);
    glow.setAttribute("d", d);
    lit.setAttribute("stroke", colour);
    glow.setAttribute("stroke", colour);
    const restFrom = this.o.bipolar ? Math.max(a, mid) : a;
    const rest = arcPath(c, ringR, restFrom + 3, START + SWEEP);
    this.svg.querySelector(".rest").setAttribute("d", rest);
    this.svg.querySelector(".rest2").setAttribute("d", rest);
    this.pointer.style.transform = `rotate(${a}deg)`;
    this.valueEl.textContent = this.o.format(this.value);
  }

  bind() {
    const d = this.dial;
    let startY = 0, startN = 0, dragging = false;
    d.addEventListener("pointerdown", (e) => {
      dragging = true;
      this.dragging = true;
      startY = e.clientY;
      startN = this.toNorm(this.value);
      d.setPointerCapture(e.pointerId);
      this.root.focus();
      if (this.o.onDragStart) this.o.onDragStart();
    });
    d.addEventListener("pointermove", (e) => {
      if (!dragging) return;
      const scale = (e.shiftKey ? 0.2 : 1) / (220 * renderScale);
      this.set(this.fromNorm(startN + (startY - e.clientY) * scale), true);
    });
    const end = () => { if (dragging && this.o.onDragEnd) this.o.onDragEnd(); dragging = false; this.dragging = false; };
    d.addEventListener("pointerup", end);
    d.addEventListener("pointercancel", end);
    d.addEventListener("dblclick", () => this.set(this.o.def, true));
    d.addEventListener("wheel", (e) => {
      e.preventDefault();
      const stepN = (e.shiftKey ? 0.002 : 0.01) * (e.deltaY < 0 ? 1 : -1);
      this.set(this.fromNorm(this.toNorm(this.value) + stepN), true);
    }, { passive: false });
    this.root.addEventListener("keydown", (e) => {
      const k = { ArrowUp: 0.01, ArrowRight: 0.01, ArrowDown: -0.01, ArrowLeft: -0.01 }[e.key];
      if (k === undefined) return;
      e.preventDefault();
      this.set(this.fromNorm(this.toNorm(this.value) + k * (e.shiftKey ? 0.2 : 1)), true);
    });
  }
}

// ---------------------------------------------------------------------------------------------
// Vertical meter: rainbow bar, segment lines, peak hold.

export class Meter {
  constructor(parent, x, y, w, h, range = [-60, 6]) {
    this.root = el("div", "meter", parent, box(x, y, w, h));
    this.fill = el("div", "m-fill", this.root, { height: "0%", backgroundSize: `100% ${h}px` });
    this.peakEl = el("div", "m-peak", this.root, { bottom: "-4px" });
    el("div", "m-seg", this.root);
    this.range = range;
    this.h = h;
    this.level = -100;
    this.peak = -100;
    this.peakTime = 0;
  }
  pos(db) { const [lo, hi] = this.range; return clamp((db - lo) / (hi - lo), 0, 1); }
  set(db) {
    const now = performance.now();
    // Fast attack, ~20 dB/s fall for a readable bar.
    this.level = db > this.level ? db : Math.max(db, this.level - 0.7);
    if (db >= this.peak || now - this.peakTime > 1200) {
      if (db >= this.peak) { this.peak = db; this.peakTime = now; }
      else this.peak = Math.max(db, this.peak - 0.5);
    }
    this.fill.style.height = (this.pos(this.level) * 100).toFixed(2) + "%";
    this.peakEl.style.bottom = (this.pos(this.peak) * this.h - 1).toFixed(1) + "px";
    this.peakEl.style.display = this.peak > this.range[0] ? "block" : "none";
  }
}

// ---------------------------------------------------------------------------------------------
// Dropdown

let openMenu = null;
document.addEventListener("pointerdown", (e) => {
  if (openMenu && !openMenu.contains(e.target)) { openMenu.remove(); openMenu = null; }
});

/** Keeps a menu on the stage: opens it upward when there is more room above its button, and makes it
    scroll when it is still too long. r = the button's rectangle, sr = the stage's, k = the stage's scale. */
function fitMenu(m, r, sr, k, left) {
  const below = (sr.bottom - r.bottom) / k - 16, above = (r.top - sr.top) / k - 16;
  const h = m.offsetHeight;
  if (h > below && above > below) {
    const maxH = Math.min(h, above);
    m.style.maxHeight = maxH + "px";
    m.style.top = (r.top - sr.top) / k - 4 - maxH + "px";
  } else if (h > below) m.style.maxHeight = Math.max(120, below) + "px";
  m.style.overflowY = "auto";
  const mr = m.getBoundingClientRect();
  if (mr.right > sr.right) m.style.left = Math.max(8, left - (mr.right - sr.right) / k - 8) + "px";
  m.querySelector(".sel")?.scrollIntoView({ block: "nearest" });
}

export function select(parent, x, y, w, options, value, onChange, opts = {}) {
  const s = el("div", "select", parent, { left: x + "px", top: y + "px", width: w + "px", height: (opts.h || 44) + "px" });
  const text = el("span", "", s);
  s.insertAdjacentHTML("beforeend", icon("chevD"));
  if (opts.dot) {
    const dot = el("span", "", null, { width: "14px", height: "14px", borderRadius: "50%", background: opts.dot, marginRight: "10px", boxShadow: "0 0 6px " + opts.dot });
    s.insertBefore(dot, text);
    text.style.flex = "1";
  }
  const api = {
    el: s, value,
    set(v) { api.value = v; text.textContent = (options.find((o) => (o.value ?? o) === v)?.label) ?? v; },
    setOptions(list) { options = list; api.set(api.value); },
  };
  api.set(value);
  s.addEventListener("click", () => {
    if (openMenu) { openMenu.remove(); openMenu = null; return; }
    const stage = document.getElementById("stage");
    const r = s.getBoundingClientRect(), sr = stage.getBoundingClientRect();
    const k = sr.width / 1440;
    const left = (r.left - sr.left) / k;
    const m = el("div", "menu scroll", stage, { left: left + "px", top: (r.bottom - sr.top) / k + 4 + "px", minWidth: w + "px" });
    for (const o of options) {
      const v = o.value ?? o, label = o.label ?? o;
      const item = el("div", v === api.value ? "sel" : "", m);
      item.textContent = label;
      item.addEventListener("click", () => { api.set(v); m.remove(); openMenu = null; if (onChange) onChange(v); });
    }
    fitMenu(m, r, sr, k, left);
    openMenu = m;
  });
  return api;
}

/** A menu that pops up under `anchor`. items: { label, value, icon?, colour?, sel?, divider? }. */
export function popupMenu(anchor, items, onPick, width = 240) {
  if (openMenu) { openMenu.remove(); openMenu = null; }
  const stage = document.getElementById("stage");
  const r = anchor.getBoundingClientRect(), sr = stage.getBoundingClientRect();
  const k = sr.width / 1440;
  let left = (r.left - sr.left) / k, top = (r.bottom - sr.top) / k + 4;
  const m = el("div", "menu scroll", stage, { left: left + "px", top: top + "px", minWidth: width + "px" });
  for (const it of items) {
    if (it.divider) { el("div", "", m, { height: "1px", margin: "6px 4px", padding: "0", background: "var(--panel-edge)", pointerEvents: "none" }); continue; }
    const item = el("div", it.sel ? "sel" : "", m, { display: "flex", alignItems: "center", gap: "10px" });
    if (it.icon) {
      const ic = el("span", "", item, { width: "24px", height: "24px", flex: "none", color: it.colour || "var(--text)" });
      ic.innerHTML = icon(it.icon);
    }
    el("span", "", item).textContent = it.label;
    item.addEventListener("click", () => { m.remove(); openMenu = null; onPick(it.value); });
  }
  fitMenu(m, r, sr, k, left);
  setTimeout(() => { openMenu = m; }, 0);   // the click that opened it must not close it
  return m;
}

// ---------------------------------------------------------------------------------------------
// Left-column module list (one active, red)

export function moduleList(parent, items, x, y, w, h, gap, active, onSelect) {
  const buttons = items.map((it, i) => {
    const b = el("button", "btn", parent, box(x, y + i * (h + gap), w, h));
    b.innerHTML = icon(it.icon) + `<span>${it.label}</span>` + (it.lamp ? '<i class="lamp"></i>' : "");
    b.addEventListener("click", () => { set(it.id); onSelect(it.id); });
    b.dataset.id = it.id;
    return b;
  });
  const set = (id) => buttons.forEach((b) => b.classList.toggle("active", b.dataset.id === id));
  set(active);
  /** Lamps (modules with a processing switch): green when on, pulsing while it works on the sound. */
  const lamps = (isOn, isLive) => buttons.forEach((b) => {
    const l = b.querySelector(".lamp");
    if (!l) return;
    l.classList.toggle("on", !!isOn(b.dataset.id));
    l.classList.toggle("live", !!isLive(b.dataset.id));
  });
  return { set, buttons, lamps };
}

export function previewTag(parent, x, y, text = "Preview") {
  const t = el("div", "preview-tag", parent, { left: x + "px", top: y + "px" });
  t.textContent = text;
  t.title = "This page's controls are laid out and working, but don't process audio yet. They go live in a coming milestone (see the manual).";
  return t;
}
