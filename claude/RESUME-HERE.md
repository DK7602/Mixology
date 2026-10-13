# Mixology: resume here (updated 2026-10-13, v0.2.0)

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
- Mixology Link (M2+): small plug-in per track: meters + Trim now, analysis in M3, later that track's EQ /
  dynamics / tone / stereo; on an FX channel: Space. Track name / colour via VST3 channel context (JUCE
  updateTrackProperties). It is a SEPARATE library from Mixology, so a static singleton (Voxology's UnmaskLink
  trick) can't work: they share a named memory block (dsp SharedBlock + LinkRegistry).
- Bridge: Cubase MIDI Remote script (bridge/cubase/Mixology/Bridge/Mixology_Bridge.js, ES5) <-> two loopMIDI
  ports ("Mixology to Cubase", "Cubase to Mixology") <-> BridgeHub in the plug-in (opens the ports itself,
  only after the editor first opens, so scans / pluginval never touch MIDI). No tray app (fallback if
  Cubase/Windows blocks a plug-in from opening the ports).
- Protocol: dsp/include/mixology/BridgeProtocol.h (SysEx F0 7D 4D 58 01 ...). JS mirrors it (LAYOUT table).
  Bump kProtocolVersion + PROTOCOL_VERSION together.

## Milestone 2 status (v0.2.0): BUILT, WAITING FOR THE USER'S CUBASE TEST (docs/M2_TEST.md)
- Tracks page: all 12 strips write. Every command carries ChannelRef {slot, name hash} = what the strip showed;
  BridgeSession refuses if the slot now holds another channel, the script re-checks slot + hash on arrival.
  No lock / pause any more. Per-slot closed-loop dB moves (latest wins), wheel nudges add up (nudge from the active
  target), drags = exact positions, logged once when Cubase confirms the last one.
- Padlock = PROTECT (gold): session refuses all writes to that name hash; saved in Mixology's plug-in state
  ("protected" attribute: "hash<TAB>title" lines). R automation: allowed, warned once per channel; W: refused.
- Restore points per channel (keyed by name hash, ambiguous if the name repeats in the bank -> restore refused).
  restoreChannel / restoreAll (outside-bank + skipped counts), forgetRestorePoints ("Keep the mix as it is now").
  Session lifetime only (not saved with the project yet).
- Log: "Fader moved in Cubase" only for real Cubase-side moves (reports within 0.6 s of our own write = echo),
  once per settled value, never for channels with R on, not for the first report after (re)connect / bank.
- Mixology Link (link/, VST3 "Mixology Link", code Mxk1): Trim param (-24..+24, smoothed), TrackMeter (50 ms peaks,
  momentary / short-term, no true peak = cheap), identity from updateTrackProperties (VST3 channel context), 10 Hz
  timer: heartbeat, trim requests -> setValueNotifyingHost. Web window 600x400 (link.html/css/js; shares styles.css,
  ui.js, fonts, logo, wordmark through its own binary data). Theme follows Mixology's settings file (read-only).
- Shared registry: dsp LinkRegistry (layout v1, 256 slots, seqlock identity, atomics) in a SharedBlock named
  "MixologyLink.v1.<pid>" (Windows CreateFileMapping Local\, POSIX shm_open). Each library has its own handle
  (processRegistry()). Main: BridgeHub::updateLinks at 30 Hz: heartbeat, matchLinks (name hash; colour only to break
  ties, both ways), reportMatch back to each Link (its window shows Paired / two tracks share the name).
  Strip meters = Link peak + fader dB (post-fader, like Cubase's meters).
- Checks: 37 Catch2 cases (bridge + link + TrackMeter), bridge-sim 17 scenarios / 161 checks (script v3 unchanged),
  tools/linkcheck (MIX_BUILD_LINKCHECK=ON): hosts 2 real Link VST3s + reads them from another module: 22 checks
  (Linux local pass; CI runs it on Windows). pluginval strictness 10 SUCCESS for Mixology Link (Linux).
- Open questions for the user: Link shows the real track name / colour in Cubase Artist 14? Remote moves in
  Edit > History? R automation + play after a move?
- Mix styles (user request 2026-10-13: "rock, country, more pop, gospel, worship, all genres", pick a genre then a
  style in a pop-up): plugin/ui/mixstyles.js = 14 genres / 75 styles (bass, scoop, air targets + one-line about).
  Header arrows step within the genre; name click / PRESETS tab -> genre grid -> style pop-up (#style-pop).
  Saved as "genre.style" key in the UI state ("preset"); old numeric index migrated (LEGACY list). Milestone 4 must
  add level / pan / width targets per style.
- Light look uses the user's own chrome title art (plugin/ui/assets/wordmark_light.png, from their upload 2026-10-13,
  already transparent; trimmed to 1200 x 154). app.js / link.js swap the <img> src with the theme. Dark keeps wordmark.png.
- User M2 test (2026-10-13): "everything worked" except B.3: the type-in box wrapped under the number, behind the
  pan knob (fixed: it replaces the number in place). Their screenshot also showed the -24 / -36 / -48 scale marks
  piled up: the FaderLaw had learned mid-move pairs (new position + old text, Cubase reports them while a fader
  moves). Fix: BridgeSession learns a pair only after 0.25 s without a newer report on that slot (kLearnSettle);
  setDb refinement uses its own readbacks (shifted curve, then secant) instead of learning them; saved curves
  without "faderLawFormat" 2 are dropped once; the strip scale skips a mark closer than 18 px to the previous one.
  Test: "a fast Cubase-side drag with lagging text doesn't bend the fader curve" (fails with the old learning).
- CI gotcha: MSVC has no M_PI (test_link.cpp failed the first M2 run).

## Milestone 1 status: PASSED in Cubase Artist 14.0.41 (script v3, 2026-10-13)
User ran M1_TEST 1-22: all worked (Set dB exact, drag, pan, M/S, Cubase->Mixology, rename / reorder / bank pause,
W refused, Restore, window reopen, Reload Scripts -> "Lost contact" then reconnected and resumed, mono / stereo /
group / FX / instrument). Not reported: step 16 (R + playback) and 18 (History). Log noise found in the real log:
"Fader moved in Cubase" repeated with the same value and after Mixology drags -> fixed in M2.
M1 design notes (superseded by M2 where noted):
- Safety: writes only to the LOCKED slot; each write carries name hash + Cubase channel-ID hash
  (getUniqueIDString, else getRuntimeID); script refuses on mismatch / empty / automation-write.
  Rename / reorder / bank / hide -> PAUSE until re-lock; reconnect resumes only if same slot+name+id.
  Restore point taken before the first write. dB moves closed-loop on Cubase's display text (exact 2-decimal
  match, else within 0.05 dB, max 6 steps); FaderLaw learns (position, dB) pairs, saved in
  %APPDATA%/Mixology/Mixology.settings. Drags send exact positions (latest wins), logged once at the end.
