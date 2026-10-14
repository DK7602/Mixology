# Mixology: resume here (updated 2026-10-14, v0.7.0)

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

## Milestone 5 round 3 status (v0.7.0): BUILT, WAITING FOR THE USER'S CUBASE TEST (docs/M5_TEST.md, top part)
User (2026-10-14): "will auto edit ever automatically set the eq, space, etc.?" + "lets fix that and move forward".
- MixAnalysis TrackReport + levelRange (P90 - P10 of ~1 s averaged pre-fader rms over active moments) and loudPeak
  (P90 of pre-fader frame peaks).
- MixPlanner Tool::polish ("polish", "Polish"; Auto Mix includes it). ChannelNow + hasProc / proc / procAtAnalysis
  (known?) / duck; MixNow {now, atAnalysis} for the Full Mix. Change + proc (id, value list), fullMix, setDuck/duck,
  fromText/toText; kinds eq / comp / width / space / tone. planPolish: vox clarity = tilted(2-5 k) - tilted(200-800)
  (< -2.5 -> +presence 3.5 kHz bell, > 5 -> narrow dip at the loudest 2.5-5 k band), air gap tilted(8-12.5 k) -
  presence < -7 -> 10 kHz shelf; kick box (315-500 vs 50-100 untilted > -8) -> dip; comp when levelRange >= 8 (vocal,
  3:1, thr loudPeak-6, 8/90 ms, +2) / >= 7 (bass, 4:1, thr loudPeak-5, 25/150), skipped if comp on; kick/bass stereo
  lowWidth > .12 -> st_low 0; lead vocal reverb: FX-like Link (name regex or sp_route) -> Space there (route FX, duck
  6 dB under the lead) + note to add a Send, else on-track plate 12 % (gospel/worship/cinematic: hall); ad-libs
  ping-pong 1/8 15 % (urban/pop styles); Full Mix Tone Balance when mix vs style target > 2.5 dB (half the gap, max
  2). EQ band choice: a band already doing that job (same shape, < 0.4 oct) gets adjusted (absolute = value at
  analysis + correction), else the preferred band if flat, else another flat one. tests/test_polish.cpp (8 cases).
- Hub: procAtAnalysis snapshot (every unique Link + Full Mix via MixProcessing get/set the processor registers),
  channelsNow fillProc, planToVar fromText/toText/fullMix, applyCurrentPlan: requestProc / mixProc.set + requestDuck,
  LastApply.procs (before values, duck before), undo newest-first.
- UI: Overview tool list 7 buttons (90 px), POLISH (star); tools.js kinds + texts; Master page Glue (fin_glue),
  Tilt (tb_warmth / tb_air around their centre), Width (fin_width), Trim (fin_out) via procSet("mix"), frame.master.

## v0.6.1: the Electric guitars' "L100" (user, 2026-10-14, after round 2: "everything else seems to work")
User set the 4 Electric pans to C in Mixology, ran Auto Mix, they read L100 again (heard centred). Cause: no panner
(mono tracks into a mono group); the script's bound control (panVar) echoed the value Mixology sent, so the pan watch
saw it "land"; Cubase's own value (0 = "L100") came back on the next bank report. Fix: BridgeSession.panWrites +
notePanReport(): a channel reported at exactly 0 (kHardLeft) >= 1.2 s after Mixology wrote it elsewhere -> noPan
(markNoPan, log "... reported it back at hard left"); a noPan channel reported > 0 -> forgotten (Cubase pans it after
all). noPanList / setNoPanList / noPanVersion; the hub keeps it in TrackSettings.noPan (6th field of the saved track
settings) -> saved with the project. Script v4 (bridge::kLatestScriptVersion): pan only from route 1 (channel host
value) unless route 1 never reported a pan (hostPanRoute fallback); older scripts get a log hint. Tests: bridge test
with FakeCubase.echoPanWrites, bridge-sim scenario "no panner" (fails on v3, passes on v4), session_cli "noPan".
User also asked: "will auto edit ever automatically set the eq, space, etc.?" -> yes: M5 round 3 (next).

