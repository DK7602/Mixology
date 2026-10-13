// Overlays: Settings (Cubase Bridge, Mixology Link, display), the built-in manual, and mix styles.

import { $, el, icon, fmt } from "./ui.js";

// Mix styles: names + the tonal target the Tone page shows (bass weight, low-mid scoop, top sheen in dB).
// Milestone 4 adds level / pan / width targets for Auto Mix per style.
export const PRESETS = [
  { name: "Hip Hop - Modern Clean", bass: 4, scoop: 1.5, air: 2, about: "Full low end, clean mids, crisp top. Vocal on top of the beat." },
  { name: "Trap - Hard 808", bass: 6, scoop: 2, air: 2.5, about: "Big 808, scooped low mids, bright hats." },
  { name: "G-Funk - Laid Back", bass: 4.5, scoop: 0.5, air: 1, about: "Warm, round bass and synth leads, softer top." },
  { name: "Boom Bap - Dusty", bass: 3, scoop: 0, air: -1, about: "Punchy drums, warm mids, rolled-off top like vinyl." },
  { name: "Drill - Dark", bass: 5.5, scoop: 2.5, air: 0.5, about: "Sliding 808s, dark keys, tight hats." },
  { name: "R&B - Smooth", bass: 3.5, scoop: 1, air: 1.5, about: "Silky vocals, soft low end, lush space." },
  { name: "Pop Rap - Bright", bass: 3, scoop: 1.5, air: 3, about: "Forward vocal, bright and radio-ready balance." },
  { name: "West Coast - Bounce", bass: 4.5, scoop: 1, air: 1.5, about: "Bouncy kick and bass, clear claps, smooth top." },
];

function sheet(title) {
  $("sheet-title").textContent = title;
  const body = $("sheet-body");
  body.innerHTML = "";
  $("overlay").classList.add("shown");
  return body;
}

// ---------------------------------------------------------------------------------------------
// Settings

let bridgeRender = null;

export function openSettings(app, tab = "bridge") {
  const body = sheet("SETTINGS");
  const tabs = el("div", "", body, { display: "flex", gap: "10px", marginBottom: "18px" });
  const content = el("div", "", body);
  const names = [["bridge", "Cubase Bridge"], ["display", "Display"], ["about", "About"]];
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
    This build: the full interface, live mix meters, loudness, vectorscope, the Cubase bridge (every fader in view), and Mixology Link on each track (Milestone 2).<br>
    <span class="dim">Fonts: Barlow Semi Condensed (SIL Open Font License).</span>`;
}

function bridgePanel(app, root) {
  root.style.fontSize = "19px";
  const status = el("div", "card", root, { position: "relative", padding: "16px 20px", marginBottom: "14px", lineHeight: "1.5" });
  const setup = el("details", "card", root, { position: "relative", padding: "12px 20px", marginBottom: "14px", lineHeight: "1.5" });
  setup.innerHTML = `<summary style="cursor:pointer;font-weight:600;color:var(--gold)">One-time setup (about 5 minutes)</summary>
    <ol style="margin:10px 0 4px 0;padding-left:24px">
      <li>Install <b>loopMIDI</b> (free, by Tobias Erichsen). Add two ports named exactly <code>Mixology to Cubase</code> and <code>Cubase to Mixology</code>. Keep loopMIDI running (it can start with Windows).</li>
      <li>Copy the <code>Mixology</code> folder from the download's <code>Cubase MIDI Remote</code> folder into <code>Documents\\Steinberg\\Cubase\\MIDI Remote\\Driver Scripts\\Local\\</code>.</li>
      <li>Start Cubase. In <b>Studio &gt; Studio Setup &gt; MIDI Port Setup</b>, untick <b>In 'All MIDI Inputs'</b> for both Mixology ports (so bridge messages never reach your instruments).</li>
      <li>Cubase should find "Mixology Bridge" on its own (MIDI Remote tab). If not: MIDI Remote tab &gt; <b>+</b> (Add MIDI Controller Surface) &gt; vendor Mixology, model Bridge, input <i>Mixology to Cubase</i>, output <i>Cubase to Mixology</i>.</li>
      <li>Open Mixology (on the Stereo Out). This panel turns green when it hears the script.</li>
    </ol>`;

  const changesCard = el("div", "card", root, { position: "relative", padding: "16px 20px", marginBottom: "14px", lineHeight: "1.45" });
  el("div", "", changesCard, { fontWeight: "600", marginBottom: "6px", color: "var(--gold)" }).textContent = "Mixology's changes";
  const changesText = el("div", "", changesCard, { marginBottom: "10px" });
  const ctl = el("div", "", changesCard, { display: "flex", flexWrap: "wrap", gap: "8px", alignItems: "center" });
  const restoreBtn = el("button", "small-btn red", ctl); restoreBtn.textContent = "↺ Restore all";
  restoreBtn.title = "Every channel Mixology changed goes back the way it was before Mixology's first move";
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
    const linkText = { connected: "Connected to Cubase", waiting: "Ports open, waiting for the Mixology Bridge script in Cubase", lost: "Lost contact with Cubase: retrying", offline: "Not connected" }[b.link] || b.link;
    status.innerHTML = `<div><span class="status-dot ${dot}"></span><b>${linkText}</b>${ok ? ` <span class="dim">(script v${b.script})</span>` : ""}</div>
      <div class="dim" style="font-size:17px">Ports: <code>${b.toCubase}</code> (Mixology → Cubase), <code>${b.fromCubase}</code> (Cubase → Mixology)</div>
      ${b.portProblem ? `<div style="color:var(--amber);font-size:17px">${b.portProblem}</div>` : ""}`;
    if (!ok) setup.open = true;

    const changed = b.changedCount || 0;
    changesText.innerHTML = !b.restorePoints ? "Nothing changed yet. Before Mixology first moves a channel it remembers how it was, so you can always go back."
      : `${changed ? `<b>${changed}</b> channel${changed === 1 ? "" : "s"} changed by Mixology.` : "Everything Mixology changed is back how it was."} Restore puts them back the way they were before Mixology's first move (channels outside the 12 in view: use the bank arrows, then Restore again).`;
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
  app.on("bridge", (b) => { if (bridgeRender && $("overlay").classList.contains("shown")) bridgeRender(b); });
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
// Mix styles

export function openPresets(app) {
  const body = sheet("MIX STYLES");
  body.className = "scroll";
  const note = el("p", "", body, { fontSize: "18px", color: "var(--text-dim)", marginTop: "0" });
  note.textContent = "The style tells Mixology what you're going for. Today it sets the Tone page's target curve; from Milestone 4 it also steers Auto Mix and the Mix Assistant.";
  const grid = el("div", "", body, { display: "grid", gridTemplateColumns: "repeat(2, 1fr)", gap: "14px" });
  PRESETS.forEach((p, i) => {
    const c = el("button", "btn", grid, { position: "relative", height: "104px", flexDirection: "column", alignItems: "flex-start", justifyContent: "center", gap: "4px" });
    c.innerHTML = `<span style="font-size:24px;font-weight:600">${p.name}</span><span style="font-size:17px;opacity:0.85">${p.about}</span>`;
    c.classList.toggle("active", i === app.preset);
    c.addEventListener("click", () => { app.setPreset(i); $("overlay").classList.remove("shown"); });
  });
}
