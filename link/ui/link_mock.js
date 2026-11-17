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
      __juce__functions: ["getLinkInfo", "setTrim", "trimGesture", "clearAssist", "setProcessing", "getRack", "rackRequest", "rackAnswer", "rackWindow", "getRackTheme", "setRackTheme"],
    },
    postMessage(message) {
      const { eventId, payload } = JSON.parse(message);
      if (eventId !== "__juce__invoke") return;
      let result = true;
      if (payload.name === "getLinkInfo") result = { theme: new URLSearchParams(location.search).get("theme") || "dark", version: "preview", trim, daw: location.hash.slice(1) || "cubase" };   // #fl, #logic...
      if (payload.name === "setTrim") trim = payload.params[0];
      if (payload.name === "setProcessing") procOn = !!payload.params[0];
      if (payload.name === "clearAssist") assist = { lowCut: 0, mudHz: 300, mudDb: 0, pocketHz: 2500, pocketDb: 0 };
      // The Rack: against tools/rack/rack_mock.js's demo project when the preview loads it.
      const M = window.SonicologyRackMock, p = payload.params || [];
      if (payload.name === "getRack" || payload.name === "rackRequest" || payload.name === "rackAnswer") {
        const call = !M ? Promise.resolve(payload.name === "getRack" ? null : { ticket: 0, why: "No demo project in this preview." })
          : payload.name === "getRack" ? M.getRack()
          : payload.name === "rackRequest" ? M.request({ unit: p[0], gen: p[1], change: p[2], module: p[3], control: p[4], value: p[5], gesture: p[6] })
          : M.answer(p[0], p[1], p[2]);
        Promise.resolve(call).then((d) => send("__juce__complete", { promiseId: payload.resultId, result: d == null ? null : JSON.stringify(d) }));
        return;
      }
      if (payload.name === "getRackTheme") { try { result = localStorage.getItem("sonicologyRackTheme") || ""; } catch (e) { result = ""; } }
      if (payload.name === "setRackTheme") { try { localStorage.setItem("sonicologyRackTheme", p[0]); } catch (e) { /* none */ } }
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
        space: { type: "Chorus + Delay", fx: false, duck: ["Lead Vox"], wet: -18 } },
    }));
  }, 1000 / 30);
})();