## Milestone 5 round 2 status (v0.6.0): TESTED ("everything else seems to work", 2026-10-14)
User (2026-10-14): "ok I think its ready for the next milestone" (after the round-1 fixes; no round-1 retest report).
- dsp SpaceFx (framework-free): DelayLine (pow2, linear frac read; read(d) = d samples before the latest), FdnReverb
  (8 lines 31.3-73.9 ms x size, Hadamard, per-line gain 10^(-3 len/RT60), one-pole damping in the loop, LFO
  modulation, 4 input all-pass diffusers, early-reflection taps, freeze = no input + ~no decay), StereoDelay
  (LP/HP in the loop, ping-pong cross-feed, glided time), Chorus (3 voices), Phaser (6 all-pass stages, feedback),
  PitchShifter (two Hann taps, 50 ms window).
- ProcParams: Space appended after stHighX (41 ids: sp_on/type/route, sp_lowcut/highcut, duck_amount, rev_*, dly_*,
  cho_*, pha_*, hall_*, room_*, spx_* + spx_mode); Module::space (key "space"); Meters + spaceWet (dBFS) + duck (dB)
  = 15 floats. Units "s" / "st" in ProcHost valueText.
- ProcChain: order ... final, SPACE, limit. SpaceBank (all engines, ~MBs) made by ensureSpace() on the MESSAGE
  thread (Link / Mixology timers when sp_on; prepareToPlay too), published by an atomic pointer; until then a track
  passes, an FX route outputs silence. Plate size .85, Hall 1+1.2 size + early, Room .25+.55 size with early/late,
  Special size 1.9 (shimmer: last chunk's wet pitched into the tail; drift: more modulation; swell: envelope restarts
  on new notes; freeze: 40 s RT, input x.25), Delay from setHost bpm (120 fallback). 60 ms crossfade on type change.
  Wet: low/high cut (12 dB/oct), ducking (key dB -> want = amount * clamp((keyDb+50)/18), attack 15 / release 300 ms),
  soft clip > 0.8. Route FX = wet only, else dry + mix*(wet - dry). tailSecondsFor(). 8 [space] tests (RT60 slope,
  delay timing / ping-pong / tempo, chorus/phaser mix, ducking, shimmer octave, swell, freeze bounded, type switch,
  not-ready). The random stress test now runs with Space made and checks "goes quiet after", bound 2000.
- Registry layout 6: keyLevel/keyMs (each Link's audio thread, every block: pre-chain peak env, 60 ms release; stale
  after kKeyMs 250 = 0), duckRequest (seqlock, Mixology -> Link), duckNow (Link -> Mixology), findSlots(hashes, self).
  LinkInfo.duck. Link: setDuck() saves "duck" (hex hashes) as a property of its APVTS state, resolves slots ~3x/s,
  audio thread takes max keyLevel of those slots -> chain.setHost(bpm, key). getTailLengthSeconds = ProcHost
  reportedTail(): 0 off, 30 s on, infinity for Freeze; timer calls updateHostDisplay(latencyChanged) when that
  changes (Cubase may suspend a plug-in after its tail; restarts it, so only on on/off/freeze). Mixology main: same
  ensureSpace / tail / setHost(bpm, -1), meterDefault(13) = -100.
- Editor: frame.proc on the space page too; procTargets entries + spaceType, spaceRoute, hash, role (hub.trackRole),
  duck; meters.space / meters.duck; native spaceDuck(key, [hashes]). Link window strip: "Reverb (FX, 100 % wet)" chip
  + "ducks under X".
- UI pages_process.js buildSpace: tracks-only target select (placeholder "Pick a track"), one effect per track: list =
  view; ON switches this effect on (or replaces the other type); knob / preset / mode moves switch Space on with the
  viewed type only when Space is off; a new track jumps to the effect it uses (proc.loadedKey). USED AS select;
  FX-like names (/^fx\s*\d|verb|delay|echo|return|aux|send|space/) get route FX on first switch-on. Presets per
  effect (values arrays), "Custom" when none matches. EFFECT EQ (sp_lowcut / sp_highcut). DUCKING: Nothing / All vocals
  / each track (hashes), AMOUNT, live bar. Scene: setActive(level) — still picture (4 fps redraw, no motion) unless
  this effect is used on the target (level from the wet meter) or on another track (0.7). Lamps = this target only.
- Checks: 113 ctest (ASan clean); linkcheck: tail 30 s, reverb rings on after the input (-17 dBFS), Space meter, FX
  reverb ducks 21 dB under the Kick and comes back (0.0 dB), duck list saved with the project, Freeze = infinite
  tail, Space off = 0, Mixology's tail too.

