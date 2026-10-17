// Browser preview only (inside the plug-in JUCE provides window.__JUCE__ and this does nothing):
// fakes a Link on a "Lead Vox" track with Mixology open.
(function () {
  if (typeof window.__JUCE__ !== "undefined") return;
  let trim = 0, t = 0, procOn = true;
  let assist = { lowCut: 90, mudHz: 300, mudDb: 0, pocketHz: 2200, pocketDb: -2.5 };
  const send = (id, obj) => setTimeout(() => window.__JUCE__.backend.emitByBackend(id, JSON.stringify(obj)), 0);
  window.__JUCE__ = {
    initialisationData: {
      __juce__platform: [], __juce__registeredGlobalEventIds: [], __juce__sliders: [], __juce__toggles: [], __juce__comboBoxes: [],
      __juce__functions: ["getLinkInfo", "setTrim", "trimGesture", "clearAssist", "setProcessing"],
    },
    postMessage(message) {
      const { eventId, payload } = JSON.parse(message);
      if (eventId !== "__juce__invoke") return;
      let result = true;
      if (payload.name === "getLinkInfo") result = { theme: new URLSearchParams(location.search).get("theme") || "dark", version: "preview", trim, daw: location.hash.slice(1) || "cubase" };   // #fl, #logic...
      if (payload.name === "setTrim") trim = payload.params[0];
      if (payload.name === "setProcessing") procOn = !!payload.params[0];
      if (payload.name === "clearAssist") assist = { lowCut: 0, mudHz: 300, mudDb: 0, pocketHz: 2500, pocketDb: 0 };
      send("__juce__complete", { promiseId: payload.resultId, result });
    },
  };
  setInterval(() => {
    t += 1 / 30;
    const env = 0.75 + 0.2 * Math.sin(t * 3);
    const lvl = -14 + trim + 10 * env + (Math.random() - 0.5) * 2;
    window.__JUCE__.backend && window.__JUCE__.backend.emitByBackend("linkFrame", JSON.stringify({
      peakL: +lvl.toFixed(1), peakR: +(lvl - 1).toFixed(1), momentary: +(lvl - 9).toFixed(1), shortTerm: +(-17 + trim).toFixed(1),
      trim, name: "Lead Vox", colour: "#4fd66b", mixology: true, paired: "matched", shared: true, note: "", channels: 2, assist,
      proc: { on: procOn, modules: ["eq", "comp", "balance", "space"], running: procOn ? ["eq", "comp", "balance", "space"] : [], latencyMs: 0,
        space: { type: "Delay", fx: false, duck: ["Lead Vox"], wet: -18 } },
    }));
  }, 1000 / 30);
})();
