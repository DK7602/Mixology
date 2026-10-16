// Overlays: Settings (Cubase Bridge, Mixology Link, display), the built-in manual, and mix styles.

import { $, el, icon, fmt, popupMenu } from "./ui.js";
import { GENRES } from "./mixstyles.js";
import { DAWS, dawOf, dawName, say, hasBridge } from "./daws.js";
import { ROLES, ROLE_ORDER, roleIcon } from "./roles.js";
import { openToolSheet } from "./tools.js";

// The smart tools by the interface's id (the Mix Assistant's points link to the one that fixes them).
const TOOL_NAMES = { auto: "Auto Mix", polish: "Polish", balance: "Balance", clean: "Clean Up", pocket: "Vocal Pocket", stereo: "Stereo Architect" };

let historyRender = null;

export function sheet(title) {
  const pop = $("style-pop");
  if (pop) pop.remove();
  historyRender = null;
  $("sheet-title").textContent = title;
  const body = $("sheet-body");
  body.innerHTML = "";
  body.className = "";
  body.removeAttribute("style");
  $("overlay").classList.add("shown");
  return body;
}

// ---------------------------------------------------------------------------------------------
// Settings

let bridgeRender = null;

export function openSettings(app, tab = "bridge") {
  const body = sheet("SETTINGS");
  dawRow(app, body);
  const tabs = el("div", "", body, { display: "flex", gap: "10px", marginBottom: "18px" });
  const content = el("div", "", body);
  const names = [["bridge", `${hasBridge(app.daw) ? dawName(app.daw) : "DAW"} Bridge`], ["display", "Display"], ["about", "About"]];
  const chips = names.map(([id, label]) => {
    const c = el("div", "chip", tabs);
    c.textContent = label;
    c.addEventListener("click", () => show(id));
    c.dataset.id = id;
    return c;
  });
  function show(id) {
    chips.forEach((c) => c.classList.toggle("on", c.dataset.id === id));
    content.innerHTML = "";
    bridgeRender = null;
    if (id === "bridge") bridgePanel(app, content);
    else if (id === "display") displayPanel(app, content);
    else aboutPanel(app, content);
  }
  show(tab);
}

// Your DAW: found from the host Mixology runs in, or picked here (or in Voxology / Masterology: all three follow).
// The Mix Assistant, the tools and every tip then tell you how to do things in that DAW.
function dawRow(app, root) {
  const row = el("div", "card", root, { position: "relative", display: "flex", alignItems: "center", gap: "14px", padding: "12px 18px", marginBottom: "16px", fontSize: "19px" });
  el("span", "", row, { fontWeight: "600", color: "var(--gold)" }).textContent = "DAW";
  const sel = el("select", "", row, { height: "40px", fontSize: "18px", padding: "0 10px", borderRadius: "9px", border: "1.5px solid var(--select-edge)", background: "var(--well)", color: "var(--text)", font: "inherit" });
  const found = dawOf(app.dawDetected || app.daw);
  const auto = el("option", "", sel);
  auto.value = "";
  auto.textContent = `Found: ${found.key === "other" ? "not known" : found.name}`;
  for (const d of DAWS) {
    const o = el("option", "", sel);
    o.value = d.key;
    o.textContent = d.name + (d.also ? ` (${d.also})` : "");
  }
  sel.value = app.dawPicked ? app.daw : "";
  const note = el("span", "", row, { flex: "1", fontSize: "16px", color: "var(--text-dim)", lineHeight: "1.3" });
  const explain = () => {
    note.textContent = (hasBridge(app.daw) ? `Tips and fixes are written for ${dawOf(app.daw).name}; Mixology moves its faders through the bridge below. ` : `Tips and fixes are written for ${dawOf(app.daw).name}. Moving faders (the Tracks page, Auto Mix's fader moves) works in Cubase, FL Studio and Ableton Live for now. `)
      + "Voxology and Masterology follow your pick.";
  };
  explain();
  sel.addEventListener("change", () => { app.native.pickDaw(sel.value); app.daw = sel.value || found.key; app.dawPicked = !!sel.value; explain(); });
}

function displayPanel(app, root) {
  const row = el("div", "", root, { display: "flex", gap: "14px", alignItems: "center", fontSize: "20px" });
  el("span", "", row).textContent = "Look:";
  for (const t of ["dark", "light"]) {
    const b = el("button", "small-btn", row);
    b.textContent = t === "dark" ? "Dark" : "Light";
    b.addEventListener("click", () => { if (document.documentElement.dataset.theme !== t) $("logoBtn").click(); });
  }
  const p = el("p", "", root, { fontSize: "18px", color: "var(--text-dim)", lineHeight: "1.5", marginTop: "18px" });
  p.innerHTML = "Tip: click the beaker logo to switch looks any time. The choice is remembered for every project.<br>Drag the window's corner to resize; the interface keeps its proportions and stays sharp at any Windows display scaling.";
}

function aboutPanel(app, root) {
  const p = el("div", "", root, { fontSize: "19px", lineHeight: "1.6" });
  p.innerHTML = `<b>Mixology</b>: intelligent mixing engineer. Part of the suite: Voxology (vocals), Mixology (the mix), Masterology (mastering).<br>
    This build: the full interface, live mix meters, loudness, vectorscope, the Cubase bridge (every fader in view), Mixology Link on each track, and Analyze Mix with the Mix Assistant (Milestone 3).<br>
    <span class="dim">Fonts: Barlow Semi Condensed (SIL Open Font License).</span>`;
}

const isMac = /Mac/i.test(navigator.platform || navigator.userAgent || "");

