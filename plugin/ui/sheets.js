// Overlays: Settings (Cubase Bridge, Mixology Link, display), the built-in manual, and mix styles.

import { $, el, icon, fmt, popupMenu } from "./ui.js";
import { GENRES } from "./mixstyles.js";
import { ROLES, ROLE_ORDER, roleIcon } from "./roles.js";

export function sheet(title) {
  const pop = $("style-pop");
  if (pop) pop.remove();
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
    This build: the full interface, live mix meters, loudness, vectorscope, the Cubase bridge (every fader in view), Mixology Link on each track, and Analyze Mix with the Mix Assistant (Milestone 3).<br>
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
    const linkText = { connected: "Connected to Cubase", waiting: "Ports open, waiting for the Mixology Bridge script in Cubase", lost: "Lost contact with Cubase: retrying", offline: "Not connected" }[b.link] || b.link;
    status.innerHTML = `<div><span class="status-dot ${dot}"></span><b>${linkText}</b>${ok ? ` <span class="dim">(script v${b.script})</span>` : ""}</div>
      <div class="dim" style="font-size:17px">Ports: <code>${b.toCubase}</code> (Mixology → Cubase), <code>${b.fromCubase}</code> (Cubase → Mixology)</div>
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
// Mix styles: pick a genre, then its style in a pop-up (so the list never gets cluttered).

export function openPresets(app) {
  const body = sheet("MIX STYLES");
  body.className = "scroll";
  const note = el("p", "", body, { fontSize: "18px", color: "var(--text-dim)", margin: "0 0 6px 0", lineHeight: "1.4" });
  note.textContent = "Pick your genre, then the style. It tells Mixology what you're going for: the Tone page's target, the Mix Assistant's advice, and the levels Auto Mix and Balance aim for.";
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

export function openSuggestion(app, sg) {
  const body = sheet("MIX ASSISTANT");
  body.className = "scroll";
  const [colour, kindLabel] = KIND[sg.kind] || KIND.info;
  const head = el("div", "", body, { display: "flex", alignItems: "center", gap: "16px", marginBottom: "12px" });
  head.innerHTML = `<span style="width:54px;height:54px;flex:none;color:${colour};filter:drop-shadow(0 0 6px ${colour})">${icon(sg.icon)}</span>
    <span><span style="display:block;font-size:30px;font-weight:600">${sg.title}</span>
    <span style="font-size:16px;letter-spacing:0.12em;text-transform:uppercase;color:${colour}">${kindLabel}</span></span>`;
  el("p", "", body, { fontSize: "21px", lineHeight: "1.5", margin: "0 0 16px" }).textContent = sg.text;
  const need = el("div", "card", body, { position: "relative", padding: "14px 18px", marginBottom: "16px", fontSize: "20px", lineHeight: "1.45", borderColor: colour });
  need.innerHTML = `<b style="color:var(--gold)">Do I need to fix it?</b> ${sg.need}`;
  if (sg.steps && sg.steps.length) {
    el("div", "", body, { fontSize: "18px", fontWeight: "600", color: "var(--gold)", margin: "4px 0 6px", letterSpacing: "0.06em" }).textContent = "HOW";
    const ol = el("ol", "", body, { fontSize: "20px", lineHeight: "1.5", margin: "0 0 16px", paddingLeft: "28px" });
    for (const st of sg.steps) el("li", "", ol, { marginBottom: "6px" }).textContent = st;
  }
  if (sg.id && sg.id.startsWith("vocal-") && sg.id !== "vocal-pocket") {
    const taste = el("div", "", body, { display: "flex", flexWrap: "wrap", gap: "10px", alignItems: "center", margin: "0 0 16px" });
    el("span", "", taste, { fontSize: "19px", color: "var(--gold)" }).textContent = "To your ears, the vocal sounds:";
    for (const [value, label] of [["louder", "Too quiet"], ["right", "Sounds right"], ["quieter", "Too loud"]]) {
      const b = el("button", "small-btn", taste, { height: "44px", fontSize: "19px" });
      b.textContent = label;
      b.addEventListener("click", async () => { const r = await app.command("vocalTaste", { value }); if (r && r.ok) $("overlay").classList.remove("shown"); });
    }
    el("div", "", taste, { width: "100%", fontSize: "16px", color: "var(--text-dim)" }).textContent = "Mixology remembers it for this song: the advice, Balance and Auto Mix follow your ears.";
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
    el("span", "", row, { fontSize: "18px", color: "var(--text-dim)", marginRight: "4px" }).textContent = "Tracks:";
    for (const t of sg.tracks) el("span", "chip", row).textContent = t;
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
      roleBtn.textContent = R.label + (t.roleFrom === "you" ? "" : t.roleFrom === "name" ? "  (name)" : "  (sound)");
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
      ro.textContent = `How your tracks reach the Stereo Out (worked out from the sound: group faders, group processing, pan law): `
        + `instruments ${fmt.db(report.musicPathDb)}, vocals ${fmt.db(report.vocalPathDb)} on the way. The levels above include it.`;
    }
  };
  render();
}

