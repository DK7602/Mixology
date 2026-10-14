// Milestone 4: the Overview's smart tools. Each one shows its plan first (every change, old -> new, and why);
// nothing moves until you press Apply, and Undo puts back what it changed. Reference Match compares your
// mix with a reference song and switches between them, level-matched.

import { $, el, icon, fmt, graph, freqToX, cssVar } from "./ui.js";
import { ROLES } from "./roles.js";
import { sheet } from "./sheets.js";

const TOOL_ICON = { auto: "wand", balance: "sliders", clean: "wave", pocket: "mic", stereo: "rings" };
const TOOL_ABOUT = {
  auto: "Everything below together: levels, pans, low cuts, mud and vocal pocket dips.",
  balance: "Fader levels: the beat against itself, the vocal against the beat, and room for Masterology.",
  clean: "Assist EQ in each track's Mixology Link: low cuts where there's rumble, dips where the mix is muddy.",
  pocket: "Assist EQ dips in the instruments that cover the vocal's words (1.6 - 4 kHz).",
  stereo: "Pans: low end and lead vocal in the middle, pairs and doubles spread, a lopsided mix evened out.",
};
const KIND_LABEL = { level: "Fader", pan: "Pan", lowcut: "Low cut", mud: "Mud dip", pocket: "Pocket dip" };

function valueText(c, v) {
  if (c.kind === "level") return fmt.db(v, 1);
  if (c.kind === "pan") return fmt.pan(v);
  if (c.kind === "lowcut") return v < 20 ? "Off" : fmt.hz(v);
  return Math.abs(v) < 0.05 ? "None" : fmt.db(v, 1) + " at " + fmt.hz(c.hz);
}

let openTool = null;   // re-renders the open tool sheet when the bridge reports progress

export function hookTools(app) {
  app.on("bridge", () => { if (openTool) openTool(); });
}