## Milestone 5 round 1 status (v0.5.0): TESTED (test 1 fixes below)
User (2026-10-14): "Ok, lets move on to the next milestone" (after M4 worked: Auto Mix + Balance "did great").
M5 split in rounds: 1 = EQ / Dynamics / Tone / Stereo processing (this), 2 = Space on FX-channel Links + ducking,
3 = Master finishing + Auto Mix proposing EQ / dynamics.
- dsp ProcParams: ONE table of 100 controls (id, name, kind number/toggle/choice, range, default, log curve, step,
  unit, choices); enum Id order = registry index = procspec.js order. Never reorder released ids; add at the end.
  Modules (audio order): gate, eq, comp, multi, trans, tone(balance), loud, harm, analog, clip, stereo(width),
  final, limit; UI module keys via moduleKey(); every module's on-switch defaults OFF (chain_on defaults ON).
  Meters struct (13 floats: in/out peaks, comp, multi x3, trans signed, gate, limit, clip, glue) + running bits.
- dsp ProcDsp: RBJ design() (same formulas as the UI's JS), Filter2, LR4, Ballistics, Compressor, AutoComp
  (threshold from 2 s average level + 4 - depth*amount, ratio 1 + span*amount, 60 % auto makeup; amount 0 = unity),
  TransientShaper (fast/slow envelope differences), Gate (3 dB hysteresis, hold) / 2:1 expander, HalfbandIir
  (HIIR polyphase design; up: both paths get x, down: path0 <- in1, path1 <- in0, 0.5 * sum) + Oversampler (12/6/4
  coefs, tbw .025/.12/.18; 1x-8x), LookaheadLimiter (fixed latency = ceil(5 ms) + 12 TP lag; sliding min over look+1,
  release, box average, gain delayed by maxLook - look; true peak via TruePeakDetector; final clamp; feed() keeps its
  delay line fresh while off).
- dsp ProcChain: 32-sample chunks in double; params glide 30 ms (log for log params); module presence w fades 20 ms
  (dry/wet blend; limiter blends its gain); all off -> early return, buffer untouched (bit-exact). EQ modes:
  Smooth = Q*0.6 + 12 dB/oct cuts, Natural = bell Q*clamp(.45+|g|*.09,.5,1.6). Multiband: LR4 tree + all-pass on
  the low band. Stereo: side in 3 LR4 bands, mid through the same all-passes (so 100 % = phase only, L/R relation
  kept). Harmonic: pre/post tilt shelves (exact inverses) around 2x OS tanh odd/even + DC blocker. latencyFor().
  CPU (48 k, O2): each module 0.4-1.7 % of a core; EQ + comp ~1.5 %.
- Registry layout 5 (kProcSlots 192): procCount, procNow[], per-control procRequestSeq/procRequest (value then
  bump; Link learnProcRequests() right after claim, takeProcRequests() at 30 Hz), procMeters (audio thread per
  meter window), procRunning, sampleRate, latency. LinkInfo.hasProcessing/procRunning/sampleRate/latency.
- plugin/ProcHost.h (shared by both plug-ins): addParameters (AudioParameterFloat/Bool/Choice, log skew centre
  sqrt(min*max), value texts), Raw (lock-free read), set() (gesture + notify). Link params version hint 3, Mixology 1.
- Link: chain after Trim + Assist EQ; timer 10 -> 30 Hz; applies requests as host params; setLatencySamples when
  the limiter switches; window 600x536 with PROCESSING strip (chips, live = green) + On/Off (chain_on).
- Mixology: same chain on the Stereo Out ("mix" target). analysisRing BEFORE the chain (Analyze Mix / routing by ear
  hear the sum of the tracks), meters/spectrum/scope AFTER. Params saved as an APVTS child of MixologyState.
  monoListen atomic (Mono Check): never saved, off on setPage != stereo and in the editor's destructor.
- Editor: native procTarget(key), procSet(key, {id: v}), setMonoListen; keys "mix" | "link:<slot>:<gen>"; frame.proc
  (values, meters, sr, channels, latencyMs, track spectrum (31 bands -> 120 pts, mix units), track corr) on
  eq/dynamics/tone/stereo pages; frame.procTargets every 15 frames; frame.mono.
- UI: proc.js (shared target by NAME in store "procTarget", 30 ms batched sends, 800 ms hold against echoes,
  auto-on when a module's knob moves, reset(mod), powerButton, targetSelect, JS eqBand() = engine's designBand()).
  procspec.js GENERATED by tools/procspec (ctest procspec_in_sync fails when stale: run
  build-tests/tests/procspec plugin/ui/procspec.js --write). moduleList lamps (green on, pulsing live). Knob.dragging.
  Space + Master keep "Preview" until rounds 2 / 3.
- CI green: 38636805323 (M5 round 3 + No pan fix, v0.7.0), 38586762221 (M5 round 2: Space, v0.6.0), 38573237129 (M5 test-1 fixes; Link check now also hosts Mixology.vst3), 38515529754 (HD animated Space scenes), 38514069047 (M5 round 1 + Overview quick knobs = Full Mix EQ band gains, frame.mixEq on the overview).
- Space pictures (user, 2026-10-14: "keep them but increase the resolution ... come alive and animate with the
  beat"): HD scenes = Real-ESRGAN x4plus upscale of the mockup crops (tools/art/upscale_scenes.py, CPU torch in a
  venv, ~1 min each) -> plugin/ui/assets/scene_*.webp 2876x996 (~2.3 MB total; the small .jpg are gone).
  plugin/ui/space_scene.js: canvas scene (cover fit), highlights layer (bright pixels, blurred) added on each
  beat with the effect's decay / pre-delay, per-effect motion (reverb floor ripples + dust, delay echo arches
  down the tunnel at Time with Feedback / Ping-Pong, chorus strip shimmer + light sweep, phaser swirl rings + side
  waves, hall light wave + chandelier sparkle, room kick / snare glow, special colour pulse + orbits + rising
  particles), 40 fps cap, only while the Space page shows (app.js onHide). Beat: PluginProcessor.beat (bpm, ppq,
  bar start, numerator, whenMs from the playhead) -> frame.beat on the space page (ppq extrapolated to now);
  without a host tempo it follows level hits. Today it moves while the song plays; in round 2 it should come alive
  only while that effect is used on an FX channel (the user's wording: "when a Space feature is used in a mix").
- Checks: 14 [proc] tests + registry proc test (105 ctest total, ASan clean); linkcheck: 107 params exposed, EQ
  request -> +6.00 dB, limiter latency 252 samples at 48 k, off again bit-exact; pluginval 10 local (Linux) passed
  for both plug-ins.

### M5 user test 1 (2026-10-14, video) and the fixes
- Power buttons dead with a real (100 ms+) click: proc.js redrew the button's innerHTML on every 30 Hz frame, so the
  pressed element was gone at mouseup (Chromium drops the click). Fixed: built once, text/class updated only on
  change. Same guard for the Tracks padlock and the Analyze button. Playwright test with 160 ms clicks: old code fails,
  new passes.
- Tone "did nothing", Stereo "no difference": linkcheck now hosts the real Mixology.vst3 (2nd arg, CI too): Warmth +6
  -> +5.80 dB at 60 Hz, Width 200 % -> side +6.02 dB, state round trip. The user's mix is MONO at the Stereo Out, so
  width on Full Mix can't work: Stereo page says so (corr > 0.985).
- Guitars "L100" (mono Links, heard in both ears): channels without a working pan (mono track into a mono group).
  BridgeSession watches every pan write (setPan, plans; kPanLandSeconds 1.2): Cubase not following -> noPan set,
  log line, checkWritable refuses pan, plans skip it; hub slot "noPan" -> strip shows "No pan"; channelsNow
  panUnknown. Global "mixMono" (report.monoMix) dims pans with a tooltip.
- Auto Mix 7 -> Undo -> 3: undo steps' fromDb now = the target as Cubase can take it (capped at +6, -inf), test added;
  hub "plan" refuses while bridge.applying() (a list opened mid-undo saw half-moved faders).
- Space: delay echoes travel the whole tunnel in perspective (4 beats of echoes, faint ones kept >= 22 %), hall wave
  expands to the far corner each beat and lights the hall's own lamps in its ring, Special FX crystal turns a
  20-60 deg step per beat (spinPatch: round feathered patch), beams, shock waves (Feedback), sparks, bar flash.

## Milestone 4 status (v0.4.0): TESTED by the user, follow-ups done (docs/M4_TEST.md)
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

### M4 user test 1 (2026-10-13, Rock - Punk / Pop Punk song, video + MP3 of the raw mix) and the fixes
- User: Balance raised the vocal ("vocals way louder than music"), Auto Mix "lowered everything but the vocals,
  sounded worse", Clean Up / Pocket "nothing to change but my vocals are too loud", Undo needed close/reopen,
  steps 7, 8, 10 worked, muted tracks still show in the analyzer (Links hear muted tracks: confirmed).
- MP3 analysis (demucs htdemucs in a scratch venv): vocal vs rest median -6.4 LU (Mixology said -8.2), vocal vs mix
  -9.1 LU, mix -7.5 LUFS, +6.4 dBTP. So the measurement was right; the user's taste differs from the genre
  target (Gerdes & Siedenburg 2023: commercial rock ~0 dB, pop/country/rap positive). The export is exactly dual
  mono (L == R) although Cubase reported the four Electric tracks at L100, and Mixology saw no lean: likely
  stereo tracks with the Combined Panner (bridge pan = one side) or a mono export. Asked the user.
- Fixes: vocal taste ("Too quiet / Right / Too loud", hub vocalTaste -> Style.vocalOffset + vocalFromYou via
  Analysis::setVocalOffset, saved in the processor state "prefs"); vocal-low is "info" unless the user set a
  taste; Balance: vocal step max 4 dB, loudness-neutral top-up (>= 1 dB, not past fader tops, not around locked
  groups), headroom separate (Plan.headroomDb / headroom list, UI tick, hub adds it to every playing fader);
  one-track drum kit (perc without kick/snare/hats) weighs like the kick; Stereo Architect refuses when the pans'
  expected lean differs > 6 dB from the measured mix balance (Combined Panner note); BridgeSession tracks mute /
  solo per channel (isAudible); hub skips muted / soloed-out Links in analysisTick and roleCurves (reference
  kept); Undo re-plans the sheet when done; the wrong "protected" note when the vocal goes down is gone.
- User confirmed (2026-10-13): the Electric tracks are centred in Cubase. So the bridge's "L100" was the Stereo
  Combined Panner's left edge (MIDI Remote mPan reports only that). Now: a paired stereo Link + pan value 0 ->
  slot.stereoPanner (strip shows "Stereo", knob refuses with a toast) and ChannelNow.panUnknown (Stereo Architect
  counts it centred and never moves it). Limitation: a narrowed Combined Panner (e.g. L50 / R50) reads "L50".

### M4 user test 2 (2026-10-14): Auto Mix + Balance applied, then reopened: same moves again
- Screens: guitars 0 -> -12 -> (proposed) -18, Drum Machine to +6 (fader top), vocals +3.1, Beat -3.1. Mixdown MP3:
  -6.7 LUFS, +8.9 dBTP; BOTH exports exactly dual mono (L == R): export setting or the Stereo Out (asked).
- Causes and fixes: plans used the stale report -> ChannelNow.faderAtAnalysis (hub snapshot at finishAnalysis),
  planner corrects group energies, vocal ratio, mix peak and masking by the fader moves since; vocal correction in
  one go (no 4 dB steps: reopening found another step); instrument balance skipped when unlinked channels carry
  > 25 % of the linked instruments' energy (the Beat): the guitars were judged against the Drum Machine alone;
  Stereo Architect pans mono tracks only (ChannelNow.channels from the Link); plan steps check each field (volume /
  pan) separately and undo steps only revert a field still where the plan put it (checkFrom / checkPan).
- User: the guitars "moved to L100", "I moved them to center and it moved them back": the strip showed the Combined
  Panner's left edge (L100) and Cubase kept reporting it; the knob never writes by itself (Knob.set doesn't notify).
  The "Stereo" label needs the newest build (asked which one they ran).

