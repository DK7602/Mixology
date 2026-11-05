// AUTO on a processing page (EQ, Dynamics, Space, Tone, Stereo): one button in the page's toolbar, drawn like its ON
// button, and its line in the page's status / notes line with an Undo text button. It works on the track picked on the
// page (never the Full Mix: Auto Mix on the Overview does the whole mix), with the same rules as Polish and Auto Mix,
// and changes only that page's controls. Pressed, it applies straight away (one History step), or first listens to the
// track while you play a part where it plays, when Analyze Mix didn't hear it (pressed again: it stops listening).

import { el, icon } from "./ui.js";

export function autoButton(app, page, parent, x, y, opts = {}) {
  const proc = app.proc;
  const b = el("button", "power-btn auto-btn", parent, { left: x + "px", top: y + "px" });
  b.innerHTML = icon("wand", 'stroke-width="2.4"');
  el("span", "", b).textContent = "AUTO";
  let last = null;   // the newest AUTO result for this page: { seq, state, page, hash, name, line, step, heard, need, playing }
  let seqSeen = -1;
  const listeners = new Set();
  const emit = () => { draw(); for (const f of listeners) f(); };

  const target = () => proc.targets.find((t) => t.key === proc.key);
  const hashNow = () => (proc.key === "mix" ? 0 : Number((target() || {}).hash) || 0);
  const onTrack = () => proc.key !== "mix" && proc.ok && hashNow() !== 0;

  // A result arrives (the command's answer, or the bridge state while it listens): the line shows it; a finished one
  // is said in full once (the page's line is short).
  function take(st) {
    if (!st) return;
    if (st.page !== page) {
      // AUTO pressed on another page while this one listened: this listen has stopped (the hub listens to one at a time).
      if (last && last.state === "listening" && st.seq > seqSeen) { last = null; emit(); }
      return;
    }
    if (st.seq === seqSeen && last && st.heard === last.heard && st.playing === last.playing && st.line === last.line) return;
    const fresh = st.seq !== seqSeen;
    seqSeen = st.seq;
    last = Object.assign({}, st);
    if (st.state === "idle") last = null;
    if (fresh && last && st.state !== "listening" && st.line) app.toast(st.line);
    emit();
  }
  app.on("bridge", (s) => { if (s && s.auto) take(s.auto); });
  // You move a control on this track: the AUTO line steps aside (the History still has its step).
  proc.on((why) => {
    if (why === "touch" && last && last.state !== "listening") { last = null; emit(); }
    if (why === "target" || why === "targets") emit();
  });

  b.addEventListener("click", async () => {
    const r = await app.native.bridgeCommand("auto", { page, hash: hashNow(), key: proc.key });
    if (r && r.auto) take(r.auto);
    else if (r && r.message) app.toast(r.message);
  });

  let drawn = "";
  function draw() {
    const listening = !!(last && last.state === "listening" && Number(last.hash) === hashNow());
    const hidden = opts.hideOnMix && proc.key === "mix";
    const key = [listening, onTrack(), hidden, proc.name].join("|");
    if (key === drawn) return;
    drawn = key;
    b.style.display = hidden ? "none" : "";
    b.classList.toggle("on", listening);
    b.classList.toggle("listening", listening);
    b.style.opacity = onTrack() ? "1" : "0.45";
    b.title = !onTrack() ? "Pick a track first: AUTO works on one track at a time (Auto Mix on the Overview does the whole mix)"
      : listening ? `AUTO is listening to '${proc.name}': press again to stop`
      : `AUTO: Mixology sets this page for '${proc.name}' (the same rules as Polish), straight away. One step in the History: Undo takes it back.`;
  }
  draw();

  async function undo(step) {
    const r = await app.native.bridgeCommand("autoUndo", { value: step });
    if (r && r.ok === false && r.message) app.toast(r.message);
    else if (r && r.info) app.toast(r.info);
    if (r && r.auto) take(r.auto);
  }

  return {
    el: b,
    onChange(f) { listeners.add(f); },
    /** The AUTO line for the track the page shows now, or null. */
    message() { return last && (Number(last.hash) || 0) === hashNow() ? last : null; },
    /** Shows the AUTO line in a page's line (with Undo after it); false when there is none (the page writes its own
        text then). opts.wrap: the line may wrap (a notes block) instead of ending in "...". */
    show(line, { wrap = false } = {}) {
      const m = this.message();
      if (!m) {
        if (line.dataset.auto) {
          delete line.dataset.auto;
          line.style.display = line.dataset.autoDisplay || "";
          line.title = "";
          line.textContent = "";
        }
        return false;
      }
      const key = [m.seq, m.state, m.line, m.heard].join("|");
      if (line.dataset.auto === key) return true;
      if (!line.dataset.auto) line.dataset.autoDisplay = line.style.display;
      line.dataset.auto = key;
      line.textContent = "";
      line.title = m.line;
      if (!wrap) Object.assign(line.style, { display: "flex", alignItems: "baseline", gap: "10px" });
      const text = el("span", "", line, wrap ? {} : { overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap", minWidth: "0" });
      text.textContent = m.line;
      if (m.state === "applied" && m.step) {
        const u = el("button", "text-btn", line);
        u.textContent = "Undo";
        u.title = "Takes back exactly what this AUTO changed (it is one step in the History)";
        u.addEventListener("click", () => undo(m.step));
      }
      return true;
    },
  };
}
