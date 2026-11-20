# Mixology

Intelligent multitrack mixing engineer for Cubase, FL Studio, Ableton Live, Logic Pro and Cakewalk (VST3; AU too on a Mac), the missing link between **Voxology** (vocals) and
**Masterology** (mastering). Same template as both: JUCE 8, framework-free C++20 engine with Catch2 tests,
HTML / CSS / JS interface in the platform web view, GitHub Actions builds on every push.

The plan and milestones: [docs/PLAN.md](docs/PLAN.md). User manual (also built into the plug-in): [docs/MANUAL.md](docs/MANUAL.md).
Tests in Cubase: [docs/M1_TEST.md](docs/M1_TEST.md), [docs/M2_TEST.md](docs/M2_TEST.md) (passed), [docs/M3_TEST.md](docs/M3_TEST.md), [docs/M4_TEST.md](docs/M4_TEST.md), [docs/M5_TEST.md](docs/M5_TEST.md), [docs/M6_TEST.md](docs/M6_TEST.md) (current).

## Status: v0.42.0

| Piece | State |
| --- | --- |
| Interface: 8 pages, dark / light (click the logo), resizable 4:3 | Done |
| Live: analyzer, peaks, LUFS (Masterology's R128 meter), vectorscope, correlation, loudness history | Done |
| Cubase bridge (M1): MIDI Remote script + SysEx protocol, one channel | Passed in Cubase Artist 14.0.41 |
| Tracks page live (M2): every channel in view, protect, Restore, automation guard | Passed in Cubase |
| Mixology Link v1 (M2): per-track meters, Trim, name / colour pairing | Passed in Cubase |
| Analyze Mix (M3): Link analysis, fader read, roles, Mix Assistant advice, analyzer role colours | Tested in Cubase (video), follow-ups done |
| Smart tools (M4): Auto Mix, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match; Link Assist EQ | Tested in Cubase, follow-ups done |
| Processing (M5 round 1): EQ, Dynamics, Tone, Stereo in each Link and on the full mix; Mono Check | Tested in Cubase, fixes done |
| Space (M5 round 2): reverbs, delay, chorus, phaser, special FX on a track or FX channel, ducking under the vocal | Tested in Cubase |
| Polish / Auto Mix processing proposals (EQ, compression, space, mix tone), Master finishing knobs (M5 round 3) | Tested in Cubase, fixes done (v0.7.1) |
| Suite link (M6 round 1): Voxology on the vocals (role, no doubled compressor / reverb / delay, VOX tag), Masterology's goal on the Master page, the hand-off to Masterology | VOX tag confirmed in Cubase; the rest is being tested |
| Setup check (v0.8.1): a mono group or output on the way (names the likely channel), vocals with Voxology but no Mixology Link, Mixology on a mono channel | Mono group confirmed in Cubase |
| Tracks page scroll bar (v0.9.1) instead of the bank arrows: the channel order from the fader scan, jumps in the fewest bank steps | Passed in Cubase |
| Presets (v0.9.0): Mixology's starting points per kind of track, FX returns and the Full Mix; yours saved for every song; undo | Tested: the track list was empty on Windows, fixed in v0.9.2 |
| v0.9.2 fixes: roles from whole words in the name ("Keys - Verse" = keys), renamed channels forgotten at the next read | Passed in Cubase |
| History (v0.10.0): every change Mixology makes, step by step; back to any point, forward again | Passed in Cubase |
| Auto Analyze (v0.11.0): after Analyze Mix, keeps listening while the song plays; new advice every 15 s when it changed | Passed in Cubase |
| v0.11.1: Auto Analyze stays on when the project opens again (listens by itself, first advice ~20 s in) and follows the style | Built, waiting for the Cubase test |
| One song style for the trio (v0.12.0, with Voxology 0.49 and Masterology 0.28): pick it in any, all three follow | Built, waiting for the Cubase test |
| Your DAW for the trio (v0.13.0, with Voxology 0.50 and Masterology 0.29): found from the host or picked, shared; every tip in that DAW's words (`plugin/ui/daws.js`); FIX THIS FIRST when Voxology heard a vocal recorded too hot | Built, waiting for the test |
| Opens as you left it (v0.14.0): the History, the advice (what Analyze Mix heard) and the restore points are saved with the project | Passed in Cubase |
| Your ears per vocal track (v0.15.0): pick a lead vocal track on the vocal point, Too quiet / Sounds right / Too loud for it; Balance and Auto Mix move each to its own level | Built, waiting for the test |
| The Mix Assistant knows what's fixed (v0.16.0): Done points (a tool fixed it since Mixology listened), OPEN <TOOL> on each point; Vocal Pocket dips inside 1.6 - 4 kHz and deepens a dip it heard | Built, waiting for the test |
| Sonicology (v0.17.0, with Voxology 0.50.3 and Masterology 0.29.2): the company name in all three, a macOS build (VST3 + AU; Mixology makes its own MIDI ports), one installer per platform for the whole suite (`installer/`, `.github/workflows/sonicology.yml`) | Built; the installers are being tried by the beta testers |
| Faders in FL Studio and Ableton Live (v0.18.0): one Python bridge core (`bridge/common/mixology_bridge_core.py`, the Cubase script's protocol) with an adapter per DAW (`bridge/flstudio`, `bridge/ableton`); the setup steps per DAW in Settings > Bridge | Passed the simulator (`tools/bridge-sim/run_py.py`, 214 checks, in ctest); waiting for FL Studio and Live |
| Faders in Logic Pro and Cakewalk (v0.19.0): Mixology is their Mackie Control (`dsp/include/mixology/McuBridge.h`): translates the bridge protocol to Mackie Control, short display names back to full track names, the fader curve built in per DAW and learned from the display | Passed the simulator (`tests/test_mcu.cpp`, a fake Logic and Cakewalk modelled on Cakewalk's surface source and Logic captures); waiting for Logic and Cakewalk |
| Two tracks with one name (v0.19.1): a Mix Assistant point says which name; Auto Mix and the tools leave those tracks alone (before, Auto Mix repeated the same move after it was applied) | Built; found by the user in Cubase |
| The full audit (v0.20.0): analysis right at every sample rate, Stereo Architect never mirrors a lopsided mix, Polish never turns an instrument into a reverb return, each open project keeps its own Mixology state, the Mixology on the master leads (and another takes over), Analyze Mix with the window closed, FL / Live moves checked against the mixer as it is now and refused while recording, FL's solo, audio safety (Shimmer always dies away, no limiter click, NaN-proof, constant tail, bypass in time, no big memory clears while playing), track names never run as code in the window, installers and docs up to date | Built; tests and simulators pass, waiting for the DAW tests |
| Per-track AUTO (v0.25.0): an AUTO button on the EQ, Dynamics, Space, Tone and Stereo pages sets that page for the picked track in one press (one History step, Undo), listening first when Analyze Mix didn't hear the track; one set of rules for every role (and the new Beat role) shared with Polish and Auto Mix (`mix::makeTrackPlan`) | Built; tests and the browser preview pass, waiting for the DAW test |
| No plug-in feedback loop (v0.24.0, with Voxology 0.53.0 and Masterology 0.30.0): Voxology leaves a vocal's level to Mixology when Mixology sets it (no FIT LEVEL, no "beat louder / quieter" card); Mixology publishes `MixInfo::changing` while it changes the mix and Masterology's Auto Analyze waits, then listens afresh (`BridgeHub::handoffChanging`) | Built, waiting for the test |
| CPU check (v0.8.2): 48 real Links + Mixology timed on every build (tools/cpucheck); the idle chain made cheaper | Done: typical 48-track session about 17 % of one core |
| Sonicology Rack (v0.26.0, with Voxology 0.55.0 and Masterology 0.31.0): every Sonicology plug-in in the project in one view (the Rack tab after PRESETS): each track's chain, every module's state and settings, how they work together. Shared view (rack.js / rack.css / art) and link (suite/RackLink.h) in all three; Mixology and every Mixology Link publish their modules (mixology/RackUnit.h) | Done: tested in the engine, the real plug-ins (tools/linkcheck) and the browser preview, not yet in a DAW |
| Sonicology Rack stage 2 (v0.27.0, with Voxology 0.56.0 and Masterology 0.32.0): turn knobs and switch modules from the Rack. A click becomes a request in the owner's queue (suite/RackLink.h), the owner applies it like your own mouse move (plugin/RackHost.h, shared; plugin/RackApply.h), knobs name their parameter and turn back with the owner's curve (rackunit fromKnob) | New: tested in the engine and the browser preview, not yet in a DAW |
| Rack audit fixes and quick wins (v0.28.0, Beta 18, with Voxology 0.57.0 and Masterology 0.33.0): plug-ins send details only for the track a Rack shows and publish at once after a change (25 a second while shown); the Rack's data as a file or base64, never long JSON through a native function; double-click resets a knob, the mouse wheel nudges it, its value follows a drag; one-click fixes with UNDO on the Coach's warnings; a warning when automation moves a knob back and when a plug-in from another beta is in the project (suite/RackLink.h otherLayouts); the window grows while the Rack is open (RackHost.h RackWindow). Audit: docs/rack/AUDIT-2026-11-17.md | New: tested in the engine and the browser preview, not yet in a DAW |
| Rack tools (v0.29.0, Beta 20, with Voxology 0.59.0 and Masterology 0.34.0): UNDO / REDO of the Rack's own changes, A / B of a track's settings, BYPASS of a track's plug-ins (Change::power: each plug-in's own bypass; Honey Tune has none). All in rack.js through the existing requests; BYPASS comes back on when the Rack closes | New: tested in the engine and the browser preview, not yet in a DAW |
| EQ Dynamic (v0.30.0, Beta 22): `eq_dyn` (the first control after the space layers, so no index moves): each bell / shelf band's gain in use follows how far its region (an octave around a bell, below / above a shelf) stands over its ~1.5 s level (1 - 7 dB), glided, the filter redesigned every 32 samples; cuts untouched; 0 % = the static EQ bit for bit. Chip on the EQ graph; a Rack value | New: tested in the engine and the browser preview, not yet in a DAW |
| Rack Beta 23 (v0.31.0, with Voxology 0.61.0 and Masterology 0.35.0): the Rack's SESSION page (every track, its modules as pills, its warning count; a pill opens the module on the RACK page), gain staging in the Rack Coach (each plug-in's peaks in and out, a warning for a big jump or peaks near 0 dBFS), TURN ON HERE on the MODULES page (a state request, one UNDO step) | New: tested in the browser preview, not yet in a DAW |
| Rack Beta 24 (v0.32.0, with Voxology 0.62.0 and Masterology 0.36.0): the track's fader in the Rack's GAIN STAGING (the Mixology that owns the bridge publishes a "mix.faders" module with the faders of the tracks open Racks show, `rack::Link::shownTracks`; requests to control "fader.<hash>" go through `BridgeHub::rackFaderMove`, i.e. the Tracks page's own fader / faderEnd / setDb commands, History included); SAVE on Voxology's row (`rack::Change::save`); Mixology Link's RACK button (link.html / link.js / LinkEditor: the Rack's natives, rack-data files, a 16:10 window while it is open) | New: tested in the engine and the browser preview, not yet in a DAW |
| Rack Beta 25 (v0.33.0, with Voxology 0.63.0 and Masterology 0.37.0): the insert-order check in rack.js (watchOrder / orderLine: Voxology's in / out and the Link's in, each with its own average taken out, over 30 - 50 publishes while the vocal plays; a red Coach line when the Link's input moves like Voxology's input and not its output, ten times running; only when Voxology clearly reshapes the vocal). Voxology's Output PEAKS (Clip / Limit at a Ceiling) | New: tested in the browser preview, not yet in a DAW |
| Rack Beta 26 (v0.34.0, with Voxology 0.64.0 and Masterology 0.38.0): the Rack's COACH page for a vocal (stage 3, vocals first). RackLink layout 4: `Unit::report` (the Coach's JSON, up to 8 KB, sent with the details only) and `Change::coach` (control plan / apply / undo). Voxology does the work (vox::coach::planJson: Auto-Edit's plan, what stands out against its aim, the take played through the plan and measured again); rack.js draws it (coachPage / coachBar) | New: tested in the engine and the browser preview, not yet in a DAW |
| Rack Beta 27 (v0.35.0, with Voxology 0.65.0 and Masterology 0.39.0): the user heard the song cut out while the Coach analysed. Voxology's analysis now rests on purpose (vox/Pace.h); rack.js rebuilds the COACH / MODULES / SESSION pages only when what they show changes (their signature took every plug-in's knob texts, and Masterology's Dynamics readings move all the time), and a knob that only shows a reading changes in place on the RACK page | New: tested in the engine and the browser preview, not yet in a DAW |
| Rack Beta 28 (v0.36.0, with Voxology 0.66.0 and Masterology 0.40.0): the Coach on Mixology's tracks. `mix::makeCoachPlan` (MixPlanner: every page's rules in one joint `Rules`, like Polish for one track), `mix::coach::planJson` (TrackCoach: what it heard, what stands out, the Link's modules with each setting's reason, the tone now / with the plan from the EQ and Tone Balance curves, results, moves). BridgeHub::rackCoach (plan / apply / undo / discard; listens first through trackListen when the track wasn't heard; APPLY = one History step "Coach on '<track>'", checked against a fresh plan; UNDO via undoStep, shared with AUTO's Undo) and coachReport (the Mixology on the master's Unit::report, {"track":<hash>, ...}; rack::viewJson keeps it for that track's view). rack.js: coachOf / coachPage for kind "mix" (ROLE / STYLE / HEARD, moves in RESULTS) | New: tested in the engine and the browser preview, not yet in a DAW |
| Rack Beta 29 (v0.37.0, with Voxology 0.67.0 and Masterology 0.41.0): the Coach on the master. Masterology does the work (polisher::coach::planJson in MasterCoach.h / .cpp: Auto-Master's plan as the Rack reads it, the mix against its genre / reference aim, the loudest 12 s rendered through the plan and measured again; AutoMasterController's plan mode; BUILD NOW / STOP). Shared RackLink: the Coach's steps pass a unit's lock (the plug-in decides). rack.js: coachPage for kind "master" (GENRE / INTENSITY / TARGET, BUILD NOW, results with their own units) and STOP for every Coach while it listens | New: tested in the engine and the browser preview, not yet in a DAW |
| Beta 30 (v0.38.0, with Voxology 0.68.0 and Masterology 0.42.0): user feedback on Beta 29. The Coach after APPLY: Voxology and Masterology compare the plan with the settings they have now (and Voxology measures the take through them), so ANALYZE AGAIN says what is already set ("same" per module, "upToDate", NOTHING TO CHANGE) instead of offering the same plan. The RACK buttons show the word (no cube) in all three plug-ins and Mixology Link; Mixology's manual moved into Settings (Manual), and RACK takes the manual button's space | New: tested in the engine and the browser preview, not yet in a DAW |
| Beta 35 (v0.42.0, with Voxology 0.72.0 and Masterology 0.46.0): GUIDED MIX (the "first ten minutes" from the audit). Shared rack.js / rack.css: a GUIDED MIX button on the SESSION page; while it runs (S.guide, kept per project in localStorage) its steps take the left column (SESSION: beside WHAT TO FIX FIRST; COACH: in place of SIGNAL CHAIN): setup, listen (Analyze Mix from the Rack, its progress, FINISH), what to fix first, the Coach on each track (guideTracks: lead vocal, vocals, bass, kick, snare, hats / drums, beat, the rest), the master, finished. Each step's text follows the Coach's state (idle, listening, ready, PREVIEW, kept, already set); guideObserve notes KEEP / NOTHING TO CHANGE. BridgeHub: Coach steps "mixanalyze" / "mixfinish" / "mixcancel" (startAnalyzeMix, shared with the Overview's ANALYZE MIX); fixReport adds hearing / enough / playing | New: tested in the browser preview (the C++ compiled against JUCE), not yet in a DAW |
| Beta 34 (v0.41.1, with Voxology 0.71.1 and Masterology 0.45.1): the user's Beta 33 test. A moved module always landed in the 1st or 2nd place: rack::Link::request / take clamped every value to 0..1 (a knob's), the new place too; Change::order now carries its place (0 .. kMaxModules - 1), test with a big place; the browser mock clamps like the real link. The top bar at 1280 px (a Rack in a smaller window): BACK TO ... was cut off; narrow: "BACK", less padding. Warnings say Mixology's Compressor / Space (not "the Link's"). The Coach after APPLY: a knob counts as already set when it is musically close (1.5 dB, 15 % in frequency, 25 % in time, 8 points of a %; was 2 % of its range), in Voxology and Masterology | New: tested in the engine and the browser preview, not yet in a DAW |
| Beta 33 (v0.41.0, with Voxology 0.71.0 and Masterology 0.45.0): a chain you can reorder. Shared RackLink: Change::order (control: the module's key or "reset"; value: its place among the unit's movable modules), Module::movable / moved (bits 1 / 2 of the slot's switchable word: no layout change). proc::Order (ProcParams: the ten movable modules, kDefaultOrder, movedTo, orderText / orderFromText, packOrder), proc::Chain::setOrder (the modules that change place fade out through their own weights, the order switches once they are silent, they fade back in; at once before the first values or while idle); rackunit: the movable modules in the order they play (movable / moved), moduleOf; RackApply: Change::order, setOrder / loadOrder (the processing state's "order" property + an atomic for the audio thread), in Mixology Link and Mixology's Full Mix. rack.js: drag a movable row in SIGNAL CHAIN (6 px to start; a click still picks), RESET ORDER under the rows, UNDO / REDO of moves | New: tested in the engine and the browser preview (the C++ compiled against JUCE), not yet in a DAW |
| Beta 32 (v0.40.0, with Voxology 0.70.0 and Masterology 0.44.0): the Coach's PREVIEW and a review of Betas 26 - 31. PREVIEW: the plan plays without History (BridgeHub: "preview" / "flip" / "keep" / "back"; your values from readProc kept per Link and put back; KEEP = APPLY with History's "before" from yours), the louder side turned down to the other on the track's Link (LinkRegistry layout 7: previewDb / previewMs, only down, ends without renewal in 3 s; the Link applies it after its meters, never in an export); the gap is an estimate (mix::coach::loudnessGapDb: the plan's and your EQ / Tone Balance over the heard spectrum, K-weighted). Ends when no Rack shows the track for 4 s, on Analyze Mix, a new plan, DISCARD, another project, Mixology closing. Review fixes: the Coach resets with the project; a stale APPLIED after History's UNDO is ready again; APPLY refused during Analyze Mix; "upToDate" for Mixology's tracks; locale-free numbers in the plan JSON; rack.js redraws after a Coach answer (a refused button no longer stays grey), any shown track can ask Mixology's Coach, the owner Mixology (with the faders) is the one read | New: tested in the engine and the browser preview (the C++ compiled against JUCE), not yet in a DAW |
| Rack Beta 31 (v0.39.0, with Voxology 0.69.0 and Masterology 0.43.0): WHAT TO FIX FIRST, the project view (RACK-PLAN stage 3's last item). BridgeHub::fixReport: the setup check, then the Mix Assistant's points worst first (done / tool / tracks), as the Mixology-on-the-master unit's report while a Rack shows the Full Mix; the SESSION page asks for the Full Mix. rack.js: fixItems / fixPanel (with the Rack's together warnings and the insert-order check; said once), the SESSION rows count what is worth fixing on each track | New: tested in the browser preview (the C++ compiled against JUCE), not yet in a DAW |