- Script: 12-slot mixer bank zone, excludes inputs/outputs (Stereo Out never in bank); v2 dropped followVisibility.
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

## First Cubase test (2026-10-13, user, Cubase Artist 14, script v1)
loopMIDI + script + plug-in connected fine ("Connected to Cubase (script v1)", pings OK) but ALL 12 slots empty:
Cubase never delivered channel titles to channel.mOnTitleChange (the dump logged no slots). Script v2: no
setFollowVisibility (Steinberg's examples never call it), titles / values / text read from BOTH the channel host
values and the bound custom value variables (surface side = the route Steinberg's examples use), every callback
guarded, and each dump logs a diagnostics block ("calls: titles a/b ..." = channel route / control route,
idle count, mapping, errors). Log text is split into <= 56-char pieces (protocol text limit 63 bytes).
Simulator models routes 'channel' / 'control' / 'both' (tools/bridge-sim). Waiting for the v2 log.

## Crash on project open (2026-10-13) -> script v3
User: Cubase 14.0.41 crashed opening the "Sample Snagger" project (another project opened fine). Two minidumps
(analysed with rust-minidump's minidump-stackwalk 0.27): both EXCEPTION_ACCESS_VIOLATION_READ at address 0, same
instruction Cubase14.exe+0x2cade33 (`cmp byte [r12+r8],0`, rdi=-1: a strlen on a NULL char*), main thread, ~17 s
after start (project load). Mixology.vst3 NOT loaded in the first dump, loaded but not on the stack in the second.
The JS engine's identifier tables show script v1 (first) and v2 (second) were loaded; both call
getUniqueIDString(). Hypothesis (not proven): Cubase returns a NULL ID string for some channel type in that project
(it has a Cubase 14 Drum Track: 'Drum Machine' tab) and crashes converting it. Script v3 makes NO channel-ID calls
(protocol field kept, always 0; identity = name hash + slot). Simulator asserts idCalls === 0. If v3 still crashes,
next suspects: the 12-channel bank on that project's channel types, action bindings to mResetBank/mShift*.
CONFIRMED by the user: with script v3 both crashing projects (Sample Snagger, Test) open and close fine. Never call
getUniqueIDString / getRuntimeID during load again; if channel IDs are wanted later, test them on demand only.
FIRST WORKING CUBASE CONNECTION (script v3, Sample Snagger project): 3 channels named, faders 0.0, pans C. Diagnostics
"calls: titles 36/12, volume 27/3, text 27/12" = BOTH routes fire in Cubase Artist 14.0.41, so v1's empty bank was
caused by setFollowVisibility(true) (don't use it). Titles > 63 bytes arrive truncated (hash is of the full name; the
plug-in compares truncated title + hash consistently). Next: user runs M1_TEST steps 3+ (Set dB, drag, pan, M/S).
Other MIDI Remote scripts on the user's machine: Arturia KeyLab Essential 61 mk3, Novation Launchkey mk4 49, Akai
APC40 mkII / MPK249, Korg nanoKONTROL; MIDI ports appear via Windows MIDI Services ("I|Windows MIDI|Mixology to Cubase").

## CI
M2 green run: 38444281357 (Windows build, unit tests, Link cross-library check on Windows, pluginval 10 +
VST3 validator for BOTH plug-ins). Artifact Mixology-Windows has VST3/Mixology.vst3 + VST3/Mixology Link.vst3.
Every push: engine + bridge simulator under ASan/UBSan (Linux), Windows build + tests + package + pluginval 10 +
Steinberg VST3 validator. First green run: 38407455828 (v0.1.0). Gotcha fixed there: getProgramName must not be
empty (validator: "Program 000 has no name").
Download: Actions -> latest green run -> artifact "Mixology-Windows" (VST3, Standalone, Cubase MIDI Remote/Mixology,
MANUAL.md, M1_TEST.md, INSTALL.txt).

## Next
1. User runs docs/M2_TEST.md (copy both VST3s; script unchanged) and sends results + log. Fix what fails.
2. M3: Links listen (1/3-octave spectrum, crest, width, song-position tags), roles, Analyze Mix, Mix Assistant,
   exclude-from-analysis. Maybe: bank follows Cubase's selected track (mTrackSelection), restore points saved
   with the project.