### Music group (2026-10-14): the user's "Beat" is a GROUP of all non-vocal tracks (for Voxology's beat detect)
- So Beat was double counted (an "unheard" channel = extra music) and moved on top of its own member tracks.
- TrackSettings.musicBus (5th field of "tracks" state), hub command setMusicBus {hash, value} (one per project),
  strip menu item "Music group" (gold "glue" icon). Analysis::setMusicBus(present, faderDb at analysis): adds the
  group fader to every non-vocal / non-adlib / non-fx track (viaMusicBus) in loudness, bands, vocal ratio, masking,
  unheard residual; the group never counts as unheard (and is excluded if it has a Link). Planner: Lookup.musicBus;
  groups viaBus when the group fader can move: the vocal step and the loudness top-up move the group fader (one
  change) instead of every instrument; instruments only move for their balance against each other; headroom
  moves the group + top-level channels; group fader moves since analysis correct the instruments' levels.
- The unheard advice now suggests "Music group" for a channel the other tracks play through.
- Auto-detection (user: "I may not always name it Beat"): MixAnalysis sets Report.groupHints when the Links explain
  the mix in > 80 % of the playing moments (residual < 10 %): those unheard channels aren't counted as music
  (no unheardLoudness) and the "group-hint" suggestion + Plan.groupHints (Balance / Auto Mix) ask "Do your
  instruments play through it?" -> setMusicBus { name } (hub resolves the name via knownFaders). Routing itself
  isn't available to MIDI Remote scripts, so the user confirms.