## Layout

```
dsp/        Framework-free engine (no JUCE): unit-testable
  include/mixology/
    BridgeProtocol.h   SysEx message format shared with the Cubase script
    BridgeSession.h    Plug-in side of the bridge: per-write channel identity, closed-loop dB moves, protect, restore
    FaderLaw.h         Cubase fader position <-> dB, learned from Cubase's own display text
    LinkRegistry.h     Mixology Link <-> Mixology: shared-memory registry (meters, identity, Trim, analysis) + pairing
    TrackAnalyzer.h    One analysis frame: 31 one-third-octave bands, loudness, width (own small FFT: Fft.h)
    MixAnalysis.h      Analyze Mix: moments -> track roles, stats and the Mix Assistant's suggestions
    SharedBlock.h      Named shared memory, so two plug-in libraries in one host see each other
    TrackMeter.h       The Link's light per-track meter (peaks, momentary / short-term loudness)
    LoudnessMeter.h    EBU R128 meter (from Masterology, repo DK7602/polisher)
    ProcParams.h       M5: every processing control (id, range, default), the one table both plug-ins and the UI use
    ProcChain.h        M5: the processing chain (gate, EQ, dynamics, tone, stereo, space, limiter); off = bit for bit
    SpaceFx.h          M5 round 2: reverb (FDN), delay, chorus, phaser, pitch shifter for the Space page
    ProcDsp.h          M5: its building blocks (filters, compressors, oversampling, lookahead limiter)
  include/suite/
    SuiteLink.h        M6: Voxology, Mixology and Masterology tell each other what they do (the same file in all three repos)
  include/mixology/SetupCheck.h   Setup check: a mono group on the way, vocals without Mixology Link (Mix Assistant)
  include/mixology/Presets.h      Presets: Mixology's own, yours (one text file), loading onto a track / FX / the Full Mix
plugin/     Mixology: JUCE processor (Full Mix processing + meters), BridgeHub (MIDI ports + Link pairing), web editor;
            ProcHost.h turns the processing table into host parameters (shared with the Link)
  ui/       The interface: index.html, styles.css, app.js, ui.js, pages_*.js, sheets.js, mock.js, assets, fonts (OFL)
link/       Mixology Link: processor (Trim, Assist EQ, processing chain, TrackMeter, registry), small web window
            (ui/link.*, shares styles.css / ui.js)
bridge/cubase/Mixology/Bridge/Mixology_Bridge.js   Cubase MIDI Remote script (ES5)
tests/      Catch2 tests + FakeCubase
tools/bridge-sim/   End-to-end simulator: the real Cubase script in Node against a fake MixConsole,
                    talking to the real C++ session (runs in CI)
tools/linkcheck/    Hosts two real Mixology Link instances and reads them from another module (runs in CI)
tools/cpucheck/     48 real Mixology Links + Mixology timed like a big Cubase session; budgets on the mean (runs in CI)
tools/procspec/     Writes plugin/ui/procspec.js from the processing table; a test checks it is current
tools/art/          Cuts the logo, title and Space scenes out of the approved mockups
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Engine + bridge tests only (any OS, no JUCE download; the end-to-end test needs Node):

```bash
cmake -S . -B build-tests -DMIX_BUILD_PLUGIN=OFF && cmake --build build-tests && ctest --test-dir build-tests
```

UI preview in a browser (no DAW): copy JUCE's `modules/juce_gui_extra/native/javascript/*.js` into
`plugin/ui/juce/`, run `python3 -m http.server` in the repo root and open
`http://localhost:8000/plugin/ui/index.html`. `mock.js` simulates the plug-in, a playing mix and a Cubase bank.

Download: Actions -> latest green run -> artifact "Mixology-Windows" (plug-in, Cubase script, manual, test steps).
