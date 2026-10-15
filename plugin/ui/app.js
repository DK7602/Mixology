// Mixology interface: page switching, light / dark look, mix style picker, overlays, and the link
// to the plug-in (native functions + the 30 Hz "mixFrame" event).

import * as Juce from "./juce/index.js";
import { $, el, icon, store, setRenderScale } from "./ui.js";
import { buildOverview, buildTracks } from "./pages_main.js";
import { buildEq, buildDynamics, buildSpace } from "./pages_process.js";
import { buildTone, buildStereo, buildMaster } from "./pages_output.js";
import { openSettings, openHelp, openPresets, openStyles, openHistory, hookBridge } from "./sheets.js";
import { hookTools } from "./tools.js";
import { styleByKey } from "./mixstyles.js";
import { fillDaw, dawOf } from "./daws.js";
import { createProc } from "./proc.js";

// ---------------------------------------------------------------------------------------------
// Fit the 1440 x 1080 design to the window.

const stage = $("stage");
function fit() {
  const s = Math.min(window.innerWidth / 1440, window.innerHeight / 1080);
  stage.style.transform = `scale(${s})`;
  stage.style.left = Math.max(0, (window.innerWidth - 1440 * s) / 2) + "px";
  stage.style.top = Math.max(0, (window.innerHeight - 1080 * s) / 2) + "px";
  setRenderScale(s);
}
window.addEventListener("resize", fit);

// ---------------------------------------------------------------------------------------------
// Native functions

// Text from the engine carries {daw} / {daw:phrase} tokens: they become your DAW's words here, as it arrives.
function withDaw(v, depth = 0) {
  if (typeof v === "string") return v.indexOf("{daw") < 0 ? v : fillDaw(v, app.daw);
  if (!v || typeof v !== "object" || depth > 8) return v;
  if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) if (typeof v[i] !== "number") v[i] = withDaw(v[i], depth + 1); return v; }
  for (const k of Object.keys(v)) if (typeof v[k] !== "number") v[k] = withDaw(v[k], depth + 1);
  return v;
}
const fn = (name) => {
  const f = Juce.getNativeFunction(name);
  return (...args) => f(...args).then((r) => withDaw(r)).catch(() => undefined);
};
export const native = {
  bridgeCommand: fn("bridgeCommand"),
  getBridgeState: fn("getBridgeState"),
  getSettings: fn("getSettings"),
  setSetting: fn("setSetting"),
  setUiState: fn("setUiState"),
  pickStyle: fn("pickStyle"),
  pickDaw: fn("pickDaw"),
  setPage: fn("setPage"),
  procTarget: fn("procTarget"),
  procSet: fn("procSet"),
  setMonoListen: fn("setMonoListen"),
  spaceDuck: fn("spaceDuck"),
  presetList: fn("presetList"),
  presetApply: fn("presetApply"),
  presetSave: fn("presetSave"),
  presetDelete: fn("presetDelete"),
  presetUndo: fn("presetUndo"),
};

// ---------------------------------------------------------------------------------------------
// Shared context for the pages

const listeners = { frame: [], bridge: [], theme: [], preset: [], daw: [] };
const lastToast = { text: "", time: 0 };
export const app = {
  native,
  bridge: null,       // latest bridge state (see BridgeHub::getState)
  frame: null,        // latest meter frame
  style: styleByKey(0),   // the mix style (see mixstyles.js)
  daw: "cubase",          // the DAW you work in (daws.js): found from the host, or picked in any of the three
  dawPicked: false,
  on(kind, f) { listeners[kind].push(f); },
  emit(kind, data) { for (const f of listeners[kind]) f(data); },
  showPage,
  openSettings: (tab) => openSettings(app, tab),
  command: async (name, arg) => {
    const r = await native.bridgeCommand(name, arg === undefined ? 0 : arg);
    if (r && r.ok === false && r.message) app.toast(r.message);
    else if (r && r.warning) app.toast(r.warning);
    else if (r && r.info) app.toast(r.info);
    return r;
  },
  toast(text) {
    // The same message again within 2 s (e.g. while dragging a protected channel's knob) is shown once.
    const now = performance.now();
    if (text === lastToast.text && now - lastToast.time < 2000) return;
    lastToast.text = text;
    lastToast.time = now;
    const t = el("div", "", stage, {
      position: "absolute", left: "50%", bottom: "28px", transform: "translateX(-50%)", zIndex: 200,
      padding: "12px 22px", borderRadius: "12px", fontSize: "19px", maxWidth: "900px", textAlign: "center",
      background: "rgba(10,14,22,0.94)", color: "#fff", border: "1.5px solid #e3b260", boxShadow: "0 8px 24px rgba(0,0,0,0.5)",
    });
    t.textContent = text;
    setTimeout(() => t.remove(), 3800);
  },
};

// ---------------------------------------------------------------------------------------------
// Theme: click the beaker logo.

function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  app.emit("theme", theme);
}
$("logoBtn").addEventListener("click", () => {
  const next = document.documentElement.dataset.theme === "light" ? "dark" : "light";
  applyTheme(next);
  native.setSetting("theme", next);
  try { localStorage.setItem("mixology-theme", next); } catch (e) { /* preview only */ }
});

// ---------------------------------------------------------------------------------------------
// Pages

const pages = {};
let current = null;
function showPage(id) {
  if (!pages[id]) return;
  if (current && current !== id && pages[current].onHide) pages[current].onHide();
  current = id;
  for (const [key, p] of Object.entries(pages)) p.root.classList.toggle("shown", key === id);
  document.querySelectorAll(".tab[data-page]").forEach((t) => t.classList.toggle("active", t.dataset.page === id));
  store.set("page", id);
  native.setPage(id);
  if (pages[id].onShow) pages[id].onShow();
}
document.querySelectorAll(".tab[data-page]").forEach((t) => t.addEventListener("click", () => showPage(t.dataset.page)));

