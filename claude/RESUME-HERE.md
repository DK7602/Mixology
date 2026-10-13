# Mixology: resume here (updated 2026-10-13, v0.1.0)

Read this first in a new chat. Code: github.com/DK7602/Mixology, branch `claude/mixology`.
Sister projects (same user, same template): Masterology (mastering), Voxology (vocals, + Honey Tune).
NAMING: the mastering plug-in Polisher was renamed MASTEROLOGY (vendor "Masterology Audio", user 2026-10-13). Always call
it Masterology in Mixology's text / UI / docs. Its repo is still github.com/DK7602/polisher and its internal code names
still say "polisher". New logo (chrome crown over red/blue waveforms + MASTEROLOGY): plugin/ui/assets/logo.webp on
branch claude/eloquent-einstein-sscadx of that repo. Mixology's LoudnessMeter / KWeighting / TruePeakDetector / Biquad
are copied from it (namespace renamed to mixology).
Plan: docs/PLAN.md (milestones M0-M6). Manual: docs/MANUAL.md (built into the plug-in, ? button).

## The user
Same person as Voxology / Masterology: Cubase ARTIST 14 on Windows, makes hip-hop / trap / G-funk, mixing
novice. Wants plain language, momentum ("you make the best choice"), honest status, short replies.
Tests in Cubase and by screen recording. LOVES the approved mockup look: keep layout, colours, knobs, the
beaker logo exactly; fix spelling / wrong labels / fake track names (user said so). Light mode = click the
logo: same layout + accents, white background and white knobs.

## Source material
Handoff zip from ChatGPT (mockups 01-24, spec, manual draft). Mockups are AI concept art: the look is the
spec, numbers / labels in them are not. Art cut out by tools/art/extract_assets.py from the zip's Images
folder (logo from image 12, wordmark from image 12, gears from 12, Space scenes from 13 + 17-22). If the user
sends higher-resolution art, re-run it.

## Architecture (decided)
- Mixology (main plug-in) on the Stereo Out: big UI, analysis hub, owns the Cubase bridge.
- Mixology Link (M2+): small plug-in per track: analysis (shared in-process registry like Voxology's
  UnmaskLink) + later that track's EQ / dynamics / tone / stereo + trim; on an FX channel: Space.
  Track name / colour via VST3 channel context (JUCE updateTrackProperties).
- Bridge: Cubase MIDI Remote script (bridge/cubase/Mixology/Bridge/Mixology_Bridge.js, ES5) <-> two loopMIDI
  ports ("Mixology to Cubase", "Cubase to Mixology") <-> BridgeHub in the plug-in (opens the ports itself,
  only after the editor first opens, so scans / pluginval never touch MIDI). No tray app (fallback if
  Cubase/Windows blocks a plug-in from opening the ports).
- Protocol: dsp/include/mixology/BridgeProtocol.h (SysEx F0 7D 4D 58 01 ...). JS mirrors it (LAYOUT table).
  Bump kProtocolVersion + PROTOCOL_VERSION together.

## Milestone 1 status: BUILT, NOT YET TESTED IN CUBASE
- Safety: writes only to the LOCKED slot; each write carries name hash + Cubase channel-ID hash
  (getUniqueIDString, else getRuntimeID); script refuses on mismatch / empty / automation-write.
  Rename / reorder / bank / hide -> PAUSE until re-lock; reconnect resumes only if same slot+name+id.
  Restore point taken before the first write. dB moves closed-loop on Cubase's display text (exact 2-decimal
  match, else within 0.05 dB, max 6 steps); FaderLaw learns (position, dB) pairs, saved in
  %APPDATA%/Mixology/Mixology.settings. Drags send exact positions (latest wins), logged once at the end.
- Script: 12-slot mixer bank zone, excludes inputs/outputs (Stereo Out never in bank), followVisibility.
  Values + display text are flushed together in mOnIdle (API 1.1+) so pairs always match; also flushed after
  each incoming message. Titles deduplicated (Cubase repeats them).
- UNVERIFIED in real Cubase (M1_TEST.md asks): mute/solo via default value binding; bank via
  makeActionBinding + setProcessValue(1) then 0; display text format; whether channel IDs follow channels;
  whether remote moves land in MixConsole History; whether a plug-in can open loopMIDI ports while Cubase
  has them (loopMIDI is multi-client, expected OK).
- Tests: 24 Catch2 cases (tests/test_bridge.cpp vs FakeCubase) + tools/bridge-sim/run.js (11 scenarios, 65
  checks: the real JS script in Node with a fake MIDI Remote API <-> real C++ session via session_cli),
  both in CI (ASan/UBSan job). pluginval strictness 10 SUCCESS locally (Linux).

## UI notes
- Design canvas 1440 x 1080 (mockups are 4:3), editor default 1152 x 864, resizable 720-2160 wide, fixed aspect.
- Knobs are code-drawn (SVG arc + CSS body): dark = black metal, light = white ceramic, both gold edge;
  bipolar knobs light red below centre, orange above; blue = the rest of the travel.
- Preview pages (EQ, Dynamics, Space, Tone controls, Stereo widths, Master knobs) show a "Preview" tag and
  start at the mockup's values. When their DSP lands (M5) they must start NEUTRAL (flat / off).
- Analyzer spectrum: Masterology's 4.5 dB/oct tilt; a balanced mix reads about -40 -> displayed with +40 offset.
- Browser preview: see README (mock.js simulates audio + a 14-channel Cubase project).
- Screenshots: Playwright with /opt/pw-browsers/chromium_headless_shell-*/chrome-linux/headless_shell.

## Next
1. User runs docs/M1_TEST.md in Cubase Artist 14 and sends results + log. Fix what fails.
2. M2: Tracks page fully live (all 12 strips writable with per-channel lock / exclude), Mixology Link v1
   (trim + meters + identity pairing), automation-read warning, bank follows Cubase selection option.