- User: "It should be able to determine groupings and not count them as redundancies." -> routing by ear in
  MixAnalysis::evaluate: weighted least squares (relative error) of the Stereo Out band energies (bands 3-29,
  within 40 dB of the moment's top) as Gv * vocals + Gi * instruments (Link bands + track faders). Accepted when
  the median error < 1.5 dB and < 10 % of points have > 3 dB unexplained (a printed beat fails this). Then
  Report.routingFromSound, vocalPathDb, musicPathDb: every track's loudness / bands, the vocal ratio, masking and
  the unheard residual include the path (group faders / group compression / pan law), groups are groupHints, never
  music. The manual music group then only decides WHICH fader carries the music's moves (optional question).
- User test 3 (2026-10-14): Auto Mix + Balance "did great", "Too loud" made the lyrics much better. Remaining:
  Electric tracks (MONO Links) read L100 though heard in both ears, Stereo Architect offered L100 -> R100.
  Both exports were exactly L == R: the mix is mono at the Stereo Out, most likely Beat is a MONO group (asked).
  Fix: Report.monoMix (mix width < 0.002 and correlation > 0.998 on average) -> "mono-mix" advice (names stereo
  tracks that arrive in mono) and Stereo Architect leaves pans alone; pan-vs-mix sanity threshold 6 -> 3 dB.
- Restore now covers the Assist EQ (user agreed): dsp AssistRestore (first "before" per track, last "set"; decide():
  restore / keptYours when changed since / none; same() tolerant to parameter steps). Hub: noteSet on planApply,
  noteUndo on planUndo, restoreAll + strip restore request the old EQ (protected skipped, Link missing keeps the
  point), forget clears; changedCount = unique channels with fader or EQ changes (+ changedFaders / changedEq for
  the tooltip); slot.changed includes EQ. Session-only like the fader restore points.

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
Restore with Assist EQ green run: 38508823383 (mono mix included). Routing by ear green run: 38498631172. Music group green run: 38496765368. M4 fixes green run: 38483054140. M4 green run: 38476297775 (77 tests on Windows, Link check with Assist EQ, pluginval 10 + validator, both plug-ins).
M3 green run: 38461991599 (57 tests on Windows, Link check with analysis, pluginval 10 + validator, both plug-ins).
M2 green run: 38444281357 (Windows build, unit tests, Link cross-library check on Windows, pluginval 10 +
VST3 validator for BOTH plug-ins). Artifact Mixology-Windows has VST3/Mixology.vst3 + VST3/Mixology Link.vst3.
Every push: engine + bridge simulator under ASan/UBSan (Linux), Windows build + tests + package + pluginval 10 +
Steinberg VST3 validator. First green run: 38407455828 (v0.1.0). Gotcha fixed there: getProgramName must not be
empty (validator: "Program 000 has no name").
Download: Actions -> latest green run -> artifact "Mixology-Windows" (VST3, Standalone, Cubase MIDI Remote/Mixology,
MANUAL.md, M1_TEST.md, INSTALL.txt).

## Next
1. User runs docs/M5_TEST.md (copy BOTH VST3s: registry layout 5) and reports. Fix what they hear.
2. M5 round 2: Space on an FX channel's Link (reverb, delay, chorus, phaser, hall, room, special FX) + its EQ +
   ducking under the vocal (vocal Link publishes an envelope; FX Link reads it from the registry).
3. M5 round 3: Master page finishing (glue, tilt, width, trim on Mixology), Auto Mix proposing EQ / dynamics.
   Open question from M4: is the user's Beat a mono group (one meter in MixConsole)? Maybe: restore points saved
   with the project.