// The bridge's one-time setup in each DAW that has one (the Sonicology installer puts every script in place).
function setupSteps(daw) {
  const ports = isMac
    ? `<li>The two MIDI ports: nothing to do. On a Mac, Mixology makes <code>Mixology to Cubase</code> and <code>Cubase to Mixology</code> itself once its window has been open (the names say Cubase; they work for every DAW).</li>`
    : `<li>Install <b>loopMIDI</b> (free, by Tobias Erichsen). Add two ports named exactly <code>Mixology to Cubase</code> and <code>Cubase to Mixology</code> (the names say Cubase; they work for every DAW). Keep loopMIDI running (it can start with Windows).</li>`;
  const ol = (items) => `<ol style="margin:10px 0 4px 0;padding-left:24px">${ports}${items}</ol>`;
  if (daw === "fl") return ol(`
      <li>The Sonicology installer puts the script in ${isMac ? "<code>~/Documents/Image-Line/FL Studio/Settings/Hardware/Mixology Bridge</code>" : "<code>Documents\\Image-Line\\FL Studio\\Settings\\Hardware\\Mixology Bridge</code>"}.</li>
      <li>In FL Studio: <b>Options &gt; MIDI settings</b>. In the Input list pick <code>Mixology to Cubase</code>, tick <b>Enable</b>, set <b>Controller type</b> to <b>Mixology Bridge</b> and <b>Port</b> to <b>200</b>. In the Output list pick <code>Cubase to Mixology</code> and set its <b>Port</b> to <b>200</b> too (the same number: FL sends the script's answers there).</li>
      <li>Open Mixology (on the Master). This panel turns green. Mixology's strips are FL's mixer inserts in use (renamed ones, or ones with an effect such as Mixology Link).</li>`);
  if (daw === "ableton") return ol(`
      <li>The Sonicology installer puts the script in ${isMac ? "<code>~/Music/Ableton/User Library/Remote Scripts/MixologyBridge</code>" : "<code>Documents\\Ableton\\User Library\\Remote Scripts\\MixologyBridge</code>"}. Restart Live after installing.</li>
      <li>In Live: <b>Settings &gt; Link, Tempo &amp; MIDI</b>. In the first free <b>Control Surface</b> row pick <b>MixologyBridge</b>, Input <code>Mixology to Cubase</code>, Output <code>Cubase to Mixology</code>.</li>
      <li>Open Mixology (on the Main track). This panel turns green. Mixology's strips are Live's tracks and return tracks.</li>`);
  if (daw === "cubase") return ol(`
      <li>The Sonicology installer puts the Cubase script in place. Without it: copy the <code>Mixology</code> folder from the download's <code>Cubase MIDI Remote</code> folder into ${isMac ? "<code>~/Documents/Steinberg/Cubase/MIDI Remote/Driver Scripts/Local/</code>" : "<code>Documents\\Steinberg\\Cubase\\MIDI Remote\\Driver Scripts\\Local\\</code>"}.</li>
      <li>Start Cubase. In <b>Studio &gt; Studio Setup &gt; MIDI Port Setup</b>, untick <b>In 'All MIDI Inputs'</b> for both Mixology ports (so bridge messages never reach your instruments).</li>
      <li>Cubase should find "Mixology Bridge" on its own (MIDI Remote tab). If not: MIDI Remote tab &gt; <b>+</b> (Add MIDI Controller Surface) &gt; vendor Mixology, model Bridge, input <i>Mixology to Cubase</i>, output <i>Cubase to Mixology</i>.</li>
      <li>Open Mixology (on the Stereo Out). This panel turns green when it hears the script.</li>`);
  return `<p style="margin:10px 0 4px">Mixology moves faders in Cubase, FL Studio and Ableton Live for now (Logic Pro and Cakewalk are next). In ${dawName(daw)} it lists every fader move for you (Auto Mix, Balance) with the exact dB; everything else works as usual.</p>`;
}