$("tab-settings").innerHTML = icon("gear");
$("tab-help").innerHTML = icon("help", 'stroke-width="2.4"');
$("tab-settings").addEventListener("click", () => openSettings(app));
$("tab-help").addEventListener("click", () => openHelp(app));
$("tab-presets").addEventListener("click", () => openPresets(app));
$("tab-history").innerHTML = icon("history", 'stroke-width="2.2"');
$("tab-history").addEventListener("click", () => openHistory(app));
$("sheet-close").innerHTML = icon("close", 'stroke-width="2.6"');
$("sheet-close").addEventListener("click", () => $("overlay").classList.remove("shown"));
$("overlay").addEventListener("pointerdown", (e) => { if (e.target.id === "overlay") $("overlay").classList.remove("shown"); });
document.addEventListener("keydown", (e) => { if (e.key === "Escape") $("overlay").classList.remove("shown"); });

// Mix style picker in the header: the arrows step through the styles of the current genre;
// clicking the name opens the genre / style picker.
$("preset-prev").innerHTML = icon("chevL", 'stroke-width="2.6"');
$("preset-next").innerHTML = icon("chevR", 'stroke-width="2.6"');
// how: "you" (picked here: Voxology and Masterology follow), "suite" (picked in one of them: follow it), "start".
function setStyle(key, how = "you") {
  app.style = styleByKey(key);
  const name = $("preset-name");
  name.textContent = app.style.fullName;
  name.title = app.style.about;
  name.style.fontSize = "";
  for (const size of [20, 18, 16]) if (name.scrollWidth > name.clientWidth) name.style.fontSize = size + "px";
  store.set("preset", app.style.key);
  store.set("presetName", app.style.fullName);   // the plug-in hands the style to Masterology (M6)
  app.emit("preset", app.style);
  if (how !== "suite") native.pickStyle(app.style.key, app.style.fullName, how === "you");
  // Auto Analyze listens for this style (also when the project opens with it on and no advice yet).
  const st = app.style;
  native.bridgeCommand("autoStyle", { value: { key: st.key, name: st.fullName, bass: st.bass, scoop: st.scoop, air: st.air } });
}
function stepStyle(delta) {
  const list = app.style.genre.styles;
  const i = list.findIndex((s) => s.id === app.style.id);
  setStyle(`${app.style.genre.id}.${list[(i + delta + list.length) % list.length].id}`);
}
app.setStyle = setStyle;
$("preset-prev").addEventListener("click", () => stepStyle(-1));
$("preset-next").addEventListener("click", () => stepStyle(1));
$("preset-name").addEventListener("click", () => openStyles(app));

// ---------------------------------------------------------------------------------------------
// Start

async function start() {
  const settings = (await native.getSettings()) || {};
  let theme = settings.theme;
  if (!theme) { try { theme = localStorage.getItem("mixology-theme"); } catch (e) { /* preview */ } }
  applyTheme(theme === "light" ? "light" : "dark");
  store.load(settings.ui);
  store.setSaver((json) => native.setUiState(json));
  hookBridge(app);
  hookTools(app);
  app.proc = createProc(app);   // the processing target and controls the EQ / Dynamics / Tone / Stereo pages share

  const build = {
    overview: buildOverview, tracks: buildTracks, eq: buildEq, dynamics: buildDynamics, space: buildSpace,
    tone: buildTone, stereo: buildStereo, master: buildMaster,
  };
  for (const [id, b] of Object.entries(build)) {
    const root = $("page-" + id);
    pages[id] = Object.assign({ root }, b(root, app) || {});
  }
  fit();
  setStyle(store.get("preset", 0), "start");
  showPage(store.get("page", "overview"));

  const bridge = await native.getBridgeState();
  if (bridge) { app.bridge = bridge; app.emit("bridge", bridge); }

  let styleSeen = -1;
  window.__JUCE__.backend.addEventListener("mixFrame", (frame) => {
    app.frame = frame;
    if (frame.daw && (frame.daw.key !== app.daw || !!frame.daw.picked !== app.dawPicked)) {
      app.daw = frame.daw.key || "other";
      app.dawPicked = !!frame.daw.picked;
      app.dawDetected = frame.daw.detected;
      if (frame.daw.picked && frame.daw.from !== 2 && performance.now() > 5000) app.toast(`DAW: ${dawOf(app.daw).name} (picked in another plug-in of the suite)`);
      app.emit("daw", app.daw);
      native.getBridgeState().then((b) => { if (b) { app.bridge = b; app.emit("bridge", b); } });   // its text again, in this DAW's words
    }
    if (frame.bridge) withDaw(frame.bridge);
    // The song's style, shared with Voxology and Masterology: the project's, or one picked in one of them.
    const ss = frame.songStyle;
    if (ss && ss.version !== styleSeen) {
      styleSeen = ss.version;
      if (ss.key && ss.key !== app.style.key) {
        setStyle(ss.key, "suite");
        if (performance.now() > 5000) app.toast(`Style: ${app.style.fullName} (picked in another plug-in of the suite)`);
      }
    }
    if (frame.bridge) {
      if (!frame.bridge.log && app.bridge && app.bridge.log) frame.bridge.log = app.bridge.log;
      app.bridge = frame.bridge;
      app.emit("bridge", frame.bridge);
    }
    app.proc.onFrame(frame);
    for (const [id, p] of Object.entries(pages)) if (p.onFrame && (id === current || p.always)) p.onFrame(frame);
    app.emit("frame", frame);
  });
}
start();
