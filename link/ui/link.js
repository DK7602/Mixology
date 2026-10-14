// Mixology Link window: the track this Link sits on, its meters, Trim and Assist EQ, and whether Mixology sees it.

import * as Juce from "./juce/index.js";
import { $, el, Knob, Meter, fmt, setRenderScale } from "./ui.js";

const stage = $("lstage");
function fit() {
  const s = Math.min(window.innerWidth / 600, window.innerHeight / 536);
  stage.style.transform = `scale(${s})`;
  stage.style.left = Math.max(0, (window.innerWidth - 600 * s) / 2) + "px";
  stage.style.top = Math.max(0, (window.innerHeight - 536 * s) / 2) + "px";
  setRenderScale(s);
}
window.addEventListener("resize", fit);

const fn = (name) => { const f = Juce.getNativeFunction(name); return (...a) => f(...a).catch(() => undefined); };
const native = { getLinkInfo: fn("getLinkInfo"), setTrim: fn("setTrim"), trimGesture: fn("trimGesture"), clearAssist: fn("clearAssist"), setProcessing: fn("setProcessing") };

// Light / dark: follows Mixology's saved choice; the logo switches this window.
function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
}
$("logoBtn").addEventListener("click", () => applyTheme(document.documentElement.dataset.theme === "light" ? "dark" : "light"));

// Meters: L / R bars with a dB scale, plus the readings.
const meters = $("lmeters");
el("div", "label", meters, { position: "absolute", left: "14px", top: "10px", fontSize: "15px" }).textContent = "Level";
const RANGE = [-60, 6];
const mL = new Meter(meters, 40, 40, 16, 150, RANGE);
const mR = new Meter(meters, 62, 40, 16, 150, RANGE);
for (const db of [0, -12, -24, -36, -48, -60]) {
  const y = 40 + (1 - (db - RANGE[0]) / (RANGE[1] - RANGE[0])) * 150 - 7;
  el("div", "lscale", meters, { left: "12px", top: y + "px" }).textContent = db === 0 ? "0" : "−" + Math.abs(db);
}
const rPeak = el("div", "lreading", meters, { top: "38px" });
const rMom = el("div", "lreading", meters, { top: "92px" });
const rShort = el("div", "lreading", meters, { top: "146px" });

// Trim knob
const trimCard = $("ltrim");
let dragging = false;
const trim = new Knob(trimCard, {
  x: 38, y: 8, w: 180, size: 100, label: "TRIM", min: -24, max: 24, def: 0, value: 0, bipolar: true, step: 0.1,
  format: (v) => fmt.db(v, 1),
  onChange: (v) => native.setTrim(v),
  onDragStart: () => { dragging = true; native.trimGesture(true); },
  onDragEnd: () => { dragging = false; native.trimGesture(false); },
});
const note = el("div", "lnote", trimCard);
note.textContent = "Before the fader · double-click: 0 dB";

// Assist EQ: what Mixology's Clean Up and Vocal Pocket set on this track.
const assistCard = $("lassist");
el("div", "ltitle", assistCard).textContent = "ASSIST EQ";
const assistText = el("div", "ltext", assistCard);
const assistCanvas = el("canvas", "", assistCard);
assistCanvas.width = 280; assistCanvas.height = 88;
const clearBtn = el("button", "small-btn", assistCard);
clearBtn.textContent = "Clear";
clearBtn.title = "Take the Assist EQ out (Low Cut off, no dips)";
clearBtn.addEventListener("click", () => native.clearAssist());
let assistKey = "";
const hzText = (hz) => hz >= 1000 ? fmt.num(hz / 1000, hz >= 10000 ? 0 : 1) + " kHz" : Math.round(hz) + " Hz";

