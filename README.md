# Mixology

Intelligent multitrack mixing engineer for Cubase (VST3), the missing link between **Voxology** (vocals) and
**Masterology** (mastering). Same template as both: JUCE 8, framework-free C++20 engine with Catch2 tests,
HTML / CSS / JS interface in the platform web view, GitHub Actions builds on every push.

The plan and milestones: [docs/PLAN.md](docs/PLAN.md). User manual (also built into the plug-in): [docs/MANUAL.md](docs/MANUAL.md).
Tests in Cubase: [docs/M1_TEST.md](docs/M1_TEST.md), [docs/M2_TEST.md](docs/M2_TEST.md) (passed), [docs/M3_TEST.md](docs/M3_TEST.md), [docs/M4_TEST.md](docs/M4_TEST.md), [docs/M5_TEST.md](docs/M5_TEST.md), [docs/M6_TEST.md](docs/M6_TEST.md) (current).

## Status: v0.11.0

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
| Auto Analyze (v0.11.0): after Analyze Mix, keeps listening while the song plays; new advice every 15 s when it changed | Built, waiting for the Cubase test |
| CPU check (v0.8.2): 48 real Links + Mixology timed on every build (tools/cpucheck); the idle chain made cheaper | Done: typical 48-track session about 17 % of one core |

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
