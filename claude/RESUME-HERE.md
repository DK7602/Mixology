# Mixology: resume here (updated 2026-10-13, v0.4.0)

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

## Milestone 4 status (v0.4.0): BUILT, WAITING FOR THE USER'S CUBASE TEST (docs/M4_TEST.md)
User asked (2026-10-13): "Once fixed move forward with M4?" after the M3 test video + a Duck Under list cut off.
- M3 follow-ups done first: vocal level vs the WHOLE mix (mix loudness minus vocals, so a printed Beat without a
  Link counts) with per-genre targets (vocalTargetFor: rock -1, cinematic -2, electronic/jazz 0, trap/afro +0.5,
  pop/country/rnb/folk +1.5, else +1); left/right balance (TrackAnalyzer balanceDb, registry layout 3) and the
  "lean-left/right" suggestion; "electric" -> guitar (C++ + JS); Overview role curves smoothed; dropdown menus
  (ui.js fitMenu) open upward / scroll when long (the Duck Under list was cut off).
- BridgeSession plans: applyPlan(what, steps) with PlanStep {hash, setVolume db | exact position, checkFrom fromDb
  (skip if moved > 0.3 dB since the plan), setPan, setMute, setSolo}. Walk = the scan machinery (Scan.plan):
  in-place round on the current bank, then reset + next pages, waiting for each page's closed-loop moves
  (kPlanSettleSeconds 3) before banking, then back to the original first channel. PlanProgress {done, skipped,
  notes, message, undo (exact positions before), serial}. restoreAll uses a plan when a changed channel is outside
  the bank; isChanged() for out-of-view channels compares with knownFaders (now with volume/pan/texts).
  session_cli: "plan <now> name|db|pan;..." and "undo <now>"; bridge-sim scenario on 30 channels.
- Mixology Link: Assist EQ (dsp AssistEq: HP 12 dB/oct, mud bell Q1, pocket bell Q1.2; bit-exact when neutral,
  30 ms glide, low cut parks at 10 Hz). Params lowcut (0 = Off, <20 Off), mudfreq, mudgain, pocketfreq, pocketgain.
  Registry layout 4: assistNow[5] + assistRequestSeq (odd/even seqlock) + assistRequest[5]; kNoRequestSeen so a
  Link in a reused slot never applies an old request (trim too). Link window 600x470 with an ASSIST EQ strip
  (text, curve, Clear). linkcheck: -6.00 dB on the tone, cleared = bit exact.
- MixPlanner (dsp): makePlan(Tool, Report, ChannelNow[]) -> Plan {changes {hash, name, role, kind level|pan|
  lowcut|mud|pocket, from, to, wanted, big, hz, why}, notes, summary}. Balance: role groups (energy sums), genre
  weights vs kick (weightsFor), median-anchored, only |dev| > 2.5 moves (to within 1 dB); vocal vs rest from
  report.vocalRatio, split half/half (locks shift it; fader top +6 handed to the rest); ad-libs vs vocal; headroom
  shift so predicted peak <= -6 dBTP (never up); 6 dB cap per Apply (wanted vs to). Unheard channels (no Link)
  = one group from report.unheardLoudness, riding with the rest. Stereo: centre kick/bass (>5), snare (>25), lead
  (>15); pairName() pairs ("(D)", L/R, Dbl, numbers; role other needs an explicit tag); a pair with one side
  already out keeps it and mirrors the other; vocal doubles only spread when 2+; lean fix by trying mirror /
  half / centre on heavy-side tracks with the report's measured mixBalance as bias. Clean Up: low cut on rumble
  tracks + vocals with >= 3 % below 100 Hz (role table), mud dips when mix-target 200-500 Hz > 2.5 dB on the top
  3 carriers (bass only if nothing else). Pocket: maskDb > -6 -> 2/3/4 dB at the band of most overlap.
- Report additions: vocalRatio/vocalTarget, track maskDb / rumble / reference, unheardLoudness, mixWidth,
  ReferenceReport (TrackSettings.reference, saved as 4th field in "tracks" state): bands lined up like mixBands,
  5 regions with text, loudnessDiff, widthDiff.
- Hub: commands plan(tool) -> {plan with id}, planApply {id, picked, allowBig} (faders/pans -> bridge plan;
  EQ -> requestAssist from the Link's current assist), planUndo (bridge undo from PlanProgress.undo + previous
  assist), setReference {hash, value}, refListen "ref"|"mix" (unmute+solo / unsolo+mute through a plan; Trim
  matched = trimNow - loudnessDiff). state.apply {active, what, done, total, message, notes, serial, canUndo,
  undoWhat, hearingReference}.
- UI: tools.js (openToolSheet, openReferenceSheet, hookTools); Overview buttons live; sheet() exported from
  sheets.js and resets body styles. mock.js simulates plans / apply / undo / reference.
