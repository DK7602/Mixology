// Overview and Tracks pages.

import { el, box, icon, fmt, clamp, Knob, Meter, select, graph, freqToX, previewTag, store, popupMenu } from "./ui.js";
import { ROLES, ROLE_ORDER, GROUPS, guessRole, roleIcon } from "./roles.js";
import { openSuggestion, openTracksSheet } from "./sheets.js";
import { openToolSheet, openReferenceSheet } from "./tools.js";
import { dawName, say } from "./daws.js";

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);

export { ROLES, guessRole };

// =============================================================================================
// OVERVIEW


export function buildOverview(root, app) {
  // ---- left: operations
  const ops = [
    { id: "auto", label: "AUTO MIX", icon: "wand", tip: "Everything together: levels, pans, low cuts, vocal pocket, EQ, compression, space and the mix's tone. See every change first, then Apply" },
    { id: "polish", label: "POLISH", icon: "star", tip: "EQ, compression, vocal reverb / delay and the mix's tone, in each Mixology Link: see every change first, then Apply" },
    { id: "balance", label: "BALANCE", icon: "sliders", tip: "Fader levels for your style, and room for Masterology" },
    { id: "clean", label: "CLEAN UP", icon: "wave", tip: "Low cuts and mud dips (Assist EQ in each Mixology Link)" },
    { id: "pocket", label: "VOCAL POCKET", icon: "mic", tip: "Dips in the instruments that cover the vocal's words" },
    { id: "stereo", label: "STEREO ARCHITECT", icon: "rings", tip: "Pans: low end centred, pairs spread, a lopsided mix evened out" },
    { id: "ref", label: "REFERENCE MATCH", icon: "target", tip: "Your mix against a song you like, level-matched" },
  ];
  const analyze = el("button", "btn big active", root, box(0, 0, 252, 112));
  analyze.innerHTML = `ANALYZE MIX<span class="sub">Scan tracks and build a mix</span>`;
  const busy = () => { const a = app.bridge && app.bridge.analysis; return a && (a.state === "reading" || a.state === "listening"); };
  analyze.addEventListener("click", () => {
    if (busy()) return;
    const st = app.style;
    app.command("analyze", { value: { key: st.key, name: st.fullName, bass: st.bass, scoop: st.scoop, air: st.air } });
  });
  ops.forEach((o, i) => {
    const b = el("button", "btn", root, box(0, 128 + i * 102, 252, 90));
    b.innerHTML = icon(o.icon) + `<span>${o.label}</span>`;
    b.title = o.tip;
    b.addEventListener("click", () => (o.id === "ref" ? openReferenceSheet(app) : openToolSheet(app, o.id)));
  });

  // ---- centre: analyzer
  const an = el("div", "panel", root, box(266, 0, 830, 556));
  const t = el("div", "panel-title", an);
  t.textContent = "MIX ANALYZER";
  const legend = el("div", "", an, { position: "absolute", left: "218px", top: "27px", display: "flex", gap: "11px", fontSize: "15px" });
  legend.title = "The coloured lines: each kind of track, heard through its Mixology Link (after its fader). The rainbow: your whole mix.";
  const legendItems = GROUPS.map((gr) => {
    const item = el("span", "", legend, { display: "flex", alignItems: "center", gap: "7px", opacity: "0.9" });
    el("span", "", item, { width: "14px", height: "14px", background: gr.colour, boxShadow: `0 0 6px ${gr.colour}` });
    item.appendChild(document.createTextNode(gr.label));
    return item;
  });
  const viewSel = select(an, 690, 16, 126, ["Live Mix", "Slow", "Peak Hold"], store.get("anView", "Live Mix"), (v) => { store.set("anView", v); hold = null; });

  const g = graph(an, 18, 72, 680, 466);
  const meters = { l: new Meter(an, 742, 92, 16, 400, [-48, 0]), r: new Meter(an, 764, 92, 16, 400, [-48, 0]), lufs: new Meter(an, 790, 92, 16, 400, [-48, 0]) };
  const scale = el("div", "", an, { position: "absolute", left: "708px", top: "84px", width: "30px", height: "420px", fontSize: "15px", color: "var(--text-dim)" });
  for (const db of [0, -6, -12, -24, -36, -48]) {
    const lab = el("div", "", scale, { position: "absolute", right: "2px", top: ((-db / 48) * 400 - 1) + "px" });
    lab.textContent = db === 0 ? "0" : fmt.num(db, 0);
  }
  const ml = el("div", "", an, { position: "absolute", left: "736px", top: "502px", width: "90px", display: "flex", gap: "8px", fontSize: "16px", color: "var(--text-dim)" });
  ml.innerHTML = "<span style='width:14px;text-align:center'>L</span><span style='width:14px;text-align:center'>R</span><span>LUFS</span>";

  let spectrum = null, hold = null, smooth = null, curves = null;
  const DB_MIN = -48, DB_MAX = 12, OFFSET = 40;   // tilted spectrum reads about -40 for a balanced mix: centre it on 0
  const yOf = (db, h) => 12 + (1 - (db - DB_MIN) / (DB_MAX - DB_MIN)) * (h - 12);
  g.draw = (ctx, w, h) => {
    const plotH = h - 34;
    ctx.font = "15px 'Barlow SC', sans-serif";
    ctx.lineWidth = 1;
    for (const f of [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]) {
      const x = freqToX(f, w - 40) + 34;
      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue("--grid");
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, plotH); ctx.stroke();
      ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--text-dim");
      ctx.textAlign = "center";
      ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), clamp(x, 14, w - 14), h - 10);
    }
    for (const db of [12, 6, 0, -6, -12, -24, -36, -48]) {
      const y = yOf(db, plotH);
      ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue(db === 0 ? "--grid-strong" : "--grid");
      ctx.beginPath(); ctx.moveTo(34, y); ctx.lineTo(w, y); ctx.stroke();
      ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue("--text-dim");
      ctx.textAlign = "right";
      ctx.fillText(db > 0 ? "+" + db : String(db), 28, y + 5);
    }
    if (!spectrum) return;
    const n = spectrum.length, W = w - 40;
    const pts = spectrum.map((v, i) => [34 + (i / (n - 1)) * W, yOf(clamp(v + OFFSET, DB_MIN - 6, DB_MAX), plotH)]);
    const rainbow = ctx.createLinearGradient(34, 0, w, 0);
    ["#ff1d1d", "#ff7a12", "#ffe11f", "#3dff5a", "#1fd1ff", "#1f6bff", "#9b3dff", "#ff2bf0"].forEach((c, i, a) => rainbow.addColorStop(i / (a.length - 1), c));

    // Filled "mountains", fading towards the floor.
    ctx.save();
    ctx.beginPath();
    ctx.moveTo(pts[0][0], plotH);
    for (const [x, y] of pts) ctx.lineTo(x, y);
    ctx.lineTo(pts[n - 1][0], plotH);
    ctx.closePath();
    ctx.clip();
    ctx.globalAlpha = 0.75;
    ctx.fillStyle = rainbow;
    ctx.fillRect(34, 0, W, plotH);
    ctx.globalAlpha = 1;
    const fade = ctx.createLinearGradient(0, 0, 0, plotH);
    const dark = document.documentElement.dataset.theme !== "light";
    fade.addColorStop(0, dark ? "rgba(6,10,16,0)" : "rgba(250,251,253,0)");
    fade.addColorStop(1, dark ? "rgba(6,10,16,0.92)" : "rgba(250,251,253,0.9)");
    ctx.fillStyle = fade;
    ctx.fillRect(34, 0, W, plotH);
    ctx.restore();

    ctx.save();
    ctx.lineWidth = 2.6;
    ctx.strokeStyle = rainbow;
    ctx.shadowColor = dark ? "rgba(255,255,255,0.35)" : "rgba(0,0,0,0.15)";
    ctx.shadowBlur = dark ? 10 : 3;
    ctx.beginPath();
    pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
    ctx.stroke();
    ctx.restore();

    // Smoothed overall balance line (white), like the mockup's mix line.
    if (smooth) {
      ctx.lineWidth = 2;
      ctx.strokeStyle = dark ? "rgba(255,255,255,0.92)" : "rgba(20,28,40,0.85)";
      ctx.beginPath();
      smooth.forEach((v, i) => {
        const x = 34 + (i / (n - 1)) * W, y = yOf(clamp(v + OFFSET, DB_MIN - 6, DB_MAX), plotH);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
    }
    // Each kind of track (Mixology Link), in its colour, through the 1/3-octave band centres.
    if (curves) {
      ctx.save();
      ctx.lineWidth = 2.4;
      curves.forEach((c, gi) => {
        if (!c) return;
        const colour = GROUPS[gi].colour;
        ctx.strokeStyle = colour;
        ctx.shadowColor = colour;
        ctx.shadowBlur = dark ? 8 : 2;
        ctx.beginPath();
        const pts = [];
        c.forEach((v, b) => {
          const hz = 1000 * Math.pow(2, (b - 17) / 3);
          if (hz < 20 || hz > 20000) return;
          const sv = 0.25 * (c[b - 1] ?? v) + 0.5 * v + 0.25 * (c[b + 1] ?? v);
          pts.push([34 + freqToX(hz, W), yOf(clamp(sv + OFFSET, DB_MIN - 6, DB_MAX), plotH)]);
        });
        pts.forEach(([x, y], i) => {
          if (!i) { ctx.moveTo(x, y); return; }
          const [px, py] = pts[i - 1];
          i === pts.length - 1 ? ctx.quadraticCurveTo(px, py, x, y) : ctx.quadraticCurveTo(px, py, (px + x) / 2, (py + y) / 2);
        });
        ctx.stroke();
      });
      ctx.restore();
    }
    if (hold) {
      ctx.setLineDash([4, 4]);
      ctx.lineWidth = 1.2;
      ctx.strokeStyle = dark ? "rgba(255,255,255,0.5)" : "rgba(20,28,40,0.5)";
      ctx.beginPath();
      hold.forEach((v, i) => {
        const x = 34 + (i / (n - 1)) * W, y = yOf(clamp(v + OFFSET, DB_MIN - 6, DB_MAX), plotH);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
      ctx.setLineDash([]);
    }
  };

  // ---- Analyze Mix: the listening panel (over the analyzer while it listens)
  const lp = el("div", "card", an, { position: "absolute", left: "120px", top: "120px", width: "560px", padding: "22px 26px 20px", display: "none", zIndex: "4", boxShadow: "0 14px 44px rgba(0,0,0,0.55)", border: "2px solid var(--btn-edge)" });
  const lpTitle = el("div", "panel-title gold", lp, { position: "relative", left: "0", top: "0", fontSize: "26px", marginBottom: "8px" });
  lpTitle.textContent = "ANALYZE MIX";
  const lpText = el("div", "", lp, { fontSize: "19px", lineHeight: "1.4", minHeight: "54px" });
  const bar = el("div", "", lp, { position: "relative", height: "16px", borderRadius: "8px", background: "var(--well)", boxShadow: "inset 0 0 0 1px var(--panel-edge)", margin: "14px 0 6px" });
  const barFill = el("div", "", bar, { position: "absolute", left: "0", top: "0", bottom: "0", width: "0%", borderRadius: "8px", background: "linear-gradient(90deg,#1f6bff,#1fd1ff,#3dff5a)", boxShadow: "0 0 10px rgba(31,209,255,0.6)" });
  el("div", "", bar, { position: "absolute", left: "33.3%", top: "-4px", bottom: "-4px", width: "2px", background: "var(--gold)" });
  const barLabels = el("div", "", lp, { position: "relative", height: "20px", fontSize: "14px", color: "var(--text-dim)" });
  barLabels.innerHTML = `<span style="position:absolute;left:0">0 s</span><span style="position:absolute;left:33.3%;transform:translateX(-50%);color:var(--gold)">20 s: enough</span><span style="position:absolute;right:0">60 s</span>`;
  const lpButtons = el("div", "", lp, { display: "flex", gap: "10px", marginTop: "14px" });
  const finishBtn = el("button", "small-btn", lpButtons);
  finishBtn.textContent = "Finish now";
  finishBtn.addEventListener("click", () => app.command("analyzeFinish"));
  const cancelBtn = el("button", "small-btn", lpButtons);
  cancelBtn.textContent = "Cancel";
  cancelBtn.addEventListener("click", () => app.command("analyzeCancel"));

  function renderListening(b) {
    const a = b && b.analysis;
    const on = a && (a.state === "reading" || a.state === "listening");
    lp.style.display = on ? "block" : "none";
    const label = !a || !on ? `ANALYZE MIX<span class="sub">Scan tracks and build a mix</span>`
      : a.state === "reading" ? `READING FADERS<span class="sub">One moment...</span>`
      : `LISTENING ${Math.floor(a.seconds)} s<span class="sub">${a.playing ? "Keep playing" : "Press play in " + dawName(app.daw)}</span>`;
    if (analyze.dataset.label !== label) { analyze.dataset.label = label; analyze.innerHTML = label; }   // rebuilt only when it changes (keeps clicks)
    if (!on) return;
    const s = a.seconds || 0;
    barFill.style.width = Math.min(100, (s / (a.max || 60)) * 100) + "%";
    finishBtn.disabled = s < 10;
    lpText.innerHTML = a.state === "reading"
      ? "Reading every channel's fader in Cubase (a second or two). Nothing moves."
      : !a.playing
        ? `<b>Press play in ${dawName(app.daw)}.</b> Play the busiest part of the song (the hook or the drop), 30 to 60 seconds. Mixology listens to ${a.heard} track${a.heard === 1 ? "" : "s"} through Mixology Link and to the whole mix.`
        : `<b>Listening: ${Math.floor(s)} s</b> to ${a.heard} track${a.heard === 1 ? "" : "s"} and the whole mix. ${s < (a.enough || 20) ? "Keep playing..." : "That's enough to go on: press Finish now, or let it run to 60 s."}`;
  }
  app.on("bridge", renderListening);

  // ---- centre bottom: quick tone knobs = the Full Mix EQ's six bands (Milestone 5; they start flat)
  const qt = el("div", "", root, box(266, 570, 830, 272));
  const quick = ["LOW END", "LOW MID", "MID", "UPPER MID", "HIGH END", "AIR"];
  let quickTouched = 0;
  const quickKnobs = quick.map((label, i) => {
    const card = el("div", "card", qt, box(i * 140, 0, 130, 272));
    const k = new Knob(card, { x: -5, y: 24, w: 140, size: 106, label, min: -12, max: 12, value: 0, def: 0, bipolar: true, step: 0.1, format: (v) => fmt.db(v), labelGap: 18, valueGap: 14,
      onChange: (v) => { quickTouched = performance.now(); app.native.procSet("mix", { [`eq${i + 1}_gain`]: v, eq_on: 1 }); } });
    card.title = "The full mix's EQ, band " + (i + 1) + " (the EQ page with Full Mix picked shows the same bands)";
    return k;
  });
  // ---- right: Mix Assistant
  const ma = el("div", "panel", root, box(1110, 0, 302, 842));
  ma.insertAdjacentHTML("beforeend", `<div style="position:absolute;left:18px;top:18px;width:44px;height:44px;color:var(--amber)">${icon("bulb")}</div>`);
  const mt = el("div", "panel-title gold", ma, { left: "72px", top: "16px", fontSize: "28px" });
  mt.textContent = "MIX ASSISTANT";
  const sub = el("div", "", ma, { position: "absolute", left: "72px", top: "54px", right: "14px", fontSize: "17px", color: "var(--text)" });
  sub.textContent = "What Mixology hears right now:";
  const list = el("div", "scroll", ma, { position: "absolute", left: "12px", top: "92px", right: "12px", bottom: "12px", display: "flex", flexDirection: "column", gap: "10px", overflowY: "auto", overflowX: "hidden" });
  const maFoot = el("div", "", ma, { position: "absolute", left: "12px", right: "12px", bottom: "12px", height: "48px", display: "none", gap: "8px" });
  const tracksBtn = el("button", "small-btn", maFoot, { flex: "1" });
  tracksBtn.addEventListener("click", () => openTracksSheet(app));
  const liveBtn = el("button", "small-btn", maFoot, { flex: "1" });
  liveBtn.textContent = "Live view";
  let showLive = false;
  liveBtn.addEventListener("click", () => { showLive = !showLive; lastKey = ""; renderAssistant(); });
  const KIND = { warn: "#ff5a3c", info: "#19d3ff", calm: "#39e26b" };
  // Auto Analyze (after the first Analyze Mix): keeps listening while the song plays, new advice when it changed.
  const autoBtn = el("button", "small-btn", ma, { position: "absolute", left: "12px", right: "12px", bottom: "68px", height: "44px", display: "none", textAlign: "left", paddingLeft: "38px", fontSize: "17px" });
  autoBtn.title = "Auto Analyze: whenever the song plays, Mixology keeps listening and refreshes this advice every 15 seconds when something changed (after you move a fader or change an EQ, it listens again from there). Advice only: it never changes your mix.";
  autoBtn.addEventListener("click", () => { const au = app.bridge && app.bridge.analysis && app.bridge.analysis.auto; app.command("autoAnalyze", { value: !(au && au.on) }); });
  function renderAuto(b) {
    const a = b && b.analysis, au = a && a.auto;
    const show = !!(a && au && (a.report || au.on));
    autoBtn.style.display = show ? "block" : "none";
    autoBtn.style.bottom = a && a.report ? "68px" : "12px";   // above Your tracks / Live view once there is advice
    list.style.bottom = (show ? 56 : 0) + (a && a.report ? 70 : 12) + "px";
    if (!show) return;
    const status = !au.on ? "Off"
      : au.pause === "stopped" ? "Press play"
      : au.pause === "quiet" ? "Waiting for sound"
      : au.pause === "busy" ? "Paused" : au.pause ? "Off"
      : au.seconds < 20 ? `Listening ${au.seconds} s` : a.report ? "Listening" : "Checking...";
    const html = `<span class="lamp${au.on ? " on" : ""}${au.on && !au.pause ? " live" : ""}"></span>AUTO ANALYZE<span style="float:right;color:var(--text-dim)">${status}</span>`;
    if (autoBtn.dataset.html !== html) { autoBtn.dataset.html = html; autoBtn.innerHTML = html; }
  }
  app.on("bridge", renderAuto);
  app.on("daw", () => { lastKey = ""; renderAssistant(); });

  function card(iconName, colour, title, hint, onClick) {
    const c = el("div", "card", list, { position: "relative", flex: "none", minHeight: "104px", padding: "14px 34px 12px 70px", cursor: onClick ? "pointer" : "default" });
    c.innerHTML = `<div style="position:absolute;left:12px;top:22px;width:46px;height:46px;color:${colour};filter:drop-shadow(0 0 6px ${colour})">${icon(iconName)}</div>
      <div style="font-size:19px;font-weight:500;line-height:1.2">${title}</div>
      <div style="font-size:16px;margin-top:5px;line-height:1.25;color:var(--info)">${hint}</div>
      ${onClick ? `<div style="position:absolute;right:8px;top:36px;width:24px;height:24px;color:var(--text)">${icon("chevR", 'stroke-width="2.4"')}</div>` : ""}`;
    if (onClick) c.addEventListener("click", onClick);
    return c;
  }

  let lastKey = "";
  function renderAssistant() {
    const f = app.frame || {};
    const b = app.bridge || {};
    const st = f.shortTerm ?? -100, tp = f.truePeak ?? -100;
    const report = b.analysis && b.analysis.report;
    const useReport = report && !showLive;
    const setup = b.setup || [];
    const key = JSON.stringify(setup.map((x) => x.id + x.title)) + "|" + (useReport ? "r|" + JSON.stringify(report.suggestions.map((x) => x.id + x.kind + x.title)) + report.seconds + report.style + (b.analysis.auto && b.analysis.auto.on ? b.analysis.auto.updated : "")
      : [b.link, (b.slots || []).filter((s) => s.present).length, b.linksPaired, b.linksSeen, Math.round(st), Math.round(tp), !!report, !!(b.analysis && b.analysis.auto && b.analysis.auto.on), app.daw].join("|"));
    if (key === lastKey) return;
    lastKey = key;
    list.innerHTML = "";
    maFoot.style.display = report ? "flex" : "none";
    renderAuto(b);
    liveBtn.textContent = showLive ? "Analysis" : "Live view";
    if (report) tracksBtn.textContent = `Your tracks (${report.tracks.length})`;
    // The setup check first: things in the Cubase project no mixing move can fix (a mono group, vocals without a Link).
    const setupHint = (sg) => sg.id === "setup-mono-group" ? (sg.tracks && sg.tracks.length ? `Most likely '${esc(sg.tracks[0])}' is a mono group: click for the fix` : "A mono group or output on the way: click for the fix")
      : sg.id === "setup-vocal-link" ? "Add Mixology Link under Voxology on each: click for how"
      : sg.id === "setup-mono-mixology" ? `Put it on the ${say(app.daw, "master")} channel: click for how` : sg.need;
    for (const sg of setup) card(sg.icon, KIND.warn, sg.title, setupHint(sg), () => openSuggestion(app, sg));
    if (useReport) {
      const au = b.analysis.auto;
      sub.textContent = au && au.on && au.updated ? `Auto Analyze, updated ${new Date(au.updated).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" })}:`
        : `From Analyze Mix (${Math.round(report.seconds)} s heard):`;
      for (const sg of report.suggestions)
        card(sg.icon, KIND[sg.kind] || KIND.info, sg.title, sg.need, () => openSuggestion(app, sg));
      return;
    }
    sub.textContent = "What Mixology hears right now:";
    const n = (b.slots || []).filter((s) => s.present).length;
    if (b.link === "connected")
      card("check", "#39e26b", `Cubase connected: ${n} channel${n === 1 ? "" : "s"} in view`, b.linksPaired
        ? `${b.linksPaired} with Mixology Link · move any fader on the Tracks page`
        : "Move any fader on the Tracks page. Add Mixology Link to tracks to see their levels", () => app.showPage("tracks"));
    else if (app.daw !== "cubase")
      card("sliders", "#19d3ff", `${dawName(app.daw)}: you set the faders`, "Mixology moves faders in Cubase only (for now). Auto Mix and Balance list the moves for you", () => app.openSettings("bridge"));
    else
      card("link", "#ffc04a", "Not connected to Cubase", b.portProblem ? "Ports missing: open the setup steps" : "Open Settings > Cubase Bridge for the setup", () => app.openSettings("bridge"));
    if (st > -70)
      card("speaker", "#19d3ff", `Mix loudness ${fmt.lufs(st)} LUFS (short-term)`, "Information only: Masterology sets the final loudness");
    else
      card("speaker", "#19d3ff", "No audio yet", `Press play in ${dawName(app.daw)} to see the mix here`);
    if (tp > -70) {
      if (tp > -1) card("headroom", "#ff2b2b", `Peaks reach ${fmt.num(tp, 1)} dBTP`, "Pull the faders down a little: leave 3-6 dB for Masterology");
      else if (tp > -3) card("headroom", "#ffc04a", `Peaks reach ${fmt.num(tp, 1)} dBTP`, "A bit hot for mastering: aim for peaks around -6 to -3");
      else card("check", "#39e26b", `Headroom is good (peaks ${fmt.num(tp, 1)} dBTP)`, "Plenty of room for Masterology");
    }
    if (!report && b.analysis && b.analysis.auto && b.analysis.auto.on)
      card("bulb", "#ff36e1", "Auto Analyze is on", "Play the song: the first advice comes about 20 seconds in");
    else if (!report)
      card("bulb", "#ff36e1", "Press Analyze Mix for real advice", b.linksSeen ? `Play the busiest part of the song: Mixology listens to your ${b.linksSeen} track${b.linksSeen === 1 ? "" : "s"} with Mixology Link` : "First insert Mixology Link on your tracks (after their other plug-ins)");
  }

  return {
    onShow() { renderAssistant(); },
    onFrame(f) {
      meters.l.set(f.peakL ?? -100);
      meters.r.set(f.peakR ?? -100);
      meters.lufs.set((f.momentary ?? -100) + 0);
      if (f.mixEq && performance.now() - quickTouched > 800)
        quickKnobs.forEach((k, i) => {
          if (!k.dragging && Math.abs(k.value - f.mixEq[i]) > 0.01) k.set(f.mixEq[i]);
          const colour = f.mixEq[6] ? undefined : "#7d8796";   // grey while the full mix's EQ is off
          if (k.o.colour !== colour) k.setColour(colour);
        });
      if (f.roleCurves) {
        // Calm the lines down: blend over time, then a light smoothing across neighbouring bands.
        const prev = curves;
        curves = f.roleCurves.map((c, gi) => {
          if (!c) return null;
          const old = prev && prev[gi] && prev[gi].length === c.length ? prev[gi] : null;
          return old ? c.map((v, b) => 0.8 * old[b] + 0.2 * v) : c.slice();
        });
        legendItems.forEach((it, i) => { it.style.opacity = curves[i] ? "1" : "0.4"; });
      }
      if (f.spectrum) {
        const s = f.spectrum;
        const mode = viewSel.value;
        if (mode === "Slow") spectrum = spectrum && spectrum.length === s.length ? spectrum.map((v, i) => 0.85 * v + 0.15 * s[i]) : s.slice();
        else spectrum = s;
        hold = mode === "Peak Hold" ? (hold && hold.length === s.length ? hold.map((v, i) => Math.max(v - 0.05, s[i])) : s.slice()) : null;
        const k = 7;
        smooth = spectrum.map((_, i) => {
          let sum = 0, c = 0;
          for (let j = Math.max(0, i - k); j <= Math.min(spectrum.length - 1, i + k); j++) { sum += spectrum[j]; c++; }
          return sum / c - 3;
        });
        g.redraw();
      }
      renderAssistant();
    },
    always: false,
  };
}

// =============================================================================================
// TRACKS: the Cubase bank, live. Every strip moves its real MixConsole channel (Milestone 2).
// Each command names the channel the strip showed (slot + name hash): if the bank moved in between,
// Mixology refuses instead of moving another channel.

const STRIPS = 12;

export function buildTracks(root, app) {
  const panel = el("div", "panel", root, box(0, 0, 1412, 842));

  // Top bar: link status, bank buttons, Restore, hint
  const bar = el("div", "", panel, { position: "absolute", left: "16px", right: "16px", top: "10px", height: "48px", display: "flex", alignItems: "center", gap: "12px", fontSize: "18px" });
  const status = el("div", "", bar, { minWidth: "300px", whiteSpace: "nowrap" });
  // Scroll bar: where the 12 strips sit among all your channels (MixConsole order, learned by reading every fader
  // once). Drag the thumb, click the bar or scroll on it; Cubase's bank follows when you let go.
  const scroller = el("div", "", bar, { display: "flex", alignItems: "center", gap: "10px" });
  const SCROLL_W = 360;
  const track = el("div", "", scroller, { position: "relative", width: SCROLL_W + "px", height: "26px", borderRadius: "13px", background: "var(--well)",
    boxShadow: "inset 0 0 0 1px var(--panel-edge), inset 0 2px 5px rgba(0,0,0,0.5)", cursor: "pointer", touchAction: "none" });
  const thumb = el("div", "", track, { position: "absolute", top: "3px", height: "20px", borderRadius: "10px", cursor: "grab",
    background: "linear-gradient(180deg, var(--gold-hi), var(--gold) 55%, var(--gold-mid))", boxShadow: "0 0 8px rgba(227,178,96,0.45)" });
  const trackText = el("div", "", track, { position: "absolute", inset: "0", display: "grid", placeItems: "center", fontSize: "15px", color: "var(--text-dim)", pointerEvents: "none" });
  const where = el("div", "", scroller, { fontSize: "17px", color: "var(--text-dim)", whiteSpace: "nowrap", minWidth: "116px" });
  const scroll = { total: 0, offset: -1, preview: -1, dragging: false, grab: 0, asked: -1e9, wheel: 0, wheelTimer: null };
  const stageScale = () => document.getElementById("stage").getBoundingClientRect().width / 1440;
  const maxOffset = () => Math.max(0, scroll.total - STRIPS);
  const thumbW = () => (scroll.total > STRIPS ? Math.max(34, SCROLL_W * STRIPS / scroll.total) : SCROLL_W);
  const xOf = (off) => (maxOffset() > 0 ? (SCROLL_W - thumbW()) * off / maxOffset() : 0);
  const offAt = (x) => Math.round(clamp(x / Math.max(1, SCROLL_W - thumbW()), 0, 1) * maxOffset());
  const rangeText = (off) => `${off + 1}–${Math.min(scroll.total, off + STRIPS)} of ${scroll.total}`;
  function drawScroll() {
    const shown = scroll.preview >= 0 ? scroll.preview : scroll.offset;
    thumb.style.display = scroll.total ? "block" : "none";
    thumb.style.width = thumbW() + "px";
    thumb.style.left = xOf(Math.max(0, shown)) + "px";
    thumb.style.opacity = shown >= 0 ? "1" : "0.45";
    where.textContent = !scroll.total ? "" : shown >= 0 ? rangeText(shown) : `${scroll.total} channels`;
    const names = (app.bridge && app.bridge.channelNames) || [];
    thumb.title = shown >= 0 ? names.slice(shown, shown + STRIPS).join(", ") : "";
    track.style.cursor = maxOffset() > 0 ? "pointer" : "default";
  }
  const readChannels = () => { scroll.asked = performance.now(); app.command("readChannels"); };
  const goTo = (off) => {
    if (scroll.offset < 0) { readChannels(); return; }   // the order is out of date: read it again first
    scroll.preview = off;
    drawScroll();
    app.command("bankTo", off);
  };
  thumb.addEventListener("pointerdown", (e) => {
    if (maxOffset() === 0 || scroll.offset < 0) return;
    e.stopPropagation();
    scroll.dragging = true;
    scroll.grab = e.clientX / stageScale() - xOf(Math.max(0, scroll.offset));
    thumb.setPointerCapture(e.pointerId);
    thumb.style.cursor = "grabbing";
  });
  thumb.addEventListener("pointermove", (e) => {
    if (!scroll.dragging) return;
    scroll.preview = offAt(e.clientX / stageScale() - scroll.grab);
    drawScroll();
  });
  const endDrag = () => {
    if (!scroll.dragging) return;
    scroll.dragging = false;
    thumb.style.cursor = "grab";
    if (scroll.preview >= 0 && scroll.preview !== scroll.offset) goTo(scroll.preview); else { scroll.preview = -1; drawScroll(); }
  };
  thumb.addEventListener("pointerup", endDrag);
  thumb.addEventListener("pointercancel", endDrag);
  track.addEventListener("pointerdown", (e) => {   // a click on the bar: that part of the project, centred
    if (scroll.total === 0 || scroll.offset < 0) { readChannels(); return; }
    if (maxOffset() === 0) return;
    const x = (e.clientX - track.getBoundingClientRect().left) / stageScale();
    goTo(offAt(x - thumbW() / 2));
  });
  track.addEventListener("wheel", (e) => {
    e.preventDefault();
    if (maxOffset() === 0 || scroll.offset < 0) return;
    scroll.wheel += (e.deltaY > 0 || e.deltaX > 0) ? 1 : -1;
    scroll.preview = clamp(scroll.offset + scroll.wheel, 0, maxOffset());
    drawScroll();
    clearTimeout(scroll.wheelTimer);
    scroll.wheelTimer = setTimeout(() => { scroll.wheel = 0; if (scroll.preview !== scroll.offset) goTo(scroll.preview); }, 250);
  }, { passive: false });
  function updateScroll(b) {
    const connected = b && b.link === "connected";
    scroller.style.opacity = connected ? "1" : "0.4";
    scroll.total = connected && b.channelNames ? b.channelNames.length : 0;
    const off = connected && b.viewOffset !== undefined ? b.viewOffset : -1;
    if (off !== scroll.offset) { scroll.offset = off; if (!scroll.dragging) scroll.preview = -1; }
    if (scroll.preview === scroll.offset) scroll.preview = -1;
    // Never read yet: while the Tracks page is open, Mixology reads every fader once (the bank steps through the pages
    // and comes back), which also learns the channels' order. A track added or moved later: read again on first touch.
    const shownNow = root.classList.contains("shown");
    const canRead = connected && !b.scanning && (b.slots || []).some((s) => s && s.present);
    trackText.textContent = !connected || scroll.total ? "" : b.scanning ? "Reading your channels…" : "Click to read your channels";
    if (canRead && shownNow && scroll.total === 0 && performance.now() - scroll.asked > 60000) readChannels();
    drawScroll();
  }
  const restoreBtn = el("button", "small-btn", bar, { whiteSpace: "nowrap" });
  restoreBtn.title = "Put every channel Mixology changed back the way it was before Mixology's first move";
  restoreBtn.addEventListener("click", () => app.command("restoreAll"));
  const hint = el("div", "", bar, { flex: "1", minWidth: "0", textAlign: "right", color: "var(--text-dim)", fontSize: "17px", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" });

  const W = 100, GAP = 6, X0 = 10, TOP = 66;
  const FADER_TOP = 160, FADER_H = 430;
  const strips = [];

  for (let i = 0; i < STRIPS; i++) {
    const s = el("div", "card", panel, box(X0 + i * (W + GAP), TOP, W, 764));
    const colourBar = el("div", "", s, { position: "absolute", left: "10px", right: "10px", top: "5px", height: "3px", borderRadius: "2px" });
    const name = el("div", "", s, { position: "absolute", left: "4px", right: "4px", top: "12px", textAlign: "center", fontSize: "20px", fontWeight: "500", whiteSpace: "nowrap", overflow: "hidden", textOverflow: "ellipsis" });
    const lock = el("button", "", s, { position: "absolute", right: "4px", top: "44px", width: "28px", height: "28px", border: "0", padding: "3px", borderRadius: "7px", background: "transparent", cursor: "pointer", color: "var(--text-faint)" });
    const undo = el("button", "", s, { position: "absolute", right: "4px", top: "76px", width: "28px", height: "24px", border: "0", padding: "0", borderRadius: "7px", background: "transparent", cursor: "pointer", color: "var(--info)", fontSize: "20px", lineHeight: "24px", display: "none" });
    undo.textContent = "↺";
    undo.title = "Restore this channel: its fader, pan and Assist EQ, how they were before Mixology's first change";
    const badge = el("div", "", s, { position: "absolute", left: "5px", top: "46px", width: "20px", height: "20px", borderRadius: "5px", fontSize: "14px", fontWeight: "700", textAlign: "center", lineHeight: "20px", display: "none" });
    const linkDot = el("div", "", s, { position: "absolute", left: "4px", top: "72px", width: "22px", height: "22px", display: "none" });
    const ic = el("div", "", s, { position: "absolute", left: "26px", top: "46px", width: "48px", height: "48px" });
    // M6: Voxology on this channel (the suite link): a small gold tag under the role icon.
    const vox = el("div", "", s, { position: "absolute", left: "29px", width: "42px", top: "91px", height: "14px", borderRadius: "4px", fontSize: "11px", fontWeight: "700",
      letterSpacing: "0.6px", lineHeight: "14px", textAlign: "center", background: "var(--gold)", color: "#1a1300", display: "none", cursor: "default" });
    const sBtn = el("button", "small-btn", s, { position: "absolute", left: "8px", top: "106px", width: "40px", padding: "0", fontSize: "20px" });
    sBtn.textContent = "S";
    const mBtn = el("button", "small-btn", s, { position: "absolute", left: "52px", top: "106px", width: "40px", padding: "0", fontSize: "20px" });
    mBtn.textContent = "M";

    // Fader: slot with meters inside, cap on top, scale on the right.
    const zone = el("div", "", s, box(6, FADER_TOP, 88, FADER_H));
    const slot = el("div", "", zone, { position: "absolute", left: "22px", top: "0", width: "26px", height: FADER_H + "px", borderRadius: "6px", background: "var(--well)", boxShadow: "inset 0 0 0 1px var(--panel-edge), inset 0 2px 6px rgba(0,0,0,0.6)" });
    const mL = new Meter(slot, 4, 4, 8, FADER_H - 8, [-60, 6]);
    const mR = new Meter(slot, 14, 4, 8, FADER_H - 8, [-60, 6]);
    const scale = el("div", "", zone, { position: "absolute", left: "54px", top: "0", width: "34px", height: FADER_H + "px", fontSize: "14px", color: "var(--text-dim)" });
    const cap = el("div", "", zone, {
      position: "absolute", left: "13px", width: "44px", height: "34px", borderRadius: "5px", cursor: "ns-resize", touchAction: "none",
      background: "linear-gradient(180deg, #f4f6f9 0%, #c9ced6 45%, #9aa2ad 50%, #d9dde3 56%, #eef1f4 100%)",
      boxShadow: "0 4px 8px rgba(0,0,0,0.6), inset 0 1px 0 #fff", border: "1px solid #6b737e",
    });
    el("div", "", cap, { position: "absolute", left: "4px", right: "4px", top: "16px", height: "2px", background: "#30343a" });

    const val = el("div", "", s, { position: "absolute", left: "0", right: "0", top: "602px", textAlign: "center", fontSize: "26px", fontWeight: "600", cursor: "text" });
    val.title = "Click to type an exact level";
    const pan = new Knob(s, { x: 0, y: 640, w: 100, size: 66, min: -100, max: 100, def: 0, bipolar: true, step: 1, format: (v) => fmt.pan(v), valueGap: 2 });

    const strip = { s, colourBar, name, lock, undo, badge, linkDot, ic, vox, sBtn, mBtn, zone, cap, val, pan, mL, mR, scale, index: i, data: null };
    strips.push(strip);

    // The channel this strip shows right now; commands carry it so a moved bank can't redirect them.
    const ref = (value) => ({ slot: i, hash: strip.data ? strip.data.hash : 0, value });
    const live = () => strip.data && strip.data.present && app.bridge && app.bridge.link === "connected";
    const send = (name, value) => (live() ? app.command(name, ref(value)) : Promise.resolve());
    const guarded = () => {
      if (!live()) return false;
      if (strip.data.protected) { app.toast(`'${strip.data.title}' is protected. Click its padlock to let Mixology move it.`); return false; }
      if (strip.data.write) { app.toast(`'${strip.data.title}' is writing automation (W is on). Switch W off first.`); return false; }
      return true;
    };

    lock.addEventListener("click", () => { if (live()) send("protect", !strip.data.protected); });

    // The role icon: click to set what this track is (or leave it out of Analyze Mix).
    ic.style.cursor = "pointer";
    ic.addEventListener("click", () => {
      if (!strip.data || !strip.data.present) return;
      const d = strip.data, current = d.role || guessRole(d.title);
      const items = ROLE_ORDER.map((k) => ({ label: ROLES[k].label, value: k, icon: ROLES[k].icon, colour: ROLES[k].colour, sel: d.roleSet && k === current }));
      items.push({ divider: true });
      items.push({ label: d.roleSet ? "Automatic (from name and sound)" : "Automatic (now: " + ROLES[current].label + ")", value: "auto", icon: "wand", sel: !d.roleSet });
      items.push({ divider: true });
      items.push({ label: d.musicBus ? "Music group (the instruments play through it)" : "Music group: the instruments play through this channel", value: "bus", icon: "glue", colour: "var(--gold)", sel: !!d.musicBus });
      items.push({ label: d.excluded ? "Include in Analyze Mix" : "Leave out of Analyze Mix", value: "exclude", icon: d.excluded ? "check" : "close" });
      popupMenu(ic, items, (v) => {
        if (v === "exclude") app.command("setExcluded", { hash: d.hash, value: !d.excluded });
        else if (v === "bus") app.command("setMusicBus", { hash: d.hash, value: !d.musicBus });
        else app.command("setRole", { hash: d.hash, value: v === "auto" ? "" : v });
      }, 380);
    });
    undo.addEventListener("click", () => send("restore"));
    sBtn.addEventListener("click", () => { if (guarded()) send("solo", !strip.data.solo); });
    mBtn.addEventListener("click", () => { if (guarded()) send("mute", !strip.data.mute); });

    // Fader drag: exact positions, about 28 per second, latest wins. Cubase's readback is ignored while
    // dragging and shortly after, so the cap never jumps back under the mouse.
    let dragging = false, holdUntil = 0, lastSent = 0, pending = null, startY = 0, startPos = 0;
    const capTop = (pos) => (1 - pos) * (FADER_H - 34);
    cap.addEventListener("pointerdown", (e) => {
      if (!guarded() || !strip.data.hasVolume) return;
      dragging = true;
      startY = e.clientY;
      startPos = strip.data.volume;
      cap.setPointerCapture(e.pointerId);
    });
    cap.addEventListener("pointermove", (e) => {
      if (!dragging) return;
      const k = document.getElementById("stage").getBoundingClientRect().width / 1440;
      const pos = clamp(startPos - (e.clientY - startY) / k / (FADER_H - 34) * (e.shiftKey ? 0.25 : 1), 0, 1);
      cap.style.top = capTop(pos) + "px";
      pending = pos;
      const now = performance.now();
      if (now - lastSent > 35) { lastSent = now; send("fader", pos); pending = null; }
    });
    const end = () => {
      if (!dragging) return;
      dragging = false;
      holdUntil = performance.now() + 400;
      if (pending !== null) send("fader", pending);
      pending = null;
      setTimeout(() => send("faderEnd"), 120);
    };
    cap.addEventListener("pointerup", end);
    cap.addEventListener("pointercancel", end);
    cap.addEventListener("dblclick", () => { if (guarded()) send("setDb", 0); });
    zone.addEventListener("wheel", (e) => {
      e.preventDefault();
      if (!guarded()) return;
      send("nudge", (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.1 : 0.5));
    }, { passive: false });
    strip.isDragging = () => dragging || performance.now() < holdUntil;
    strip.capTop = capTop;

    // Click the level to type an exact value ("-6", "0", "-inf").
    val.addEventListener("click", () => {
      if (!guarded() || val.querySelector("input")) return;
      // The box takes the number's place (same spot, on top of everything), not a new line under it.
      val.textContent = "";
      const input = el("input", "num", val, {
        position: "absolute", left: "8px", top: "-4px", width: "84px", height: "38px", zIndex: "5",
        fontSize: "22px", textAlign: "center", padding: "0 4px", boxSizing: "border-box",
      });
      input.value = strip.data.volumeDb === null || strip.data.volumeDb === undefined ? "" : (Math.round(strip.data.volumeDb * 10) / 10).toString();
      input.focus();
      input.select();
      let done = false;
      const finish = (apply) => {
        if (done) return;
        done = true;   // removing the field blurs it: don't finish twice
        const text = input.value.trim().replace(",", ".").replace("−", "-");
        input.remove();
        render(app.bridge);
        if (!apply || text === "") return;
        if (/^-?\s*(inf|oo|∞)/i.test(text)) return send("minusInf");
        const db = parseFloat(text);
        if (isFinite(db)) send("setDb", db);
      };
      input.addEventListener("keydown", (e) => { e.stopPropagation(); if (e.key === "Enter") finish(true); if (e.key === "Escape") finish(false); });
      input.addEventListener("blur", () => finish(true));
    });

    pan.o.onChange = (v) => {
      if (strip.data && strip.data.stereoPanner) {
        pan.set(0);
        pan.valueEl.textContent = "Stereo";
        app.toast(`'${strip.data.title}' uses Cubase's Stereo Combined Panner: Cubase only tells Mixology its left edge. Pan it in Cubase.`);
        return;
      }
      if (strip.data && strip.data.noPan) {
        pan.set(0);
        pan.valueEl.textContent = "No pan";
        app.toast(`'${strip.data.title}' doesn't pan in Cubase: its output is mono (a mono group or output), so it plays in the middle.`);
        return;
      }
      if (!guarded()) { pan.set(strip.data && strip.data.present ? Math.round((strip.data.pan - 0.5) * 200) : 0); return; }
      send("pan", (v + 100) / 200);
    };
    pan.o.onDragStart = () => { strip.panDragging = true; };
    pan.o.onDragEnd = () => { strip.panDragging = false; strip.panHoldUntil = performance.now() + 400; };
  }

  // Master strip: Mixology's own input (it sits on the Stereo Out)
  const ms = el("div", "card", panel, box(X0 + STRIPS * (W + GAP) + 4, TOP, 116, 764));
  const mn = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "12px", textAlign: "center", fontSize: "20px", fontWeight: "500" });
  mn.textContent = "Master";
  const msub = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "40px", textAlign: "center", fontSize: "14px", color: "var(--text-dim)", lineHeight: "1.15" });
  msub.innerHTML = "what Mixology<br>hears";
  const mmL = new Meter(ms, 30, 106, 22, 484, [-60, 6]);
  const mmR = new Meter(ms, 62, 106, 22, 484, [-60, 6]);
  const mval = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "602px", textAlign: "center", fontSize: "26px", fontWeight: "600" });
  const mlufs = el("div", "", ms, { position: "absolute", left: "0", right: "0", top: "650px", textAlign: "center", fontSize: "18px", color: "var(--text-dim)", lineHeight: "1.3" });
  let peakHold = -100, peakTime = 0;

  // Not-connected cover
  const cover = el("div", "", panel, { position: "absolute", left: "10px", top: "66px", width: "1266px", height: "764px", borderRadius: "12px", display: "none", placeItems: "center", textAlign: "center", background: "rgba(0,0,0,0.0)" });
  const coverBox = el("div", "card", cover, { position: "relative", width: "640px", padding: "30px 34px", fontSize: "20px", lineHeight: "1.45" });
  const coverText = el("div", "", coverBox);
  const coverBtn = el("button", "small-btn", coverBox, { marginTop: "18px" });
  coverBtn.textContent = "Open the Cubase Bridge setup";
  coverBtn.addEventListener("click", () => app.openSettings("bridge"));

  // What Voxology does on a channel, in words (the tag's tooltip).
  function voxTip(v) {
    if (v.beat) return "Voxology in Beat mode: it listens to this track for the song's key. Nothing changes here.";
    if (!v.on) return "Voxology is on this vocal, bypassed right now.";
    const does = [];
    if (v.comp) does.push(v.compDb >= 0.5 ? `compresses (about ${Math.round(v.compDb)} dB)` : "compresses");
    if (v.deEsser) does.push("de-esses");
    if (v.eq) does.push("shapes the tone");
    if (v.pitch) does.push("tunes");
    if (v.doubler) does.push("doubles");
    if (v.reverb) does.push("adds its reverb");
    if (v.delay) does.push("adds its delay");
    const list = does.length > 1 ? does.slice(0, -1).join(", ") + " and " + does[does.length - 1] : does[0] || "processes it";
    const left = [v.comp && "compressor", v.reverb && "reverb", v.delay && "delay"].filter(Boolean);
    return `Voxology is on this vocal${v.style ? " (" + v.style + ")" : ""}: it ${list}.`
      + (left.length ? ` Auto Mix adds no second ${left.join(" or ")}.` : "");
  }

  const LINK_TIPS = {
    matched: "Mixology Link is on this track: the meters show its level after the fader",
    ambiguous: "Mixology Link found, but another track has the same name: rename one so Mixology can pair it",
  };

  function render(b) {
    const connected = b && b.link === "connected";
    const slots = (b && b.slots) || [];
    status.innerHTML = connected
      ? `<span class="status-dot ok"></span>Cubase connected <span class="dim">(Bridge v${b.script})</span>`
      : `<span class="status-dot ${b && b.link === "waiting" ? "warn" : "bad"}"></span>${b && b.link === "waiting" ? "Waiting for Cubase..." : b && b.link === "lost" ? "Lost contact with Cubase" : "Not connected to Cubase"}`;
    updateScroll(b);
    const changed = (b && b.changedCount) || 0;
    restoreBtn.textContent = changed ? `↺ Restore (${changed})` : "↺ Restore";
    const cf = (b && b.changedFaders) || 0, ce = (b && b.changedEq) || 0;
    restoreBtn.title = changed
      ? `Back to before Mixology's first change: ${cf} channel${cf === 1 ? "" : "s"} (faders, pans), ${ce} Assist EQ${ce === 1 ? "" : "s"}. What you changed yourself since stays.`
      : "Put every channel Mixology changed back the way it was before Mixology's first move";
    restoreBtn.style.opacity = connected && changed ? "1" : "0.45";
    restoreBtn.style.color = changed ? "var(--info)" : "";
    const named = slots.filter((d) => d && d.present).length;
    const paired = (b && b.linksPaired) || 0;
    const setupIssue = connected && b.setup && b.setup.length ? b.setup[0] : null;
    hint.style.cursor = setupIssue ? "pointer" : "";
    hint.onclick = setupIssue ? () => openSuggestion(app, setupIssue) : null;
    hint.title = setupIssue ? setupIssue.text : "";
    hint.innerHTML = setupIssue ? `<span style="color:var(--amber)">⚠ ${esc(setupIssue.title)}</span> <span style="text-decoration:underline">What to do</span>`
      : !connected ? ""
      : named === 0 ? `<span style="color:var(--amber)">●</span> Connected, but Cubase hasn't reported any channels yet. Settings › Cubase Bridge › <b>Refresh</b>, then <b>Copy log</b>.`
      : paired === 0 ? "Drag, scroll or click a level to move it. Meters: add Mixology Link to your tracks."
      : `Drag, scroll or click a level to move it. <span style="color:var(--green)">${paired}</span> with Mixology Link.`;
    cover.style.display = connected ? "none" : "grid";
    const notCubase = app.daw !== "cubase";
    coverBtn.textContent = notCubase ? "Settings: your DAW" : "Open the Cubase Bridge setup";
    coverText.innerHTML = notCubase
      ? `<b>This page moves Cubase's faders.</b><br>You work in ${esc(dawName(app.daw))}: Mixology can't move its faders yet, so set levels with ${esc(say(app.daw, "mixer"))}. Analyze Mix, the Mix Assistant, Mixology Link and every tool's EQ, compression and space work as usual; Auto Mix lists the fader moves for you to make.`
      : b && b.portProblem
      ? `<b>Mixology can't reach Cubase yet.</b><br>${b.portProblem}`
      : `<b>Waiting for Cubase.</b><br>Mixology talks to the real MixConsole through a small Cubase MIDI Remote script and two virtual MIDI ports. It takes about five minutes to set up once.`;

    const marks = (b && b.marks) || [];
    const light = document.documentElement.dataset.theme === "light";
    strips.forEach((st, i) => {
      const d = slots[i];
      st.data = d;
      const present = connected && d && d.present;
      st.s.style.opacity = present ? "1" : "0.35";
      const role = present ? (d.role || guessRole(d.title)) : "other";
      const R = ROLES[role];
      st.name.textContent = present ? d.title : "—";
      st.name.title = present ? d.title + (d.duplicate ? "\nAnother channel in view has the same name." : "") : "";
      st.name.style.color = present && d.duplicate ? "var(--amber)" : "";
      st.colourBar.style.background = present && d.colour ? d.colour : "transparent";
      const bus = present && d.musicBus;
      const iconKey = bus ? "glue" : roleIcon(role, present ? d.title : "");
      if (st.role !== iconKey) { st.ic.innerHTML = icon(iconKey); st.role = iconKey; }
      const icColour = bus ? "#ffb43c" : R.colour;
      st.ic.style.color = icColour;
      st.ic.style.filter = present && d.excluded ? "grayscale(1)" : `drop-shadow(0 0 6px ${icColour})`;
      st.ic.style.opacity = present && d.excluded ? "0.4" : "1";
      const vx = present && d.voxology ? d.voxology : null;
      st.ic.title = !present ? "" : bus ? "Music group: the instruments play through it (Mixology moves the music on this fader). Click to change"
        : `${R.label}${d.roleSet ? " (set by you)" : vx && !vx.beat ? " (Voxology is on it)" : ""}${d.excluded ? ". Left out of Analyze Mix" : ""}. Click to change`;
      st.vox.style.display = vx ? "block" : "none";
      if (vx) {
        st.vox.textContent = vx.beat ? "BEAT" : "VOX";
        st.vox.style.opacity = vx.beat || vx.on ? "1" : "0.5";
        st.vox.title = voxTip(vx);
      }

      // Padlock: closed = protected (Mixology never moves it).
      const prot = present && d.protected;
      if (st.lockKey !== prot) { st.lockKey = prot; st.lock.innerHTML = icon(prot ? "lock" : "unlock", 'stroke-width="2.2"'); }
      st.lock.style.color = prot ? "var(--gold)" : "var(--text-faint)";
      st.lock.title = prot ? "Protected: Mixology won't move this channel. Click to allow." : "Click to protect: Mixology will never move this channel";
      st.s.style.boxShadow = prot ? "inset 0 0 0 1.5px rgba(227,178,96,0.55)" : "";
      st.undo.style.display = present && d.changed && !prot ? "block" : "none";

      // Automation: W blocks moves, R warns (playback may move the fader back).
      st.badge.style.display = present && (d.write || d.read) ? "block" : "none";
      if (present && d.write) {
        st.badge.textContent = "W";
        Object.assign(st.badge.style, { background: "#c80000", color: "#fff", boxShadow: "0 0 8px rgba(255,30,30,0.7)" });
        st.badge.title = "Writing automation: Mixology won't move this channel";
      } else if (present && d.read) {
        st.badge.textContent = "R";
        Object.assign(st.badge.style, { background: "#2fae4f", color: "#fff", boxShadow: "0 0 8px rgba(57,226,107,0.5)" });
        st.badge.title = "Reading automation: if this channel has volume automation, playback moves the fader back";
      }

      const ls = present ? d.linkStatus : "none";
      st.linkDot.style.display = ls === "matched" || ls === "ambiguous" ? "block" : "none";
      if (st.linkKey !== ls) {
        st.linkKey = ls;
        st.linkDot.innerHTML = icon("link", 'stroke-width="2.4"');
        st.linkDot.style.color = ls === "matched" ? "var(--green)" : "var(--amber)";
        st.linkDot.title = LINK_TIPS[ls] || "";
      }
      if (ls !== "matched") { st.mL.set(-100); st.mR.set(-100); }

      st.sBtn.style.background = present && d.solo ? "linear-gradient(180deg,#ffe36b,#d8a400)" : "";
      st.sBtn.style.color = present && d.solo ? "#1a1300" : "";
      st.mBtn.classList.toggle("red", !!(present && d.mute));
      const blocked = present && (prot || d.write);
      for (const c of [st.sBtn, st.mBtn, st.cap]) c.style.cursor = blocked ? "not-allowed" : "";
      st.cap.style.cursor = blocked ? "not-allowed" : "ns-resize";
      st.cap.style.filter = blocked ? "grayscale(0.6) brightness(0.8)" : "";
      if (!st.isDragging()) st.cap.style.top = st.capTop(present && d.hasVolume ? d.volume : 0.78) + "px";
      if (!st.val.querySelector("input"))
        st.val.textContent = present && d.hasVolume ? (d.volumeText.match(/oo|inf|∞/i) ? MINUS_INF : formatFaderText(d.volumeText)) : "";
      st.val.style.color = light ? `color-mix(in srgb, ${R.colour} 62%, #000)` : R.colour;
      st.val.style.cursor = present && !blocked ? "text" : "default";
      const panBusy = st.panDragging || performance.now() < (st.panHoldUntil || 0) || document.activeElement === st.pan.root;
      const panInert = present && (d.stereoPanner || d.noPan);
      if (present && !panBusy) st.pan.set(panInert ? 0 : Math.round((d.pan - 0.5) * 200));
      if (present && d.stereoPanner) st.pan.valueEl.textContent = "Stereo";
      if (present && d.noPan) st.pan.valueEl.textContent = "No pan";
      st.pan.root.title = !present ? ""
        : d.noPan ? "Cubase didn't follow a pan move on this channel: its output is mono (a mono group or output), so it plays in the middle whatever the pan says. Mixology leaves it alone."
        : d.stereoPanner ? "Cubase's Stereo Combined Panner: Cubase only tells Mixology its left edge (\"L100\" at full width). Pan this track in Cubase."
        : b && b.mixMono ? `Your mix plays in mono at the ${say(app.daw, "master")} channel (Analyze Mix heard left = right): pans change nothing until the output is stereo.`
        : "";
      st.pan.root.style.opacity = present && d.hasPan !== false && !panInert && !(b && b.mixMono) ? "1" : "0.5";
      if (st.marksKey !== JSON.stringify(marks)) {
        st.marksKey = JSON.stringify(marks);
        st.scale.innerHTML = "";
        let lastTop = -100;
        for (const m of marks) {
          if (m.db > 0) continue;
          const top = st.capTop(m.pos) + 8;
          if (top - lastTop < 18) continue;   // too close to the mark above: never draw two on top of each other
          lastTop = top;
          const lab = el("div", "", st.scale, { position: "absolute", left: "2px", top: top + "px" });
          lab.textContent = m.db === 0 ? "0" : "−" + Math.abs(m.db);
        }
      }
    });
  }
  const MINUS_INF = "−∞";
  function formatFaderText(t) {
    const v = parseFloat(String(t).replace(",", ".").replace("−", "-"));
    if (!isFinite(v)) return t;
    const r = Math.round(v * 10) / 10;
    return (r > 0 ? "+" : r < 0 ? "−" : "") + Math.abs(r).toFixed(1);
  }

  app.on("bridge", render);
  app.on("theme", () => render(app.bridge));
  render(app.bridge);

  return {
    onShow() { render(app.bridge); },   // the scroll bar may want to read the channels now
    onFrame(f) {
      // Per-channel meters come from Mixology Link on each track (null = no Link paired).
      if (f.trackMeters) strips.forEach((st, i) => { const m = f.trackMeters[i]; if (m) { st.mL.set(m[0]); st.mR.set(m[1]); } });
      mmL.set(f.peakL ?? -100);
      mmR.set(f.peakR ?? -100);
      const p = Math.max(f.peakL ?? -100, f.peakR ?? -100);
      const now = performance.now();
      if (p >= peakHold || now - peakTime > 2000) { peakHold = p; peakTime = now; }
      mval.textContent = peakHold > -99 ? fmt.signed(peakHold, 1) : "−∞";
      mval.style.color = peakHold > -1 ? "var(--red)" : "var(--text)";
      mlufs.innerHTML = `${fmt.lufs(f.shortTerm ?? -100)}<br>LUFS S`;
    },
  };
}