function bridgePanel(app, root) {
  root.style.fontSize = "19px";
  const status = el("div", "card", root, { position: "relative", padding: "16px 20px", marginBottom: "14px", lineHeight: "1.5" });
  const setup = el("details", "card", root, { position: "relative", padding: "12px 20px", marginBottom: "14px", lineHeight: "1.5" });
  setup.innerHTML = `<summary style="cursor:pointer;font-weight:600;color:var(--gold)">One-time setup (about 5 minutes)</summary>` + setupSteps(app.daw);

  const changesCard = el("div", "card", root, { position: "relative", padding: "16px 20px", marginBottom: "14px", lineHeight: "1.45" });
  el("div", "", changesCard, { fontWeight: "600", marginBottom: "6px", color: "var(--gold)" }).textContent = "Mixology's changes";
  const changesText = el("div", "", changesCard, { marginBottom: "10px" });
  const ctl = el("div", "", changesCard, { display: "flex", flexWrap: "wrap", gap: "8px", alignItems: "center" });
  const restoreBtn = el("button", "small-btn red", ctl); restoreBtn.textContent = "↺ Restore all";
  restoreBtn.title = "Every channel Mixology changed (faders, pans, mute / solo, Assist EQ) goes back the way it was before Mixology's first move";
  restoreBtn.addEventListener("click", () => app.command("restoreAll"));
  const forgetBtn = el("button", "small-btn", ctl); forgetBtn.textContent = "Keep the mix as it is now";
  forgetBtn.title = "Forget the restore points: the current mix becomes the new starting point";
  forgetBtn.addEventListener("click", () => app.command("forget"));
  const result = el("span", "", ctl, { marginLeft: "10px", color: "var(--cyan)" });

  const linkCard = el("div", "card", root, { position: "relative", padding: "14px 20px", marginBottom: "14px", lineHeight: "1.45" });
  el("div", "", linkCard, { fontWeight: "600", marginBottom: "6px", color: "var(--gold)" }).textContent = "Mixology Link";
  const linkInfo = el("div", "", linkCard);

  const bankCard = el("div", "card", root, { position: "relative", padding: "14px 20px", marginBottom: "14px", display: "flex", gap: "8px", alignItems: "center", flexWrap: "wrap" });
  el("span", "", bankCard, { marginRight: "8px", fontWeight: "600", color: "var(--gold)" }).textContent = "Bank";
  for (const [label, a] of [["Reset", 0], ["◀◀ Previous", 1], ["◀ Shift", 3], ["Shift ▶", 4], ["Next ▶▶", 2]]) {
    const b = el("button", "small-btn", bankCard); b.textContent = label; b.addEventListener("click", () => app.command("bank", a));
  }
  const refresh = el("button", "small-btn", bankCard); refresh.textContent = "Refresh"; refresh.addEventListener("click", () => app.command("refresh"));
  const reconnect = el("button", "small-btn", bankCard); reconnect.textContent = "Reconnect"; reconnect.addEventListener("click", () => app.command("reconnect"));
  const lawInfo = el("span", "", bankCard, { marginLeft: "auto", color: "var(--text-dim)", fontSize: "16px" });

  const logCard = el("div", "card", root, { position: "relative", padding: "14px 20px" });
  const logHead = el("div", "", logCard, { display: "flex", alignItems: "center", gap: "10px", marginBottom: "8px" });
  el("span", "", logHead, { fontWeight: "600", color: "var(--gold)" }).textContent = "Log";
  const copy = el("button", "small-btn", logHead, { marginLeft: "auto" }); copy.textContent = "Copy log";
  copy.addEventListener("click", async () => { await app.command("copyLog"); app.toast("Log copied: paste it into the chat with Claude."); });
  const log = el("div", "scroll", logCard, { height: "230px", overflow: "auto", fontSize: "15px", lineHeight: "1.45", whiteSpace: "pre-wrap", userSelect: "text", fontFamily: "Consolas, 'IBM Plex Mono', monospace", color: "var(--text-dim)" });

  bridgeRender = (b) => {
    if (!b) return;
    const ok = b.link === "connected";
    const dot = ok ? "ok" : b.link === "waiting" ? "warn" : "bad";
    const dn = hasBridge(app.daw) ? dawName(app.daw) : "your DAW";
    const linkText = { connected: `Connected to ${dn}`, waiting: `Ports open, waiting for the Mixology Bridge script in ${dn}`, lost: `Lost contact with ${dn}: retrying`, offline: "Not connected" }[b.link] || b.link;
    status.innerHTML = `<div><span class="status-dot ${dot}"></span><b>${linkText}</b>${ok ? ` <span class="dim">(script v${b.script})</span>` : ""}</div>
      <div class="dim" style="font-size:17px">Ports: <code>${b.toCubase}</code> (Mixology → ${dn}), <code>${b.fromCubase}</code> (${dn} → Mixology)</div>
      ${b.portProblem ? `<div style="color:var(--amber);font-size:17px">${b.portProblem}</div>` : ""}`;
    if (!ok) setup.open = true;

    const changed = b.changedCount || 0;
    changesText.innerHTML = !b.restorePoints ? "Nothing changed yet. Before Mixology first moves a channel it remembers how it was, so you can always go back."
      : `${changed ? `<b>${changed}</b> channel${changed === 1 ? "" : "s"} changed by Mixology (faders, pans${b.changedEq ? `, Assist EQ on ${b.changedEq}` : ""}).` : "Everything Mixology changed is back how it was."} Restore puts them back the way they were before Mixology's first move, wherever they are in the project. What you changed yourself since stays as you set it.`;
    restoreBtn.disabled = !ok || !changed;
    forgetBtn.disabled = !b.restorePoints;
    result.textContent = b.lastMove || "";

    const seen = b.linksSeen || 0, paired = b.linksPaired || 0;
    linkInfo.innerHTML = b.linksShared === false
      ? `<span style="color:var(--red)">Windows refused the shared memory Mixology Link uses.</span> Restart Cubase; if it keeps happening, Copy log and send it.`
      : seen === 0 ? "No Mixology Link found yet. Insert <b>Mixology Link</b> on a track (after its other plug-ins): its level shows on that strip of the Tracks page."
      : `${seen} Mixology Link${seen === 1 ? "" : "s"} running, <b>${paired}</b> paired with the channels in view. Links pair by track name (and colour when two tracks share a name).`;
    lawInfo.textContent = `Fader curve: ${b.lawPoints} points learned`;
    if (b.log) {
      const atBottom = log.scrollTop + log.clientHeight >= log.scrollHeight - 8;
      log.textContent = b.log.join("\n");
      if (atBottom) log.scrollTop = log.scrollHeight;
    }
  };
  bridgeRender(app.bridge);
  app.native.getBridgeState().then((s) => { if (s && bridgeRender) { app.bridge = s; bridgeRender(s); } });
}

export function hookBridge(app) {
  app.on("bridge", (b) => {
    if (!$("overlay").classList.contains("shown")) return;
    if (bridgeRender) bridgeRender(b);
    if (historyRender) historyRender(b);
  });
}

// ---------------------------------------------------------------------------------------------
// History: every change Mixology makes, step by step. Go back to right after any step, or forward again until
// something new is changed.