- Tests: 77 Catch2 (incl. plans, reference, Assist EQ, registry assist), bridge-sim 19 scenarios / 183 checks,
  linkcheck all pass, pluginval 10 both plug-ins (Linux), ASan/UBSan clean.
- Open: does a muted track's insert still get audio in Cubase (reference via its Link)? Ducking (sidechain) waits
  for M5. Calibrate genre weights / thresholds from the user's results.

## Milestone 3 status (v0.3.0): BUILT, tested by the user (video): worked; follow-ups done in M4
- dsp: Fft (radix-2), TrackAnalyzer (4096-pt, mid/side in one complex FFT, 31 ISO 1/3-oct bands re full-scale
  sine, sideShare, K-weighted loudness, rms, peak, corr, width), AudioRing, MixAnalysis (Moment = mix frame +
  TrackSamples {hash, name, faderDb, frame}; evaluate(settings, unheard) -> Report: roles you > name (same regexes
  as JS guessRole) > sound, stats, suggestions: short, unheard, vocal-low/high/ok (family targets: urban/pop +1,
  band/acoustic -1, tol 3), kick-bass (shared 40-160 Hz > 50 %), kick-quiet, mud / harsh / bass-light|heavy /
  dull|bright vs style target (mix bands tilted +1.5 dB/oct, normalised), vocal-pocket (1.6-4 kHz within 6 dB of
  the vocal), highpass (>20 % below 100 Hz), low-wide, phase, quiet, hot, headroom, fine).
- Registry layout v2 (name MixologyLink.v2.<pid>): per-slot analysis frame under its own seqlock + playing flag +
  song sample. Link: AudioRing after Trim, juce::TimeSliceThread per library (SharedResourcePointer), 200 ms.
- BridgeSession: knownFaders (hash -> title, dB) from every report; scanAllChannels(): reset, page with next
  every 0.5 s until a short/empty/repeated page, then back (reset, next x page, shiftRight x slot, 80 ms apart);
  writes refused while scanning. Simulated with the real script (30 channels).
- Hub: commands analyze {value: style}, analyzeFinish, analyzeCancel, setRole {hash, value: key|""},
  setExcluded; analysisTick(mixFrame, playing) from the editor every 6th frame (5 Hz); auto-finish at 60 s;
  state.analysis {state idle|reading|listening|done, seconds, enough, max, playing, heard, report}; slots get
  role / roleSet / excluded; roleCurves() for the Overview (6 groups x 31 bands, analyzer units). Track settings
  saved in the processor state ("tracks": hash<TAB>role<TAB>excluded).
- UI: roles.js (ROLES, ROLE_ORDER, GROUPS, guessRole), popupMenu() in ui.js, listening panel over the analyzer,
  Mix Assistant report cards (+ Your tracks / Live view), openSuggestion + openTracksSheet in sheets.js, strip icon
  role menu. mock.js simulates the whole flow.
- Tests: 56 Catch2 cases (analysis: bands, sums, LF spread, width, silence, rates; roles, clash / no clash,
  vocal level, masking, rumble + wide lows, mud / harsh, headroom / short / unheard / excluded, calm), bridge-sim
  18 scenarios / 169 checks, linkcheck + analysis checks.

## Milestone 2 status (v0.2.0): PASSED in Cubase Artist 14 (user, 2026-10-13)
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
- User answers (M2): Link shows the real track name + colour in Artist 14 (VST3 channel context works). Edit History
  stays empty after Mixology's moves (so Restore is the only undo for them). R + volume automation: user didn't know
  automation yet; explained, optional 2-minute test offered (step E16 of M1 still unanswered).
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
M4 green run: 38476297775 (77 tests on Windows, Link check with Assist EQ, pluginval 10 + validator, both plug-ins).
M3 green run: 38461991599 (57 tests on Windows, Link check with analysis, pluginval 10 + validator, both plug-ins).
M2 green run: 38444281357 (Windows build, unit tests, Link cross-library check on Windows, pluginval 10 +
VST3 validator for BOTH plug-ins). Artifact Mixology-Windows has VST3/Mixology.vst3 + VST3/Mixology Link.vst3.
Every push: engine + bridge simulator under ASan/UBSan (Linux), Windows build + tests + package + pluginval 10 +
Steinberg VST3 validator. First green run: 38407455828 (v0.1.0). Gotcha fixed there: getProgramName must not be
empty (validator: "Program 000 has no name").
Download: Actions -> latest green run -> artifact "Mixology-Windows" (VST3, Standalone, Cubase MIDI Remote/Mixology,
MANUAL.md, M1_TEST.md, INSTALL.txt).

## Next
1. User runs docs/M4_TEST.md (copy BOTH VST3s: registry layout 4) and sends results, screenshots, log. Tune
   genre weights, thresholds and texts from what they hear.
2. M5: processing pages (EQ, Dynamics, Tone, Stereo in each Link; Space on FX Links; ducking / sidechain), Master
   page. Maybe: restore points saved with the project.
