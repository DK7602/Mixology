// Mixology Link window: the track this Link sits on, its meters and Trim, and whether Mixology sees it.

import * as Juce from "./juce/index.js";
import { $, el, Knob, Meter, fmt, setRenderScale } from "./ui.js";

const stage = $("lstage");
function fit() {
  const s = Math.min(window.innerWidth / 600, window.innerHeight / 400);
  stage.style.transform = `scale(${s})`;
  stage.style.left = Math.max(0, (window.innerWidth - 600 * s) / 2) + "px";
  stage.style.top = Math.max(0, (window.innerHeight - 400 * s) / 2) + "px";
  setRenderScale(s);
}
window.addEventListener("resize", fit);

const fn = (name) => { const f = Juce.getNativeFunction(name); return (...a) => f(...a).catch(() => undefined); };
const native = { getLinkInfo: fn("getLinkInfo"), setTrim: fn("setTrim"), trimGesture: fn("trimGesture") };

// Light / dark: follows Mixology's saved choice; the logo switches this window.
$("logoBtn").addEventListener("click", () => {
  const root = document.documentElement;
  root.dataset.theme = root.dataset.theme === "light" ? "dark" : "light";
});

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
  document.documentElement.dataset.theme = info.theme === "light" ? "light" : "dark";
  if (typeof info.trim === "number") trim.set(info.trim);
  fit();
  window.__JUCE__.backend.addEventListener("linkFrame", frame);
}
start();