export function openHistory(app) {
  const body = sheet("HISTORY");
  body.className = "scroll";
  const opened = new Set();   // steps showing their details
  let seen = -1, busy = false;
  const btn = (parent, text, onClick, extra = {}) => {
    const b = el("button", "small-btn", parent, extra);
    b.textContent = text;
    b.addEventListener("click", (e) => { e.stopPropagation(); onClick(); });
    return b;
  };
  const time = (ms) => new Date(ms).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  // A step from another day (the history is saved with the project): its date over its time.
  const when = (ms) => new Date(ms).toDateString() === new Date().toDateString() ? time(ms)
    : new Date(ms).toLocaleDateString([], { month: "short", day: "numeric" }) + "\n" + new Date(ms).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  async function go(name, id) {
    if (busy) return;
    busy = true;
    await app.command(name, { value: id || 0 });
    busy = false;
    render();
  }

  async function render() {
    const r = await app.native.bridgeCommand("history", 0);
    const h = r && r.history;
    if (!h) { body.textContent = "The history needs the plug-in."; return; }
    seen = h.version;
    body.innerHTML = "";
    const steps = h.steps || [];
    const undoable = steps.filter((s) => !s.undone), redoable = steps.filter((s) => s.undone);

    const top = el("div", "", body, { display: "flex", alignItems: "center", gap: "12px", flexWrap: "wrap", marginBottom: "10px" });
    const undo = btn(top, undoable.length ? `UNDO: ${undoable[undoable.length - 1].what}` : "UNDO", () => go("historyUndo"));
    undo.disabled = !undoable.length || h.busy;
    const redo = btn(top, redoable.length ? `REDO: ${redoable[0].what}` : "REDO", () => go("historyRedo"));
    redo.disabled = !redoable.length || h.busy;
    for (const b of [undo, redo]) Object.assign(b.style, { maxWidth: "520px", overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" });
    if (h.busy) el("span", "", top, { fontSize: "17px", color: "var(--amber)" }).textContent = "Moving faders in Cubase...";
    el("p", "", body, { fontSize: "17px", color: "var(--text-dim)", margin: "0 0 12px", lineHeight: "1.4" }).textContent =
      "Every change Mixology makes, newest first: faders, pans, mute and solo, Trim, every knob, presets, the smart tools and Restore. " +
      "GO BACK HERE puts the mix back to how it was right after that step. Undone steps stay (dimmed) until you change something new. " +
      `Moves you make in ${dawName(app.daw)} itself aren't here (its own undo history has those).`;

    if (!steps.length) {
      el("p", "", body, { fontSize: "19px", margin: "18px 0" }).textContent = "Nothing yet. Every change Mixology makes from now on shows up here.";
      return;
    }

    let earlier = false;
    for (const s of steps.slice().reverse()) {
      if (!earlier && h.fromProject && s.id <= h.fromProject) {
        // The steps that came back with the project: made before it was last opened.
        earlier = true;
        el("div", "", body, { fontSize: "16px", fontWeight: "600", color: "var(--text-dim)", letterSpacing: "0.06em", margin: "14px 0 8px" }).textContent =
          "EARLIER SESSIONS (SAVED WITH THE PROJECT)";
      }
      const isNow = s.id === h.current;
      const c = el("div", "card", body, { position: "relative", padding: "10px 16px", marginBottom: "8px", cursor: "pointer",
        opacity: s.undone ? "0.5" : "1", borderColor: isNow ? "var(--gold)" : "" });
      const line = el("div", "", c, { display: "flex", alignItems: "center", gap: "14px" });
      el("div", "", line, { fontSize: "16px", color: "var(--text-dim)", width: "92px", flex: "none", whiteSpace: "pre-line", lineHeight: "1.25" }).textContent = when(s.time);
      const text = el("div", "", line, { flex: "1", minWidth: "0" });
      el("div", "", text, { fontSize: "19px", fontWeight: "600" }).textContent = s.what + (s.undone ? "  (undone)" : "");
      el("div", "", text, { fontSize: "16px", color: "var(--text-dim)", marginTop: "2px", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" }).textContent = s.summary;
      if (isNow) el("span", "", line, { fontSize: "16px", fontWeight: "600", color: "var(--gold)", letterSpacing: "0.06em" }).textContent = "NOW";
      else if (s.pending) el("span", "", line, { fontSize: "16px", color: "var(--amber)" }).textContent = "moving...";
      else {
        const b = btn(line, s.undone ? "REDO TO HERE" : "GO BACK HERE", () => go("historyTo", s.id));
        b.disabled = h.busy;
      }
      if (opened.has(s.id) && s.lines && s.lines.length) {
        const list = el("div", "", c, { margin: "8px 0 2px 106px", fontSize: "16px", lineHeight: "1.45", color: "var(--text)" });
        for (const l of s.lines) el("div", "", list).textContent = l;
      }
      c.addEventListener("click", () => { if (opened.has(s.id)) opened.delete(s.id); else opened.add(s.id); render(); });
    }
    const start = el("div", "card", body, { position: "relative", display: "flex", alignItems: "center", gap: "14px", padding: "10px 16px", marginBottom: "8px",
      borderColor: h.current === 0 ? "var(--gold)" : "" });
    el("div", "", start, { width: "92px", flex: "none" });
    const st = el("div", "", start, { flex: "1" });
    el("div", "", st, { fontSize: "19px", fontWeight: "600" }).textContent = "The start";
    el("div", "", st, { fontSize: "16px", color: "var(--text-dim)", marginTop: "2px" }).textContent = "How the mix was before the first step above.";
    if (h.current === 0) el("span", "", start, { fontSize: "16px", fontWeight: "600", color: "var(--gold)", letterSpacing: "0.06em" }).textContent = "NOW";
    else btn(start, "UNDO EVERYTHING", () => go("historyTo", 0)).disabled = h.busy;
    el("p", "", body, { fontSize: "16px", color: "var(--text-dim)", margin: "12px 0 0", lineHeight: "1.4" }).textContent =
      `Click a step to see each change. A fader you moved in ${dawName(app.daw)} since Mixology moved it stays where you put it. The history is saved with the project (the last 200 steps), so it's still here when you open the song again.`;
  }
  historyRender = (b) => { if (b && b.history && b.history.version !== seen && !busy) render(); };
  render();
}

// ---------------------------------------------------------------------------------------------
// Manual

function markdown(md) {
  const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  const inline = (s) => esc(s).replace(/\*\*(.+?)\*\*/g, "<b>$1</b>").replace(/`(.+?)`/g, "<code>$1</code>").replace(/\*(.+?)\*/g, "<i>$1</i>");
  const out = [];
  let list = null, table = null, para = [];
  const flush = () => {
    if (para.length) { out.push(`<p>${inline(para.join(" "))}</p>`); para = []; }
    if (list) { out.push(`<${list.tag}>${list.items.map((i) => `<li>${inline(i)}</li>`).join("")}</${list.tag}>`); list = null; }
    if (table) { out.push(`<table>${table.map((r, i) => `<tr>${r.map((c) => (i === 0 ? `<th>${inline(c)}</th>` : `<td>${inline(c)}</td>`)).join("")}</tr>`).join("")}</table>`); table = null; }
  };
  for (const raw of md.split("\n")) {
    const line = raw.trimEnd();
    let m;
    if (!line.trim()) { flush(); continue; }
    if ((m = line.match(/^(#{1,3})\s+(.*)/))) { flush(); out.push(`<h${m[1].length}>${inline(m[2])}</h${m[1].length}>`); continue; }
    if (/^\|.*\|$/.test(line)) {
      if (/^\|[\s:|-]+\|$/.test(line)) continue;
      if (!table) { flush(); table = []; }
      table.push(line.slice(1, -1).split("|").map((c) => c.trim()));
      continue;
    }
    if ((m = line.match(/^\s*([-*]|\d+\.)\s+(.*)/))) {
      const tag = /\d/.test(m[1]) ? "ol" : "ul";
      if (!list || list.tag !== tag) { flush(); list = { tag, items: [] }; }
      list.items.push(m[2]);
      continue;
    }
    if (list && /^\s{2,}/.test(raw)) { list.items[list.items.length - 1] += " " + line.trim(); continue; }
    if (list || table) flush();
    para.push(line.trim());
  }
  flush();
  return out.join("\n");
}

export async function openHelp(app) {
  const body = sheet("MANUAL");
  body.className = "scroll manual";
  let text = "";
  const preview = !location.host.includes("juce");   // inside the plug-in the page is served from juce.backend
  for (const url of preview ? ["../../docs/MANUAL.md", "MANUAL.md"] : ["MANUAL.md"]) {
    try { const r = await fetch(url); if (r.ok) { text = await r.text(); break; } } catch (e) { /* next */ }
  }
  body.innerHTML = text ? markdown(text) : "<p>The manual couldn't be loaded.</p>";
}

// ---------------------------------------------------------------------------------------------
// Mix styles: pick a genre, then its style in a pop-up (so the list never gets cluttered).

// ---------------------------------------------------------------------------------------------
// Presets: a track's (or the Full Mix's) processing under a name. Mixology's own starting points by kind of track,
// and yours (saved from any track, for every song). Loading sets every control; UNDO puts back what it had.

const ROLE_WORD = { kick: "kick", snare: "snare", hats: "hi-hat", perc: "percussion", bass: "bass / 808", keys: "keys", synth: "synth", guitar: "guitar",
  vox: "vocal", adlibs: "ad-lib", fx: "FX", other: "" };
const KIND_TITLE = { track: "Tracks", fx: "FX channels (100 % wet returns)", mix: "Full Mix (Mixology on the master channel)" };

export async function openPresets(app) {
  const body = sheet("PRESETS");
  body.className = "scroll";
  let key = (app.proc && app.proc.key) || "mix";
  let draftName = "";
  const btn = (parent, text, onClick, extra = {}) => {
    const b = el("button", "small-btn", parent, extra);
    b.textContent = text;
    b.addEventListener("click", onClick);
    return b;
  };
  const say = (r) => { if (r && r.message) app.toast(r.message, r.ok ? undefined : "warn"); };

  async function render() {
    const r = await app.native.presetList(key);
    if (!r || !r.target) { body.textContent = "Presets need the plug-in."; return; }
    const t = r.target;
    key = t.key;
    body.innerHTML = "";

    // For: which track (or the Full Mix); the mix style lives here too.
    const top = el("div", "", body, { display: "flex", alignItems: "center", gap: "12px", flexWrap: "wrap", marginBottom: "12px" });
    el("span", "", top, { fontSize: "19px", color: "var(--text-dim)" }).textContent = "For:";
    const pick = el("select", "", top, { height: "42px", minWidth: "260px", borderRadius: "9px", border: "1.5px solid var(--select-edge)", background: "var(--well)",
      color: "var(--text)", font: "500 19px var(--font)", padding: "0 10px" });
    for (const x of r.targets || []) {
      const o = el("option", "", pick);
      o.value = x.key;
      o.textContent = x.name;
      o.selected = x.key === key;
    }
    pick.addEventListener("change", () => {
      key = pick.value;
      if (app.proc) {   // the processing pages follow
        if (!app.proc.targets.some((x) => x.key === key)) app.proc.targets = r.targets;
        app.proc.select(key);
      }
      render();
    });
    el("span", "", top, { fontSize: "17px", color: "var(--text-dim)" }).textContent = t.kind === "mix" ? "" : t.kind === "fx" ? "an FX channel (its Space is 100 % wet)"
      : (ROLE_WORD[t.role] ? `a ${ROLE_WORD[t.role]} track` : "") + (t.voxology ? (ROLE_WORD[t.role] ? ", " : "") + "Voxology compresses it already" : "");
    el("span", "", top, { flex: "1" });
    btn(top, `Mix style: ${app.style.fullName}`, () => openStyles(app));

    if (!t.ok) {
      el("p", "", body, { fontSize: "19px", color: "var(--amber)" }).textContent = "This track's Mixology Link isn't answering. Pick it again, or another track.";
      return;
    }
    if (r.undo) {
      const u = el("div", "card", body, { position: "relative", display: "flex", alignItems: "center", gap: "14px", padding: "10px 16px", marginBottom: "12px", borderColor: "var(--gold)" });
      el("span", "", u, { flex: "1", fontSize: "19px" }).textContent = `'${r.undo}' was loaded on '${t.name}'.`;
      btn(u, "UNDO", async () => { say(await app.native.presetUndo(key)); render(); });
    }

    const row = (parent, p) => {
      const c = el("div", "card", parent, { position: "relative", display: "flex", alignItems: "center", gap: "14px", padding: "12px 16px", marginBottom: "8px" });
      const text = el("div", "", c, { flex: "1", minWidth: "0" });
      el("div", "", text, { fontSize: "20px", fontWeight: "600" }).textContent = p.name;
      el("div", "", text, { fontSize: "16px", color: "var(--text-dim)", marginTop: "3px", lineHeight: "1.3" }).textContent =
        p.about || (p.factory ? "" : p.kind === "mix" ? "Yours, from the Full Mix." : p.kind === "fx" ? "Yours, from an FX channel." : `Yours, from ${ROLE_WORD[p.roles] ? "a " + ROLE_WORD[p.roles] : "a"} track.`);
      btn(c, "LOAD", async () => { say(await app.native.presetApply(key, p.name, p.factory)); render(); });
      if (!p.factory) btn(c, "DELETE", async (e) => {
        const b = e.currentTarget;
        if (b.dataset.sure !== "1") { b.dataset.sure = "1"; b.textContent = "SURE?"; return; }
        await app.native.presetDelete(p.name);
        app.toast(`Deleted '${p.name}'.`);
        render();
      });
    };
    const head = (text) => { el("div", "", body, { fontSize: "18px", fontWeight: "600", color: "var(--gold)", letterSpacing: "0.06em", margin: "16px 0 8px" }).textContent = text; };

    const forThis = r.factory.filter((p) => p.suits).concat(r.mine.filter((p) => p.suits));
    if (forThis.length) {
      head(`FOR '${t.name.toUpperCase()}'`);
      forThis.forEach((p) => row(body, p));
    }

    head("MY PRESETS");
    const save = el("div", "", body, { display: "flex", gap: "10px", alignItems: "center", marginBottom: "10px", flexWrap: "wrap" });
    const input = el("input", "num", save, { width: "360px", textAlign: "left" });
    input.placeholder = t.kind === "mix" ? "A name, e.g. My Mix Bus" : `A name, e.g. My ${t.name}`;
    input.maxLength = 60;
    input.value = draftName;
    const saveBtn = btn(save, "", async () => {
      const name = input.value.trim();
      if (!name) { input.focus(); app.toast("Give it a name first.", "warn"); return; }
      if (r.mine.some((p) => p.name === name) && saveBtn.dataset.sure !== "1") { saveBtn.dataset.sure = "1"; saveBtn.textContent = `REPLACE '${name}'?`; return; }
      const res = await app.native.presetSave(key, name);
      say(res);
      if (res && res.ok) { draftName = ""; render(); }
    });
    const label = () => { saveBtn.dataset.sure = ""; saveBtn.textContent = `SAVE '${t.name.toUpperCase()}' AS A PRESET`; };
    label();
    input.addEventListener("input", () => { draftName = input.value; label(); });
    input.addEventListener("keydown", (e) => { e.stopPropagation(); if (e.key === "Enter") saveBtn.click(); });
    if (!r.mine.length) el("p", "", body, { fontSize: "17px", color: "var(--text-dim)", margin: "0 0 6px" }).textContent =
      "None yet. Dial in a sound you like (EQ, Dynamics, Tone, Stereo, Space), then save it here: it's there on every song.";
    r.mine.filter((p) => !p.suits).forEach((p) => row(body, p));

    for (const kind of ["track", "fx", "mix"]) {
      const rest = r.factory.filter((p) => !p.suits && p.kind === kind);
      if (!rest.length) continue;
      head(`MIXOLOGY PRESETS: ${KIND_TITLE[kind].toUpperCase()}`);
      rest.forEach((p) => row(body, p));
    }
    el("p", "", body, { fontSize: "16px", color: "var(--text-dim)", margin: "14px 0 0", lineHeight: "1.4" }).textContent =
      "A preset sets the processing only (EQ, Dynamics, Tone, Stereo, Space): never the fader, the Trim or the Assist EQ. A track keeps its own routing: an FX channel stays 100 % wet.";
  }
  render();
}

export function openStyles(app) {
  const body = sheet("MIX STYLES");
  body.className = "scroll";
  const note = el("p", "", body, { fontSize: "18px", color: "var(--text-dim)", margin: "0 0 6px 0", lineHeight: "1.4" });
  note.textContent = "Pick your genre, then the style. It tells Mixology what you're going for: the Tone page's target, the Mix Assistant's advice, and the levels Auto Mix and Balance aim for. Voxology and Masterology share this list: pick it in any of them and all three follow.";
  const now = el("p", "", body, { fontSize: "19px", margin: "0 0 16px 0" });
  now.innerHTML = `Now: <b style="color:var(--gold)">${app.style.fullName}</b>`;

  const grid = el("div", "genre-grid", body);
  for (const genre of GENRES) {
    const colour = genreColour(genre);
    const b = el("button", "btn", grid);
    b.innerHTML = `${icon(genre.icon)}<span class="g-name">${genre.name}</span><span class="g-sub">${genre.styles.length} styles</span>`;
    b.querySelector("svg").style.color = colour;
    b.classList.toggle("active", genre === app.style.genre);
    if (genre === app.style.genre) b.querySelector("svg").style.color = "#fff";
    b.addEventListener("click", () => openGenre(app, genre));
  }
}

// Pale genre colours (Worship, Cinematic...) get darker on the light look so they still read on white.
function genreColour(genre) {
  return document.documentElement.dataset.theme === "light" ? `color-mix(in srgb, ${genre.colour} 72%, #000)` : genre.colour;
}

function openGenre(app, genre) {
  const old = $("style-pop");
  if (old) old.remove();
  const cover = el("div", "", $("sheet"));
  cover.id = "style-pop";
  cover.addEventListener("pointerdown", (e) => { if (e.target === cover) cover.remove(); });
  const pop = el("div", "pop", cover);
  const head = el("div", "pop-head", pop);
  head.innerHTML = `<span style="color:${genreColour(genre)};display:grid">${icon(genre.icon)}</span><span>${genre.name}</span>`;
  const back = el("button", "small-btn", head, { marginLeft: "auto" });
  back.textContent = "‹ All genres";
  back.addEventListener("click", () => cover.remove());
  const list = el("div", "styles scroll", pop);
  for (const style of genre.styles) {
    const key = `${genre.id}.${style.id}`;
    const b = el("button", "btn", list);
    b.innerHTML = `<span class="s-name">${style.name}</span><span class="s-about">${style.about}</span>`;
    b.classList.toggle("active", key === app.style.key);
    b.addEventListener("click", () => { app.setStyle(key); cover.remove(); $("overlay").classList.remove("shown"); });
  }
}

// ---------------------------------------------------------------------------------------------
// Mix Assistant: one suggestion in full, and the list of tracks Analyze Mix heard.

const KIND = { warn: ["#ff5a3c", "Worth fixing"], info: ["#19d3ff", "Have a look"], calm: ["#39e26b", "Fine"] };

export function openSuggestion(app, sg, opts = {}) {
  const body = sheet("MIX ASSISTANT");
  body.className = "scroll";
  const [colour, kindLabel] = sg.done ? [KIND.calm[0], "Done: play the song to check"] : KIND[sg.kind] || KIND.info;
  const head = el("div", "", body, { display: "flex", alignItems: "center", gap: "16px", marginBottom: "12px" });
  head.innerHTML = `<span style="width:54px;height:54px;flex:none;color:${colour};filter:drop-shadow(0 0 6px ${colour})">${icon(sg.icon)}</span>
    <span><span style="display:block;font-size:30px;font-weight:600">${sg.title}</span>
    <span style="font-size:16px;letter-spacing:0.12em;text-transform:uppercase;color:${colour}">${kindLabel}</span></span>`;
  el("p", "", body, { fontSize: "21px", lineHeight: "1.5", margin: "0 0 16px" }).textContent = sg.text;
  const toolName = TOOL_NAMES[sg.tool];
  if (sg.done) {
    // A smart tool (or you) already changed it since Mixology listened: the point is about the mix before that.
    const done = el("div", "card", body, { position: "relative", padding: "14px 18px 14px 58px", marginBottom: "16px", fontSize: "19px", lineHeight: "1.45", borderColor: KIND.calm[0] });
    done.innerHTML = `<div style="position:absolute;left:16px;top:14px;width:28px;height:28px;color:${KIND.calm[0]}">${icon("check")}</div>
      <b style="color:${KIND.calm[0]}">Done since Mixology listened.</b> Changed: ${sg.doneWhat}. This point is about the mix as it was before: Mixology checks it again when it hears the song (Auto Analyze while it plays, or Analyze Mix), and it goes when it's fixed.`;
  }
  const need = el("div", "card", body, { position: "relative", padding: "14px 18px", marginBottom: "16px", fontSize: "20px", lineHeight: "1.45", borderColor: colour });
  need.innerHTML = `<b style="color:var(--gold)">Do I need to fix it?</b> ${sg.need}`;
  if (sg.steps && sg.steps.length) {
    el("div", "", body, { fontSize: "18px", fontWeight: "600", color: "var(--gold)", margin: "4px 0 6px", letterSpacing: "0.06em" }).textContent = "HOW";
    const ol = el("ol", "", body, { fontSize: "20px", lineHeight: "1.5", margin: "0 0 16px", paddingLeft: "28px" });
    for (const st of sg.steps) el("li", "", ol, { marginBottom: "6px" }).textContent = st;
  }
  if (toolName) {
    // The smart tool that makes this fix: its changes, to look at and apply.
    const row = el("div", "", body, { display: "flex", alignItems: "center", gap: "12px", margin: "0 0 16px" });
    const go = el("button", "small-btn", row, { height: "46px", fontSize: "19px", fontWeight: "600", padding: "0 20px", borderColor: "var(--gold)", color: "var(--gold)" });
    go.textContent = `OPEN ${toolName.toUpperCase()} ›`;
    go.addEventListener("click", () => openToolSheet(app, sg.tool));
    el("span", "", row, { fontSize: "16px", color: "var(--text-dim)" }).textContent = sg.done
      ? `${toolName} has nothing more to do here.` : `${toolName} works out these changes for you: see each one, then Apply.`;
  }
  const isVocal = sg.id && sg.id.startsWith("vocal-") && sg.id !== "vocal-pocket";
  // Your ears, for the whole vocal or one vocal track (a verse and a hook on their own tracks can want different levels).
  const report = app.bridge && app.bridge.analysis && app.bridge.analysis.report;
  const vocalTracks = isVocal && report ? (sg.tracks || []).map((n) => report.tracks.find((t) => t.name === n)).filter((t) => t && t.vocalRatio !== undefined) : [];
  if (isVocal) {
    let picked = vocalTracks.find((t) => t.hash === opts.track) || null;
    const box = el("div", "", body, { margin: "0 0 16px" });
    const send = async (value) => {
      const r = await app.command("vocalTaste", picked ? { value, hash: picked.hash } : { value });
      if (!r || !r.ok) return;
      if (vocalTracks.length < 2) { $("overlay").classList.remove("shown"); return; }
      // Stay here (set the next track too): the card again, as the advice now reads.
      const hash = picked && picked.hash;
      setTimeout(() => {
        const rep = app.bridge && app.bridge.analysis && app.bridge.analysis.report;
        const fresh = rep && rep.suggestions.find((x) => x.id && x.id.startsWith("vocal-") && x.id !== "vocal-pocket");
        if (fresh) openSuggestion(app, fresh, { track: hash });
        else $("overlay").classList.remove("shown");
      }, 250);
    };
    const render = () => {
      box.innerHTML = "";
      if (vocalTracks.length >= 2) {
        const row = el("div", "", box, { display: "flex", flexWrap: "wrap", gap: "8px", alignItems: "center", marginBottom: "10px" });
        el("span", "", row, { fontSize: "18px", color: "var(--text-dim)", marginRight: "4px" }).textContent = "Set it for:";
        const chip = (label, on, onClick) => {
          const c = el("button", "chip", row);
          c.textContent = label;
          c.classList.toggle("on", on);
          c.addEventListener("click", onClick);
        };
        chip("All vocals", !picked, () => { picked = null; render(); });
        for (const t of vocalTracks) chip(t.name, picked === t, () => { picked = picked === t ? null : t; render(); });
        if (picked) {
          el("div", "", box, { fontSize: "18px", lineHeight: "1.45", margin: "0 0 10px" }).textContent =
            `'${picked.name}' sits ${fmt.db(picked.vocalRatio)} against the rest of the mix while it sings. ` +
            (picked.vocalOwn ? `You like it about ${fmt.db(picked.vocalTarget)} (this track's setting).` : `It follows the whole vocal's aim (${fmt.db(picked.vocalTarget)}).`);
        }
      }
      const taste = el("div", "", box, { display: "flex", flexWrap: "wrap", gap: "10px", alignItems: "center" });
      el("span", "", taste, { fontSize: "19px", color: "var(--gold)" }).textContent = picked ? `To your ears, '${picked.name}' sounds:` : "To your ears, the vocal sounds:";
      for (const [value, label] of [["louder", "Too quiet"], ["right", "Sounds right"], ["quieter", "Too loud"]]) {
        const b = el("button", "small-btn", taste, { height: "44px", fontSize: "19px" });
        b.textContent = label;
        b.addEventListener("click", () => send(value));
      }
      if (picked && picked.vocalOwn) {
        const back = el("button", "small-btn", taste, { height: "44px", fontSize: "17px" });
        back.textContent = "Follow the whole vocal";
        back.addEventListener("click", () => send("style"));
      }
      el("div", "", taste, { width: "100%", fontSize: "16px", color: "var(--text-dim)" }).textContent = picked
        ? "Mixology remembers it for this track and this song: the advice, Balance and Auto Mix follow your ears."
        : "Mixology remembers it for this song: the advice, Balance and Auto Mix follow your ears." + (vocalTracks.length >= 2 ? " Pick a track above to set it for that track only." : "");
    };
    render();
  }
  if (sg.id === "group-hint") {
    const row = el("div", "", body, { display: "flex", flexWrap: "wrap", gap: "10px", alignItems: "center", margin: "0 0 16px" });
    for (const n of sg.tracks || []) {
      const b = el("button", "small-btn", row, { height: "44px", fontSize: "19px" });
      b.textContent = `It's the music group: '${n}'`;
      b.addEventListener("click", async () => { const r = await app.command("setMusicBus", { name: n, value: true }); if (r && r.ok) $("overlay").classList.remove("shown"); });
    }
  }
  if (sg.tracks && sg.tracks.length) {
    const row = el("div", "", body, { display: "flex", flexWrap: "wrap", gap: "8px", alignItems: "center" });
    if (vocalTracks.length < 2) {   // (the vocal tracks are picked above)
      el("span", "", row, { fontSize: "18px", color: "var(--text-dim)", marginRight: "4px" }).textContent = "Tracks:";
      for (const t of sg.tracks) el("span", "chip", row).textContent = t;
    }
    const go = el("button", "small-btn", row, { marginLeft: "auto" });
    go.textContent = "Show the Tracks page";
    go.addEventListener("click", () => { $("overlay").classList.remove("shown"); app.showPage("tracks"); });
  }
}

export function openTracksSheet(app) {
  const body = sheet("YOUR TRACKS");
  body.className = "scroll";
  const render = () => {
    body.innerHTML = "";
    const report = app.bridge && app.bridge.analysis && app.bridge.analysis.report;
    if (!report) { el("p", "", body, { fontSize: "20px" }).textContent = "Press Analyze Mix first: this list shows what it heard."; return; }
    const note = el("p", "", body, { fontSize: "18px", color: "var(--text-dim)", margin: "0 0 14px", lineHeight: "1.4" });
    note.textContent = `What Analyze Mix heard (${Math.round(report.seconds)} s). Correct a track's role if Mixology guessed wrong, or leave a track out (a reference song, a printed stem): the advice updates right away.`;
    const table = el("div", "", body, { display: "grid", gridTemplateColumns: "44px 1.6fr 1.2fr 0.8fr 0.8fr 0.7fr 0.9fr", gap: "8px 12px", alignItems: "center", fontSize: "18px" });
    for (const h of ["", "Track", "Role", "Level", "Peak", "Width", ""]) el("div", "", table, { color: "var(--gold)", fontSize: "15px", letterSpacing: "0.08em", textTransform: "uppercase" }).textContent = h;
    for (const t of report.tracks) {
      const R = ROLES[t.role] || ROLES.other;
      const ic = el("div", "", table, { width: "34px", height: "34px", color: R.colour, filter: t.excluded ? "grayscale(1)" : `drop-shadow(0 0 5px ${R.colour})`, opacity: t.excluded ? "0.4" : "1" });
      ic.innerHTML = icon(roleIcon(t.role, t.name));
      const name = el("div", "", table, { whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis", opacity: t.excluded ? "0.5" : "1" });
      name.textContent = t.name;
      name.title = t.name + (t.faderKnown ? "" : " (its fader wasn't read: counted at 0 dB)");
      const roleBtn = el("button", "small-btn", table, { textAlign: "left", padding: "0 12px" });
      roleBtn.textContent = R.label + (t.roleFrom === "you" ? "" : t.roleFrom === "voxology" ? "  (Voxology)" : t.roleFrom === "name" ? "  (name)" : "  (sound)");
      roleBtn.addEventListener("click", () => {
        const items = ROLE_ORDER.map((k) => ({ label: ROLES[k].label, value: k, icon: ROLES[k].icon, colour: ROLES[k].colour, sel: k === t.role }));
        items.push({ divider: true }, { label: "Automatic", value: "", icon: "wand" });
        popupMenu(roleBtn, items, async (v) => { await app.command("setRole", { hash: t.hash, value: v }); setTimeout(render, 250); }, 260);
      });
      el("div", "", table).textContent = t.loudness > -99 ? fmt.num(t.loudness, 1) + " LUFS" : "—";
      const pk = el("div", "", table, { color: t.peak > 0 ? "var(--red)" : "" });
      pk.textContent = t.peak > -99 ? fmt.signed(t.peak, 1) : "—";
      el("div", "", table).textContent = Math.round(t.width * 100) + " %";
      const inc = el("button", "small-btn", table);
      inc.textContent = t.excluded ? "Left out" : "Included";
      inc.style.color = t.excluded ? "var(--text-dim)" : "var(--green)";
      inc.addEventListener("click", async () => { await app.command("setExcluded", { hash: t.hash, value: !t.excluded }); setTimeout(render, 250); });
    }
    if (report.unheard && report.unheard.length) {
      const u = el("p", "", body, { fontSize: "17px", color: "var(--text-dim)", marginTop: "18px" });
      u.textContent = "Without Mixology Link (not heard one by one): " + report.unheard.join(", ") + ".";
    }
    if (report.routingFromSound) {
      const ro = el("p", "", body, { fontSize: "17px", color: "var(--text-dim)", marginTop: "10px", lineHeight: "1.4" });
      ro.textContent = `How your tracks reach the ${say(app.daw, "master")} channel (worked out from the sound: group faders, group processing, pan law): `
        + `instruments ${fmt.db(report.musicPathDb)}, vocals ${fmt.db(report.vocalPathDb)} on the way. The levels above include it.`;
    }
  };
  render();
}