export async function openToolSheet(app, tool) {
  const body = sheet("");
  body.style.display = "flex";
  body.style.flexDirection = "column";
  const r = await app.command("plan", { value: tool });
  const plan = r && r.plan;
  if (!plan) return;
  $("sheet-title").textContent = plan.title.toUpperCase();

  const picked = new Set(plan.changes.map((_, i) => i));
  let allowBig = false, lastSerial = null, waiting = false, headroomOn = false, reopenAfter = null;
  const reopen = () => { openTool = null; openToolSheet(app, tool); };

  // ---- head: what this tool does, and what it found
  const head = el("div", "", body, { display: "flex", gap: "16px", alignItems: "flex-start", marginBottom: "12px", flex: "none" });
  const ic = el("span", "", head, { width: "50px", height: "50px", flex: "none", color: "var(--gold)", filter: "drop-shadow(0 0 6px rgba(255,170,60,0.5))" });
  ic.innerHTML = icon(TOOL_ICON[tool] || "wand");
  const ht = el("div", "", head, { flex: "1" });
  el("div", "", ht, { fontSize: "17px", color: "var(--text-dim)", marginBottom: "4px" }).textContent = TOOL_ABOUT[tool] || "";
  el("div", "", ht, { fontSize: "22px", lineHeight: "1.35" }).textContent = plan.summary;

  if (!plan.valid) {
    const go = el("button", "small-btn red", body, { alignSelf: "flex-start", marginTop: "10px", height: "48px", fontSize: "20px" });
    go.textContent = "Close, then press Analyze Mix";
    go.addEventListener("click", () => $("overlay").classList.remove("shown"));
    return;
  }

  // ---- your ears: how the vocal sounds right now (Balance and Auto Mix aim for it; saved with the project)
  const report = app.bridge && app.bridge.analysis && app.bridge.analysis.report;
  if ((tool === "balance" || tool === "auto") && report && report.hasVocal) {
    const taste = el("div", "", body, { display: "flex", alignItems: "center", gap: "10px", margin: "0 0 10px", flex: "none", fontSize: "18px", flexWrap: "wrap" });
    el("span", "", taste, { color: "var(--gold)" }).textContent = "Your vocal right now sounds:";
    for (const [value, label] of [["louder", "Too quiet"], ["right", "Right"], ["quieter", "Too loud"]]) {
      const b = el("button", "small-btn", taste);
      b.textContent = label;
      b.addEventListener("click", async () => { const res = await app.command("vocalTaste", { value }); if (res && res.ok) setTimeout(reopen, 150); });
    }
    const st = el("span", "", taste, { color: "var(--text-dim)", fontSize: "16px" });
    st.textContent = report.vocalFromYou ? `Aiming for your setting (${fmt.db(report.vocalTarget)} against the band).`
      : `Aiming for ${report.style}'s usual level (${fmt.db(report.vocalTarget)}). Your ears decide: tell Mixology.`;
    if (report.vocalFromYou) {
      const back = el("button", "small-btn", taste, { fontSize: "15px", height: "34px" });
      back.textContent = "Use the style's level";
      back.addEventListener("click", async () => { const res = await app.command("vocalTaste", { value: "style" }); if (res && res.ok) setTimeout(reopen, 150); });
    }
  }

  // ---- a channel that sounds like a group the instruments play through: ask once
  if (plan.groupHints && plan.groupHints.length) {
    const gh = el("div", "", body, { display: "flex", alignItems: "center", gap: "10px", margin: "0 0 10px", flex: "none", fontSize: "18px", flexWrap: "wrap" });
    el("span", "", gh, { color: "var(--gold)" }).textContent = plan.groupHints.length === 1
      ? `'${plan.groupHints[0]}' adds no sound of its own. Do your instruments play through it?`
      : "These channels add no sound of their own. Do your instruments play through one of them?";
    for (const n of plan.groupHints) {
      const b = el("button", "small-btn", gh);
      b.textContent = plan.groupHints.length === 1 ? "Yes, it's the music group" : `Yes: '${n}'`;
      b.addEventListener("click", async () => { const res = await app.command("setMusicBus", { name: n, value: true }); if (res && res.ok) setTimeout(reopen, 150); });
    }
  }

  // ---- toolbar
  const bar = el("div", "", body, { display: "flex", alignItems: "center", gap: "14px", margin: "4px 0 10px", flex: "none", fontSize: "18px" });
  const allBtn = el("button", "small-btn", bar);
  const bigs = plan.changes.filter((c) => c.big).length;
  let bigBtn = null;
  if (bigs) {
    bigBtn = el("button", "small-btn", bar);
    bigBtn.title = "Mixology moves a fader at most 6 dB at a time unless you allow more.";
  }
  const count = el("span", "", bar, { marginLeft: "auto", color: "var(--text-dim)" });

  // ---- the changes
  const list = el("div", "scroll", body, { flex: "1", overflowY: "auto", paddingRight: "8px", minHeight: "0" });
  const grid = el("div", "", list, { display: "grid", gridTemplateColumns: "34px 34px minmax(150px, 1.1fr) 104px minmax(200px, 1.1fr) 2fr", gap: "10px 12px", alignItems: "center" });
  for (const h of ["", "", "Track", "Change", "Now → new", "Why"])
    el("div", "", grid, { color: "var(--gold)", fontSize: "14px", letterSpacing: "0.08em", textTransform: "uppercase", position: "sticky", top: "0", background: "var(--panel)", zIndex: "1", padding: "2px 0" }).textContent = h;
  const rows = [];
  const order = plan.changes.map((c, i) => ({ c, i })).sort((a, b) => a.c.name.localeCompare(b.c.name) || Object.keys(KIND_LABEL).indexOf(a.c.kind) - Object.keys(KIND_LABEL).indexOf(b.c.kind));
  let lastName = null;
  for (const { c, i } of order) {
    const R = ROLES[c.role] || ROLES.other;
    const box = el("button", "", grid, { width: "30px", height: "30px", borderRadius: "7px", border: "1.5px solid var(--btn-edge)", background: "var(--btn)", color: "var(--green)", cursor: "pointer", padding: "4px", display: "grid", placeItems: "center" });
    box.addEventListener("click", () => { picked.has(i) ? picked.delete(i) : picked.add(i); refresh(); });
    const ri = el("div", "", grid, { width: "30px", height: "30px", color: R.colour, filter: `drop-shadow(0 0 4px ${R.colour})`, opacity: c.name === lastName ? "0" : "1" });
    ri.innerHTML = icon(R.icon);
    const nm = el("div", "", grid, { fontSize: "19px", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis", opacity: c.name === lastName ? "0.45" : "1" });
    nm.textContent = c.name;
    nm.title = c.name;
    lastName = c.name;
    el("div", "", grid, { fontSize: "17px", color: "var(--text-dim)" }).textContent = KIND_LABEL[c.kind] || c.kind;
    const val = el("div", "", grid, { fontSize: "19px", whiteSpace: "nowrap" });
    const why = el("div", "", grid, { fontSize: "15.5px", lineHeight: "1.3", color: "var(--text-dim)" });
    why.textContent = c.why.charAt(0).toUpperCase() + c.why.slice(1) + ".";
    rows.push({ c, i, box, val });
  }
  if (!plan.changes.length) el("div", "", list, { fontSize: "20px", marginTop: "14px", color: "var(--green)" }).textContent = "Nothing to change: this part of your mix already fits.";

  // ---- room for Masterology: on its own, unticked (the mix gets quieter, and quieter always sounds worse at first)
  let headBox = null;
  if (plan.headroomDb < 0) {
    const hr = el("div", "", list, { display: "grid", gridTemplateColumns: "34px 1fr", gap: "6px 12px", alignItems: "start", marginTop: "18px", padding: "12px 14px", borderRadius: "10px", border: "1.5px solid var(--panel-edge)" });
    headBox = el("button", "", hr, { width: "30px", height: "30px", borderRadius: "7px", border: "1.5px solid var(--btn-edge)", background: "var(--btn)", color: "var(--green)", cursor: "pointer", padding: "4px", display: "grid", placeItems: "center" });
    headBox.addEventListener("click", () => { headroomOn = !headroomOn; refresh(); });
    const ht2 = el("div", "", hr);
    el("div", "", ht2, { fontSize: "19px", fontWeight: "600" }).textContent = `Also make room for Masterology: every fader ${fmt.db(plan.headroomDb)} (${plan.headroomCount} channels)`;
    el("div", "", ht2, { fontSize: "15.5px", lineHeight: "1.35", color: "var(--text-dim)", marginTop: "3px" }).textContent = plan.headroomWhy + " Best done last, when the balance is right.";
  }

  // ---- what it left alone
  if (plan.notes.length) {
    const nb = el("div", "", list, { marginTop: "18px", fontSize: "16px", color: "var(--text-dim)", lineHeight: "1.4" });
    el("div", "", nb, { color: "var(--gold)", fontSize: "14px", letterSpacing: "0.08em", marginBottom: "4px" }).textContent = "LEFT AS IT IS";
    for (const n of plan.notes) el("div", "", nb, { marginBottom: "3px" }).textContent = "• " + n;
  }

  // ---- footer: progress, Undo, Apply
  const foot = el("div", "", body, { display: "flex", alignItems: "center", gap: "14px", marginTop: "12px", flex: "none", borderTop: "1.5px solid var(--panel-edge)", paddingTop: "12px" });
  const status = el("div", "", foot, { flex: "1", fontSize: "17px", lineHeight: "1.35", color: "var(--text-dim)" });
  const undoBtn = el("button", "small-btn", foot, { height: "48px", fontSize: "19px" });
  const applyBtn = el("button", "small-btn red", foot, { height: "48px", fontSize: "20px", minWidth: "220px" });

  allBtn.addEventListener("click", () => {
    if (picked.size === plan.changes.length) picked.clear(); else plan.changes.forEach((_, i) => picked.add(i));
    refresh();
  });
  if (bigBtn) bigBtn.addEventListener("click", () => { allowBig = !allowBig; refresh(); });
  applyBtn.addEventListener("click", async () => {
    waiting = true;
    refresh();
    const res = await app.command("planApply", { id: plan.id, picked: [...picked], allowBig, headroom: headroomOn });
    waiting = false;
    if (res && res.ok) {
      const a = app.bridge && app.bridge.apply;
      lastSerial = a ? a.serial : null;
      applyBtn.dataset.done = "1";
    }
    refresh();
  });
  // Undo, then the same tool again with a fresh plan (once Cubase has the faders back).
  undoBtn.addEventListener("click", async () => {
    const before = (app.bridge && app.bridge.apply && app.bridge.apply.serial) ?? null;
    const res = await app.command("planUndo");
    if (!res || !res.ok) return;
    if (res.undo && res.undo.bridge) reopenAfter = before;
    else setTimeout(reopen, 600);
    refresh();
  });

  function refresh() {
    if (!document.body.contains(body) || $("sheet-title").textContent !== plan.title.toUpperCase()) { openTool = null; return; }
    for (const { c, i, box, val } of rows) {
      const on = picked.has(i);
      box.innerHTML = on ? icon("check", 'stroke-width="3"') : "";
      const to = c.kind === "level" && allowBig ? c.wanted : c.to;
      val.innerHTML = `<span style="color:var(--text-dim)">${valueText(c, c.from)}</span> → <b style="color:${on ? "var(--text)" : "var(--text-dim)"}">${valueText(c, to)}</b>`
        + (c.big ? ` <span style="color:var(--amber);font-size:15px" title="The whole move is ${fmt.db(c.wanted - c.from)}">${allowBig ? "" : "(limited)"}</span>` : "");
      val.style.opacity = on ? "1" : "0.5";
    }
    allBtn.textContent = picked.size === plan.changes.length ? "Untick all" : "Tick all";
    if (bigBtn) {
      bigBtn.textContent = allowBig ? `Moves over 6 dB: allowed` : `Allow moves over 6 dB (${bigs})`;
      bigBtn.style.color = allowBig ? "var(--amber)" : "";
    }
    count.textContent = `${picked.size} of ${plan.changes.length} ticked`;
    if (headBox) headBox.innerHTML = headroomOn ? icon("check", 'stroke-width="3"') : "";

    const a = (app.bridge && app.bridge.apply) || {};
    if (reopenAfter !== null && !a.active && a.serial !== reopenAfter) { reopenAfter = null; setTimeout(reopen, 300); }
    const n = picked.size + (headroomOn ? 1 : 0);
    const done = applyBtn.dataset.done === "1";
    if (a.active) status.innerHTML = `<span style="color:var(--cyan)">${a.what}: ${a.done} of ${a.total} channels…</span> Mixology steps through Cubase's channels and comes back.`;
    else if (done && lastSerial !== null && a.serial !== lastSerial && a.message) {
      status.innerHTML = `<span style="color:var(--green)">${a.message}</span>` + (a.notes && a.notes.length ? `<br>${a.notes.slice(0, 3).join("<br>")}` : "");
    } else if (done) status.innerHTML = `<span style="color:var(--green)">Applied.</span> Listen, then keep it or press Undo.`;
    else status.textContent = plan.changes.length || plan.headroomDb < 0 ? "Nothing changes until you press Apply. Protected channels are never touched." : "";
    if (reopenAfter !== null) status.innerHTML = `<span style="color:var(--cyan)">Undoing…</span> the list comes back fresh when Cubase has the faders back.`;
    undoBtn.style.display = a.canUndo && reopenAfter === null ? "" : "none";
    undoBtn.textContent = "Undo " + (a.undoWhat || "");
    applyBtn.disabled = waiting || a.active || !n || done || reopenAfter !== null;
    applyBtn.textContent = done ? "Applied" : `Apply ${n} change${n === 1 ? "" : "s"}`;
  }
  openTool = refresh;
  refresh();
}

// ---------------------------------------------------------------------------------------------
// Reference Match

let openRef = null;

export function openReferenceSheet(app) {
  const body = sheet("REFERENCE MATCH");
  body.className = "scroll";
  const render = () => {
    body.innerHTML = "";
    const an = app.bridge && app.bridge.analysis;
    const report = an && an.report;
    const intro = el("p", "", body, { fontSize: "19px", lineHeight: "1.45", margin: "0 0 14px", color: "var(--text-dim)" });
    intro.textContent = "Compare your mix with a finished song you like. Put that song on its own track under yours (same time), insert Mixology Link on it and mute it: its Link still hears it. Then Analyze Mix.";
    if (!report) { el("p", "", body, { fontSize: "21px" }).textContent = "Press Analyze Mix first (with your reference on its track, muted)."; return; }

    const ref = report.reference || {};
    if (!ref.chosen) {
      el("div", "", body, { fontSize: "22px", fontWeight: "600", margin: "6px 0 10px" }).textContent = "Which track is your reference song?";
      const grid = el("div", "", body, { display: "grid", gridTemplateColumns: "repeat(3, 1fr)", gap: "10px" });
      for (const t of report.tracks) {
        const b = el("button", "small-btn", grid, { height: "52px", fontSize: "19px", textAlign: "left", overflow: "hidden", textOverflow: "ellipsis" });
        b.textContent = t.name;
        b.addEventListener("click", async () => { await app.command("setReference", { hash: t.hash, value: true }); setTimeout(render, 250); });
      }
      return;
    }

    const headRow = el("div", "", body, { display: "flex", alignItems: "center", gap: "12px", marginBottom: "12px" });
    el("div", "", headRow, { fontSize: "24px", fontWeight: "600", flex: "1" }).textContent = `Your mix against '${ref.name}'`;
    const change = el("button", "small-btn", headRow);
    change.textContent = "Choose another";
    change.addEventListener("click", async () => { await app.command("setReference", { hash: ref.hash, value: false }); setTimeout(render, 250); });

    if (!ref.heard) {
      el("p", "", body, { fontSize: "20px", lineHeight: "1.45" }).textContent = `Mixology didn't hear '${ref.name}' while it listened. Make sure it plays under your song (same bars), with Mixology Link on it, then Analyze Mix again. It can stay muted.`;
      return;
    }

    // Curves: your mix and the reference, lined up the same way.
    const wrap = el("div", "", body, { position: "relative", height: "300px", marginBottom: "14px" });
    const g = graph(wrap, 0, 0, 1140, 300);
    g.draw = (ctx, w, h) => {
      const grid = cssVar("--grid");
      ctx.strokeStyle = grid; ctx.lineWidth = 1;
      for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) { const x = 10 + freqToX(f, w - 20); ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke(); }
      const yOf = (db) => h / 2 - db * (h / 20);
      const line = (bands, colour, width) => {
        ctx.strokeStyle = colour; ctx.lineWidth = width; ctx.shadowColor = colour; ctx.shadowBlur = 6;
        ctx.beginPath();
        let started = false;
        bands.forEach((v, b) => {
          const hz = 1000 * Math.pow(2, (b - 17) / 3);
          if (hz < 25 || hz > 18000) return;
          const sv = 0.25 * (bands[b - 1] ?? v) + 0.5 * v + 0.25 * (bands[b + 1] ?? v);
          const x = 10 + freqToX(hz, w - 20), y = yOf(sv);
          started ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
          started = true;
        });
        ctx.stroke(); ctx.shadowBlur = 0;
      };
      if (ref.bands && ref.bands.length) line(ref.bands, "#ffb43c", 3);
      if (report.mixBands && report.mixBands.length) line(report.mixBands, "#19d3ff", 3);
      ctx.font = "16px 'Barlow SC', sans-serif"; ctx.textAlign = "left";
      ctx.fillStyle = "#19d3ff"; ctx.fillText("Your mix", 16, 24);
      ctx.fillStyle = "#ffb43c"; ctx.fillText(ref.name, 110, 24);
      ctx.fillStyle = cssVar("--text-dim"); ctx.textAlign = "center";
      for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) ctx.fillText(fmt.hz(f), 10 + freqToX(f, w - 20), h - 8);
    };
    g.redraw && g.redraw();

    // Region by region, in words.
    const rows = el("div", "", body, { display: "grid", gridTemplateColumns: "260px 220px 1fr", gap: "8px 16px", alignItems: "center", fontSize: "19px", marginBottom: "14px" });
    for (const rg of ref.regions || []) {
      el("div", "", rows, { color: "var(--text-dim)" }).textContent = rg.name;
      const barBox = el("div", "", rows, { position: "relative", height: "14px", borderRadius: "7px", background: "var(--graph-bg)", border: "1px solid var(--panel-edge)" });
      const span = Math.min(1, Math.abs(rg.diff) / 9) * 50;
      el("div", "", barBox, { position: "absolute", top: "0", bottom: "0", left: rg.diff >= 0 ? "50%" : 50 - span + "%", width: span + "%", borderRadius: "7px", background: Math.abs(rg.diff) < 1.5 ? "var(--green)" : "var(--amber)" });
      el("div", "", barBox, { position: "absolute", left: "50%", top: "-3px", bottom: "-3px", width: "2px", background: "var(--text-dim)" });
      el("div", "", rows).textContent = rg.text;
    }
    const more = el("div", "", body, { fontSize: "18px", lineHeight: "1.5", color: "var(--text-dim)", marginBottom: "16px" });
    const louder = ref.loudnessDiff;
    more.innerHTML = (Math.abs(louder) < 1 ? `Loudness: about the same as '${ref.name}'.`
      : `Loudness: '${ref.name}' is ${fmt.num(Math.abs(louder), 1)} LU ${louder > 0 ? "louder" : "quieter"}. ${louder > 0 ? "That's mastering: Masterology closes that gap, so don't push your mix up to match." : ""}`)
      + `<br>Width: your mix is ${Math.abs(ref.widthDiff) < 0.05 ? "about as wide as" : ref.widthDiff > 0 ? "wider than" : "narrower than"} the reference.`;

    // A / B, level-matched.
    const ab = el("div", "", body, { display: "flex", gap: "14px", alignItems: "center" });
    const hearing = !!(app.bridge && app.bridge.apply && app.bridge.apply.hearingReference);
    const toRef = el("button", "small-btn" + (hearing ? "" : " red"), ab, { height: "52px", fontSize: "20px", minWidth: "260px" });
    toRef.textContent = "Hear the reference";
    toRef.disabled = hearing;
    toRef.addEventListener("click", async () => { await app.command("refListen", { value: "ref" }); setTimeout(render, 300); });
    const toMix = el("button", "small-btn" + (hearing ? " red" : ""), ab, { height: "52px", fontSize: "20px", minWidth: "220px" });
    toMix.textContent = "Back to your mix";
    toMix.disabled = !hearing;
    toMix.addEventListener("click", async () => { await app.command("refListen", { value: "mix" }); setTimeout(render, 300); });
    el("div", "", ab, { fontSize: "16px", color: "var(--text-dim)", lineHeight: "1.35" }).textContent = "The reference plays alone (solo), its Trim matched to your mix's loudness, so you compare sound, not volume.";
  };
  openRef = render;
  render();
}

export function referenceIsOpen() { return !!openRef && $("sheet-title").textContent === "REFERENCE MATCH" && $("overlay").classList.contains("shown"); }