function biquadDb(type, f0, db, q, f, sr = 48000) {
  const w0 = 2 * Math.PI * f0 / sr, cw = Math.cos(w0), al = Math.sin(w0) / (2 * q), A = Math.pow(10, db / 40);
  let b0, b1, b2, a0, a1, a2;
  if (type === "hp") { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
  else { b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
  const w = 2 * Math.PI * f / sr, c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
  const nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2), dr = a0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

function drawAssist(a) {
  const on = a.lowCut >= 20, mud = a.mudDb < -0.05, pocket = a.pocketDb < -0.05;
  const parts = [];
  if (on) parts.push("Low cut " + hzText(a.lowCut));
  if (mud) parts.push(`Mud ${fmt.num(a.mudDb, 1)} dB at ${hzText(a.mudHz)}`);
  if (pocket) parts.push(`Pocket ${fmt.num(a.pocketDb, 1)} dB at ${hzText(a.pocketHz)}`);
  assistText.textContent = parts.length ? parts.join(" · ") : "Off. Mixology's Clean Up and Vocal Pocket set it.";
  assistText.style.color = parts.length ? "var(--text)" : "var(--text-dim)";
  clearBtn.disabled = !parts.length;
  clearBtn.style.opacity = parts.length ? "1" : "0.4";

  const ctx = assistCanvas.getContext("2d"), W = assistCanvas.width, H = assistCanvas.height;
  ctx.clearRect(0, 0, W, H);
  ctx.strokeStyle = "rgba(128,140,160,0.35)"; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(0, H * 0.3); ctx.lineTo(W, H * 0.3); ctx.stroke();
  ctx.strokeStyle = parts.length ? "#19d3ff" : "rgba(128,140,160,0.6)"; ctx.lineWidth = 3;
  ctx.beginPath();
  for (let i = 0; i <= 100; i++) {
    const f = 20 * Math.pow(1000, i / 100);
    let db = 0;
    if (on) db += biquadDb("hp", a.lowCut, 0, 0.7071, f);
    if (mud) db += biquadDb("pk", a.mudHz, a.mudDb, 1.0, f);
    if (pocket) db += biquadDb("pk", a.pocketHz, a.pocketDb, 1.2, f);
    const y = H * 0.3 - Math.max(-24, db) / 24 * (H * 0.66);
    i ? ctx.lineTo(i / 100 * W, y) : ctx.moveTo(0, y);
  }
  ctx.stroke();
}

// Processing: what Mixology's EQ, Dynamics, Tone and Stereo pages switched on for this track.
const procCard = $("lproc");
el("div", "ltitle", procCard).textContent = "PROCESSING";
const procText = el("div", "ltext", procCard);
const procBtn = el("button", "small-btn", procCard);
let procOn = true, procKey = "";
procBtn.addEventListener("click", () => native.setProcessing(!procOn));
const MODULE_NAMES = { gate: "Gate", eq: "EQ", comp: "Compressor", multi: "Multiband", trans: "Transient", balance: "Tone Balance",
  loud: "Loudness", harm: "Harmonics", analog: "Analog", clip: "Clip", width: "Stereo", final: "Final Touch", limit: "Limiter" };
function drawProc(p) {
  procOn = !!p.on;
  procBtn.textContent = procOn ? "On" : "Off";
  procBtn.title = procOn ? "Switch all processing off (compare with the plain track)" : "Switch the processing back on";
  procBtn.style.color = procOn ? "var(--green)" : "var(--text-dim)";
  if (!p.modules.length) { procText.textContent = "Nothing on. Mixology's EQ, Dynamics, Tone and Stereo pages work here."; procText.style.color = "var(--text-dim)"; return; }
  procText.style.color = procOn ? "var(--text)" : "var(--text-dim)";
  procText.innerHTML = p.modules.map((m) => `<span class="lchip${p.running.includes(m) ? " live" : ""}">${MODULE_NAMES[m] || m}</span>`).join("")
    + (p.latencyMs > 0 ? `<span style="color:var(--text-dim)">${fmt.num(p.latencyMs, 1)} ms lookahead</span>` : "");
}

let peakHold = -100, peakTime = 0;
function frame(f) {
  mL.set(f.peakL);
  mR.set(f.channels === 1 ? f.peakL : f.peakR);
  const p = Math.max(f.peakL, f.channels === 1 ? f.peakL : f.peakR);
  const now = performance.now();
  if (p >= peakHold || now - peakTime > 2000) { peakHold = p; peakTime = now; }
  rPeak.innerHTML = `Peak<b style="color:${peakHold > -1 ? "var(--red)" : "var(--text)"}">${peakHold > -99 ? fmt.signed(peakHold, 1) + " dB" : "−∞"}</b>`;
  rMom.innerHTML = `Loudness now<b>${f.momentary > -99 ? fmt.num(f.momentary, 1) + " LUFS" : "—"}</b>`;
  rShort.innerHTML = `Last 3 seconds<b>${f.shortTerm > -99 ? fmt.num(f.shortTerm, 1) + " LUFS" : "—"}</b>`;

  if (!dragging && Math.abs(trim.value - f.trim) > 0.04) trim.set(f.trim);

  $("lname").textContent = f.name || "Track name not reported";
  $("lname").style.color = f.name ? "var(--text)" : "var(--text-dim)";
  $("lcolour").style.background = f.colour || "var(--text-faint)";
  $("lsub").textContent = f.note || (f.channels === 1 ? "Mono track" : "Stereo track") + " · put Mixology Link after your other plug-ins on this track";

  if (f.proc) {
    const key = JSON.stringify(f.proc);
    if (key !== procKey) { procKey = key; drawProc(f.proc); }
  }

  if (f.assist) {
    const key = JSON.stringify(f.assist);
    if (key !== assistKey) { assistKey = key; drawAssist(f.assist); }
  }

  const st = $("lstatus");
  const [colour, text] = !f.shared ? ["var(--red)", "Can't share data with Mixology on this computer"]
    : !f.mixology ? ["var(--amber)", "Mixology isn't open. Insert it on the Stereo Out"]
    : f.paired === "matched" ? ["var(--green)", "Paired: live on Mixology's Tracks page"]
    : f.paired === "ambiguous" ? ["var(--amber)", "Two tracks have this name: rename one so Mixology can pair it"]
    : ["var(--cyan)", "Mixology is open. Pairs when this track is in its Tracks view"];
  st.innerHTML = `<span class="dot" style="background:${colour};box-shadow:0 0 8px ${colour}"></span><span>${text}</span>`;
}

async function start() {
  const info = (await native.getLinkInfo()) || {};
  applyTheme(info.theme === "light" ? "light" : "dark");
  if (typeof info.trim === "number") trim.set(info.trim);
  fit();
  window.__JUCE__.backend.addEventListener("linkFrame", frame);
}
start();
