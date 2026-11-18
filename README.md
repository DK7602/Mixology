# Mixology

Intelligent multitrack mixing engineer for Cubase, FL Studio, Ableton Live, Logic Pro and Cakewalk (VST3; AU too on a Mac), the missing link between **Voxology** (vocals) and
**Masterology** (mastering). Same template as both: JUCE 8, framework-free C++20 engine with Catch2 tests,
HTML / CSS / JS interface in the platform web view, GitHub Actions builds on every push.

The plan and milestones: [docs/PLAN.md](docs/PLAN.md). User manual (also built into the plug-in): [docs/MANUAL.md](docs/MANUAL.md).
Tests in Cubase: [docs/M1_TEST.md](docs/M1_TEST.md), [docs/M2_TEST.md](docs/M2_TEST.md) (passed), [docs/M3_TEST.md](docs/M3_TEST.md), [docs/M4_TEST.md](docs/M4_TEST.md), [docs/M5_TEST.md](docs/M5_TEST.md), [docs/M6_TEST.md](docs/M6_TEST.md) (current).

## Status: v0.34.0

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
