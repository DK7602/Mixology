// Overlays: Settings (Cubase Bridge test panel, display), the built-in manual, and mix styles.

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
  p.innerHTML = `<b>Mixology</b>: intelligent mixing engineer. Part of the suite: Voxology (vocals), Mixology (the mix), Polisher (mastering).<br>
    This build: the full interface, live mix meters, loudness, vectorscope, and the Cubase bridge test (Milestone 1).<br>
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

  const lockCard = el("div", "card", root, { position: "relative", padding: "16px 20px", marginBottom: "14px" });
  const lockHead = el("div", "", lockCard, { fontWeight: "600", marginBottom: "10px", color: "var(--gold)" });
  lockHead.textContent = "Test channel";
  const slotList = el("div", "", lockCard, { display: "grid", gridTemplateColumns: "repeat(4, 1fr)", gap: "8px", marginBottom: "12px" });
  const pauseLine = el("div", "", lockCard, { color: "var(--amber)", marginBottom: "8px" });

  const ctl = el("div", "", lockCard, { display: "flex", flexWrap: "wrap", gap: "8px", alignItems: "center" });
  const input = el("input", "num", ctl);
  input.type = "number"; input.step = "0.1"; input.value = "-6";
  const setBtn = el("button", "small-btn", ctl); setBtn.textContent = "Set dB";
  setBtn.addEventListener("click", () => app.command("setDb", parseFloat(input.value)));
  input.addEventListener("keydown", (e) => { if (e.key === "Enter") setBtn.click(); });
  const quick = [["0 dB", () => app.command("setDb", 0)], ["−6", () => app.command("setDb", -6)], ["−12", () => app.command("setDb", -12)], ["+6", () => app.command("setDb", 6)], ["−∞", () => app.command("minusInf")],
    ["−1 dB", () => app.command("nudge", -1)], ["+1 dB", () => app.command("nudge", 1)],
    ["Pan L", () => app.command("pan", 0)], ["Pan C", () => app.command("pan", 0.5)], ["Pan R", () => app.command("pan", 1)],
    ["Mute", () => app.command("mute", !(cur() && cur().mute))], ["Solo", () => app.command("solo", !(cur() && cur().solo))]];
  const quickBtns = quick.map(([label, f]) => { const b = el("button", "small-btn", ctl); b.textContent = label; b.addEventListener("click", f); return b; });
  const ctl2 = el("div", "", lockCard, { display: "flex", flexWrap: "wrap", gap: "8px", alignItems: "center", marginTop: "10px" });
  const restoreBtn = el("button", "small-btn red", ctl2); restoreBtn.textContent = "Restore original";
  restoreBtn.addEventListener("click", () => app.command("restore"));
  const unlockBtn = el("button", "small-btn", ctl2); unlockBtn.textContent = "Unlock";
  unlockBtn.addEventListener("click", () => app.command("unlock"));
  const result = el("span", "", ctl2, { marginLeft: "10px", color: "var(--cyan)" });

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

  const cur = () => app.bridge && app.bridge.slots && app.bridge.slots[app.bridge.lockedSlot];

  bridgeRender = (b) => {
    if (!b) return;
    const ok = b.link === "connected";
    const dot = ok ? "ok" : b.link === "waiting" ? "warn" : "bad";
    const linkText = { connected: "Connected to Cubase", waiting: "Ports open, waiting for the Mixology Bridge script in Cubase", lost: "Lost contact with Cubase: retrying", offline: "Not connected" }[b.link] || b.link;
    status.innerHTML = `<div><span class="status-dot ${dot}"></span><b>${linkText}</b>${ok ? ` <span class="dim">(script v${b.script})</span>` : ""}</div>
      <div class="dim" style="font-size:17px">Ports: <code>${b.toCubase}</code> (Mixology → Cubase), <code>${b.fromCubase}</code> (Cubase → Mixology)</div>
      ${b.portProblem ? `<div style="color:var(--amber);font-size:17px">${b.portProblem}</div>` : ""}`;
    if (!ok) setup.open = true;

    slotList.innerHTML = "";
    (b.slots || []).forEach((s, i) => {
      const item = el("button", "small-btn", slotList, { textAlign: "left", overflow: "hidden", textOverflow: "ellipsis" });
      const locked = b.lockedSlot === i && b.lock !== "none";
      item.innerHTML = `<span style="display:inline-block;width:18px;height:18px;vertical-align:-3px;margin-right:6px;color:${locked ? (b.lock === "locked" ? "var(--green)" : "var(--amber)") : "var(--text-faint)"}">${icon(locked ? "lock" : "unlock", 'stroke-width="2.2"')}</span>${i + 1}. ${s.present ? s.title : "—"}${s.present && s.hasVolume ? ` <span class="dim">${s.volumeText}</span>` : ""}${s.write ? ' <span style="color:var(--red)">W</span>' : ""}`;
      item.disabled = !ok || !s.present;
      item.style.borderColor = locked ? (b.lock === "locked" ? "var(--green)" : "var(--amber)") : "";
      item.addEventListener("click", () => app.command(locked ? "unlock" : "lock", i));
    });
    pauseLine.textContent = b.lock === "paused" ? "Paused: " + b.pauseReason : "";
    const canWrite = ok && b.lock === "locked";
    [setBtn, input, ...quickBtns, unlockBtn].forEach((x) => (x.disabled = !canWrite && x !== unlockBtn));
    unlockBtn.disabled = b.lock === "none";
    restoreBtn.disabled = !canWrite || !(b.snapshot && b.snapshot.valid);
    restoreBtn.title = b.snapshot && b.snapshot.valid ? `Back to ${b.snapshot.volumeText} dB on '${b.snapshot.title}'` : "Saved automatically before the first move";
    lockHead.innerHTML = b.lock === "none" ? "Test channel: <span class='dim' style='font-weight:400'>pick one below (only this channel can be moved)</span>"
      : `Test channel: <span style="color:var(--text)">${b.lockedTitle}</span>${cur() && cur().hasVolume ? ` <span class="dim" style="font-weight:400">now ${cur().volumeText} dB, pan ${cur().panText}</span>` : ""}`;
    result.textContent = b.moving ? "Moving..." : b.lastMove || "";
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
