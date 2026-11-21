# Sonicology Rack: the plan

Written 2026-11-14, before any code. Stage 1 is the next build; it starts when the user says so.

## What we decided (the user, 2026-11-14)

- The Rack is **a view, not a fourth plug-in**. Voxology, Mixology and Masterology stay exactly as they are and
  keep doing the processing. The Rack shows every Sonicology plug-in in the project in one place: which modules
  run on each track, what they are set to, and how the three plug-ins work together.
- **All three plug-ins open it**, from a new **RACK** button on each one's current screen. That button is the only
  change to the three locked looks.
- **Stage 1 first: see everything.** Stages 2 and 3 are planned below and built later.
- **The look is the concept art the user picked** (`concepts/rack-main.png`, `concepts/rack-modules.png`,
  `concepts/rack-coach.png`): the gold honeycomb frames, navy glass panels, red / blue chrome, gold knobs, hex
  chain, the crystal logo. The layout changes only where the function needs it (listed below), and the user approves
  screenshots before anything is wired.

Earlier ideas that were turned down, so they don't come back: a separate Rack plug-in that hosts or replaces the
three (it would duplicate Voxology on vocals, cut Auto Mix off from the tracks that use it, and need the smart
features built twice); dragging modules between plug-ins (the DAW decides the order of plug-ins on a track).

## What it can and can't do (the honest limits)

- **No reordering across plug-ins.** On each track the order is the DAW's insert order. The Rack shows it; it can't
  move Voxology's De-Esser after Mixology's EQ. Each plug-in's own chain is fixed too (Voxology's 13 cells,
  Masterology's 12, Mixology's processing order).
- **Only Sonicology plug-ins.** A third-party plug-in or one of Cubase's own between them doesn't show.
- **The Rack doesn't know the DAW's insert order between two Sonicology plug-ins on one track.** It shows the order
  the manuals ask for (Honey Tune on the clip, then Voxology, then Mixology Link last). Stage 2 can check it from the
  levels (Voxology's output against the Link's input) and warn.
- **It can't open another plug-in's window** (no plug-in format allows it). It says where to go ("open Voxology on
  'Lead Vox'").
- **One project at a time:** like the suite link, only plug-ins of the project that plays take part.
- **Same process only:** hosts that run each plug-in in its own process (Bitwig's sandbox, a bridged plug-in) keep
  the plug-ins apart, as they already do for the suite link.
- **All plug-ins must be the new version** to show up (the shared block's name carries its layout). An older one is
  missing from the Rack; the Rack can't tell it is there.

## The modules that exist (signal order)

- **Honey Tune** (Voxology's extension, on the clip, before every insert): tuning. Shown with Voxology's Pitch
  (which rests while Honey Tune tunes the track).
- **Voxology** (13 cells): 01 Pitch, 02 Cleanup, 03 Tone EQ, 04 Dynamic EQ (with Smooth), 05 De-Esser, 06 Rider,
  07 Compressor, 08 Saturation, 09 Voices, 10 Dimension, 11 Delay, 12 Reverb, 13 Output. Beat mode: the beat track
  (only listens for the key).
- **Mixology Link** (each track; mostly off until a tool or AUTO turns them on): the Assist EQ (Clean Up's low cut
  and mud dips, Vocal Pocket's dips), Gate, EQ, Compressor, Multiband, Transient, Tone Balance, Loudness, Harmonics,
  Analog Touch, Clip Control, Stereo, Final Touch, Space (up to three of Reverb, Delay, Chorus, Phaser, Hall, Room,
  Special FX, with ducking), Limiter. Plus the Trim.
- **Mixology** (the Full Mix, on the master channel, before Masterology): the same processing modules on the whole
  mix, Final Output (Make Room).
- **Masterology** (12): Unlimiter, Tonal EQ, Stem EQ, Tone Steady, Resonance Tamer, Bass Control, Glue, Dynamics,
  Stereo Imager, Saturation, Soft Clipper, Maximizer. Plus Input Trim, loudness target and ceiling.

(The concept's module browser had 24 cards with a few wrong sources: Dynamic EQ is Voxology's, Width is Masterology's
Stereo Imager / Mixology's Stereo, Limiter is Mixology's. The real list above is what the Rack shows.)

## How it works (technical, short)

The plug-ins already meet in shared memory (the suite link, `SuiteLink.h`; Mixology Link's registry; Voxology's Honey
Tune link). The Rack gets its own block next to them, so nothing the suite already does changes:

- **New shared files, the same in all three repos** (added to the "Shared files match" job in `sonicology.yml`):
  `dsp/include/suite/RackLink.h`, `dsp/src/RackLink.cpp` (the block, framework-free C++17, lock-free like
  SuiteLink), `plugin/ui/rack.js`, `plugin/ui/rack.css` and the art in `plugin/ui/rack/` (the view), and
  `tests/test_rack_link.cpp`.
- **Block** "SonicologyRack.v1.<pid>": one slot per plug-in instance (Voxology, Mixology, each Mixology Link,
  Masterology; room for 256). Each slot: which plug-in, its version, the track name and hash (the same hash as the
  suite link), its role (vocal, beat, track, full mix, master), on / bypassed, latency, in / out levels, and its
  modules in signal order. Each module: a key ("vox.pitch", "link.comp", "mas.glue"), its state (on, off, idle, resting,
  set by Mixology), a one-line summary the plug-in writes itself ("Natural · 50 ms · F minor"), a meter (gain
  reduction or level), up to 8 values for the knobs (name, value, unit), and for EQs their bands (type, Hz, dB, Q) so
  the view can draw the curve. Plus up to 4 "together" lines the plug-in knows ("Pitch rests: Honey Tune tunes this
  track", "Mixology treats this track as a vocal").
- **It costs nothing while nobody looks:** an open Rack view refreshes a viewer heartbeat in the block; the plug-ins
  publish module details only while one is fresh (else just who and where they are, once a second).
- **Writers:** each plug-in's message thread, a few times a second (meters from the audio thread as atomics), with
  the suite link's rules (Presence: only while the host runs it; a slot silent for 2 s is gone).
- **The view** is shared HTML / JS: each plug-in serves `rack.js` / `rack.css` / the art with its own page and gives it
  one native function, `getRack()`, which returns every slot as JSON (about 10 times a second while the Rack is open).
- **The "together" checks across plug-ins** (two reverbs on one vocal, two tuners, a vocal without a Link, a shared
  track name, room made for Masterology, Masterology's target against the mix) are worked out in `RackLink.cpp`
  (`rack::together (slots)`), so Catch2 tests cover them; the view only shows them.
- **The art:** the user's art files (`art/`, see "The art files" below) for the logo, wordmark, chrome bars, frame
  corners, hex cells and knobs; CSS and SVG for the rest (panel bevels, buttons, pills, toggles, knob arcs, meters,
  graphs, text). Trimmed and saved as WebP, sharp up to the largest window on a 2x (Retina / 4K) screen (the cube emblem 860 px, the title 2092 px, the honeycomb tiles at 2x): about 0.9 MB in each plug-in. It will look like the concepts,
  not pixel for pixel: the concepts are AI art at 1448 x 1086.

## Stage 1: see everything

### The RACK button (the one change to each locked screen; spots shown for approval)

Measured in step 1 (the first spots didn't fit: Voxology's top and bottom bars have no free space, Mixology's tab row
is full); each shows the Rack's crystal emblem:
- **Voxology**: a RACK pill beside MY CHAINS, in the row under INTENSITY (the two share that row; nothing else moves).
- **Mixology**: one more icon tab after PRESETS, like History / Settings / Help (the emblem); the text tabs get a few
  pixels narrower to make room.
- **Masterology**: a RACK pill (MATCH's style) before the gear and ?.
- Mixology Link: a RACK button too, in stage 2 (decided below). Honey Tune: none (it shows up in the Rack).

The Rack opens over the plug-in's own page in the same window; a "BACK TO VOXOLOGY / MIXOLOGY / MASTEROLOGY" button
returns. The windows differ (Voxology 1280 x 720 and Masterology 1440 x 810, both 16:9; Mixology 1152 x 864, 4:3; all
resizable, Masterology keeps its 16:9): the view is designed at the concept's 4:3 and widens its side panels in the
16:9 windows.

### Page 1: RACK (concept `rack-main.png`)

- **Top bar:** the logo and wordmark (subtitle "YOUR WHOLE SUITE IN ONE VIEW", decided below); the **track picker** where the concept has "Trap Vocal Chain" (‹ Lead Vox ›,
  then Full Mix and Master at the end); the page picker (RACK / MODULES; COACH arrives in stage 3); BACK TO ... .
  The concept's A/B, BYPASS, UNDO / REDO, SAVE CHAIN, ANALYZE and power buttons arrive when they work (stages 2 and 3);
  until then that row holds the suite status ("Voxology 3 · Mixology Links 9 · Masterology 1").
- **Left, SIGNAL CHAIN:** the picked track's real chain, top to bottom, grouped by plug-in with a header each (HONEY
  TUNE, VOXOLOGY, MIXOLOGY LINK): icon, name, ON / OFF / RESTS / IDLE pill (read-only in stage 1), no drag handles.
  Modules that are off fold into one "+ 9 off" row per plug-in so the list fits.
- **CHAIN FLOW** (the hex row): the modules that are on, in order, ending with FADER → FULL MIX → MASTER hexes that
  jump to those chains. Click a hex or a row to pick the module.
- **Centre, the module:** the concept's Dynamic EQ panel for whatever module is picked: its name, "in Voxology on
  'Lead Vox' · 4th of 13", its graph (EQs: the real curve; dynamics: gain reduction over the last seconds; others:
  level), and its main controls as gold knobs showing the real values (they turn in stage 2).
- **Right, RACK COACH** (stage 1 content): HOW THEY WORK TOGETHER (the real lines for this track: "Voxology
  compresses this vocal, so Mixology's Link adds no compressor", "Pitch rests: Honey Tune tunes this track", warnings
  like "two reverbs on this vocal: Voxology's and the Link's Space") and WHAT THIS MODULE DOES (a short text per
  module, from the plug-ins' Learn / manual text).
- **Bottom bar:** IN / OUT levels of the picked plug-in and LOUDNESS (I / S / TP) of the mix as Mixology and
  Masterology measure it (already shared).

### Page 2: MODULES (concept `rack-modules.png`)

- Search and the concept's filter chips (Vocal, Mixing, Mastering, Utility, Creative, Dynamics, Space).
- Cards for every real module (the list above), each tagged "from Voxology / Mixology / Masterology", with where it
  runs in this project ("on in 3 tracks"); modules of a plug-in that isn't in the project show dim with how to get them
  ("add Voxology to a vocal").
- Left column: categories (as the concept) and IN THIS PROJECT (tracks with their plug-in badges) where the concept
  has RACK TEMPLATES.
- Right panel: the concept's module card (what it is, ideal uses, key controls) plus WHERE IT IS ON (click a track:
  the RACK page with that module picked). ADD TO CHAIN / ADD AFTER SELECTED / FAVORITE and the bottom buttons are left
  out (nothing can be added across plug-ins); stage 2 adds TURN ON HERE where a plug-in has the module.

### What each repo gets in stage 1

- **All three** (shared): RackLink.h / .cpp, rack.js / rack.css / art, test_rack_link.cpp; the shared-files job
  lists them.
- **Voxology:** publishes its 13 cells (on / idle / resting, summaries, knob values, Tone EQ / Dynamic EQ bands,
  meters), Honey Tune on its track (from the Honey link), the Mixology answer it already reads; RACK button; serves
  the view; tests for its module list. Version 0.55.0.
- **Mixology:** the Full Mix publishes its modules and Final Output; **each Mixology Link** publishes its Assist EQ
  and modules (so the Rack works even without the main Mixology); RACK tab; serves the view; tests. Version 0.26.0.
- **Masterology:** publishes its 12 modules, Input Trim, target and ceiling; RACK button; serves the view; tests.
  Version 0.31.0.
- **Manuals:** a "Sonicology Rack" section in all three (Masterology's manual: update the Claude Docs copy too).
- **Release:** Beta 14 (all three, one installer per platform as now).

### Build order (with approval gates)

1. **Art and a still view (gate: the user approves screenshots).** Rebuild the concept look in `rack.css` / SVG with
   the user's art files, fill it with a made-up project (`rack_mock.js`: Lead Vox with Voxology + Link, Chorus Vox with
   Honey Tune + Voxology + Link, Beat in Beat mode, Drums and Bass with Links, Full Mix, Master), screenshots of both
   pages at each plug-in's window size, and of the RACK button on each screen.
   **Built 2026-11-15, waiting for the user's OK:** `plugin/ui/rack.js`, `plugin/ui/rack.css`, `plugin/ui/rack/` (art
   and font, made by `tools/rack/make_art.py`), `tools/rack/preview.html` + `tools/rack/rack_mock.js` (the made-up
   project; open the page in a browser: `?host=voxology|mixology|masterology&page=modules&track=master&unit=...&key=...`).
   rack.js / rack.css are in Mixology's UI folder (its build embeds them, unused until step 4); the other repos get the
   shared files in step 4.
2. **The shared block** (`RackLink`) with its tests, Release and ASan / UBSan.
   **Built 2026-11-15:** `dsp/include/suite/RackLink.h`, `dsp/src/RackLink.cpp`, `tests/test_rack_link.cpp`, the same in all
   three repos (in the suite's "Shared files match" job). Block "SonicologyRack.v1.<pid>", 256 slots (about 4 MB of
   address space; only the slots in use are touched): each a Unit (product, role, track, levels, latency, up to 16
   modules with state / summary / "set by" / meter / 8 values / 8 EQ bands / Space layers, 4 own lines, a 128-bin
   spectrum). `claim` / `publish` / `release` like the suite link (seqlock, heartbeat, take-over after 2 s); `view` /
   `watched` (the viewer heartbeat: details only while a Rack is open); `units`; `together` (two compressors / reverbs /
   delays on a vocal, a vocal without a Link, Honey Tune with Pitch resting or not, tracks sharing a name, the Beat,
   two Mixologys or Masterologys, Mixology's Limiter before Masterology, no Mixology on the master, Links without
   Mixology); `toJson` (locale-independent, finite numbers, the spectrum normalised for the EQ graph). Checked end to end:
   units published through the real process block, read back, turned into JSON and shown by rack.js.
3. **Each plug-in publishes** its modules (tests per repo).
   **Mixology done 2026-11-15:** `rack::Publisher` (in the shared RackLink: the slot, the suite link's "only the project
   that plays" rule via suite::Presence, brief about every 0.8 s, full about 5 a second while a Rack view watches);
   `mixology/RackUnit.h` (framework-free, tested: a Link's 15 modules / the Full Mix's 14 in signal order, states, summaries
   and knobs written as Mixology's pages write them, EQ and Assist EQ bands, gain-reduction meters, Space's effects and
   ducking); each Link and Mixology publish from their timers (the Link starts / stops with prepareToPlay /
   releaseResources now). Checked with the real plug-ins in tools/linkcheck (CI's "cross-library check"): both Links and
   the Full Mix appear, brief then full when watched, gone when removed or stopped.
   **Voxology, Honey Tune and Masterology done 2026-11-15:** `vox/RackUnit.h` (the 13 cells in the window's order, idle
   when switched on but set to do nothing, Pitch resting while Honey Tune tunes the track, the Tone EQ's bands, the
   Dynamic EQ's cuts as they happen, the Beat Listener in Beat mode; its own meter copies and the vocal's spectrum while a
   Rack watches), Honey Tune publishes itself, one entry per track with its clips (`honeyui::RackTrack` / `rackModule` in
   HoneyView.h: notes edited, steadied, the key and where it comes from, Snap / Keep Drift / Keep Vibrato), and
   `polisher/RackUnit.h` (the 12 modules, the Tonal EQ's bands, gain reduction for Resonance, Bass, Glue, Dynamics, the
   clipper and the Maximizer; Drive, Input Trim, ceiling and the target in the Maximizer). Tests in each repo; real JSON
   from all three rendered in rack.js (screenshots checked). The Beat line no longer says Beat mode changes nothing
   (Unmask dips the beat under the vocal); unknown levels show a dash.
4. **Wire the view** to `getRack()`, the RACK buttons, the "together" checks.
   **Done 2026-11-16:** `rack::viewJson` / `rack::overview` (shared, tested: the viewer heartbeat, every live unit, the
   together lines, the mix's loudness and Masterology's goal from the suite link); each plug-in's window has
   getRack / getRackTheme / setRackTheme (the light / dark choice is one file, Sonicology/rack-theme.txt in the user's
   app-data folder, shared by all three) and its RACK button at the approved spot (Mixology an icon tab after PRESETS,
   Voxology a pill beside MY CHAINS, Masterology a pill before the gear: its full header row takes a little width from
   the logo); rack.js / rack.css / art served flat in all three and checked by the shared-files job. Checked in each
   window's browser preview (opens, starts on the plug-in's own track, theme kept, BACK returns) and Masterology's UI
   smoke test.
5. **Manuals, versions, CI green in all three, Beta 14.** The user checks it in Cubase.

Roughly 3 to 5 build sessions, step 1 first.

### Stage 1 is done when

- In Cubase with all three plug-ins, the Rack opens from each of them and shows every track that has a Sonicology
  plug-in, the Full Mix and the Master.
- A change in any plug-in (a module on / off, a knob, Honey Tune taking over) shows in the Rack within a second.
- The "together" lines and warnings are right for the user's real song (vocal with Voxology + Link, the chorus with
  Honey Tune, the beat, the master).
- With only one or two of the three in the project it still works (and says what's missing).
- No extra CPU while no Rack is open; little while one is.
- CI green in all three, shared files the same.

## Stage 2: quick controls (steps 1 - 2 built 2026-11-16; the rest planned)

- **First: a RACK button on Mixology Link's window** (the user said yes, 2026-11-14; when is ours to pick). It opens
  the Rack straight on that Link's track. Stage 2, not 1, because the Link's window is small (600 x 536): it has to
  grow to the Rack's size while the Rack is open and shrink back after, which needs testing in Cubase, and stage 1 is
  complete without it (the three big plug-ins open the Rack).

- **Turn modules on / off and turn their main knobs from the Rack**, in any plug-in on any track. Each plug-in
  publishes its module's controls (name, range, unit, curve), so the shared view draws the gold knobs for any module.
  **Built (step 2, 2026-11-16):** each knob (`rack::Value`) carries `key`, the control it turns (the plug-in's own
  host parameter id: Mixology's proc ids "comp_thr", the Link's Assist EQ "lowcut" / "mudgain" / "pocketgain",
  Voxology's "cpThr", Masterology's "glueRatio", Honey Tune's "snap" / "drift" / "vibrato"; "" = shown only), and
  `steps` (a choice of that many, 0 = continuous); each module `switchable` (it has its own on switch). Range and curve
  did NOT go into the shared block: the owner turns the Rack's 0..1 back into the value with its own curve, the one
  that placed the knob (`fromKnob()` in each plug-in's RackUnit, tested to round-trip; the module's switch:
  `switchOf()` in Voxology / Masterology, proc::onParam in Mixology). Shown only, on purpose: Pitch's Key (the key it
  follows), the Beat Listener's Key / Scale / Tuning, Voices "MIDI notes", Honey Tune's Key / Scale (a re-tune of
  every clip: chosen in Honey Tune), Masterology's Dynamics bands (gain reduction now), Tone Steady's Target and
  the Maximizer's Target (from the goal). Masterology's Glue "Ignore Lows" knob now sits where the host's curve puts
  it (skew for a 90 Hz centre; it was drawn linear). Tests in each repo check every published key exists in the
  plug-in's parameter code (Voxology: plugin/Params.h; Masterology: PluginProcessor.cpp + ModuleParams.h).
  **Built (step 3, 2026-11-16; Mixology 0.27.0, Voxology 0.56.0, Masterology 0.32.0):** the knobs and switches work.
  rack.js: a turnable knob (its unit has `gen`, no `lock`, the host gave `request`) drags up / down (220 stage px for
  the whole range, Shift 900), a choice knob steps on a click, the module's ON / OFF pill is a button; requests go one
  at a time in order (begin, moves about 25 a second, end); a knob is held where you put it until the plug-in's value
  catches up (2.5 s at most); a refusal shows a toast for 4.5 s and the knob goes back. Native functions rackRequest
  / rackAnswer in all three editors (plugin/RackHost.h, shared: requestFrom / answerFrom over rack::requestJson /
  answerJson). The owners: Mixology + every Link (plugin/RackApply.h: switching a module on while the processing is
  off switches it on too, unless another module would start with it), Voxology (applyRack; locked while Auto-Edit
  listens / analyses), Masterology (applyRack; locked while Auto-Master listens / analyses), Honey Tune (serveRack,
  ten times a second while a Rack is open; applies on a drag's end, its settings are for all its clips). Voxology and
  Masterology run a 25 Hz rackTimer while a Rack is open (their main timers are 4 Hz). DECIDED (changed from the
  plan): Mixology's padlock doesn't lock the Rack's hand (it stops only Mixology's automatic moves; Mixology's own
  pages edit a padlocked track too); it will guard the Coach (stage 3). Preview: tools/rack/preview.html turns knobs
  against rack_mock.js (?lock=1 locks Masterology; its Tonal EQ Air refuses).
- **How:** a request queue per slot in RackLink (the way Mixology already asks a Link to change its Assist EQ): the
  plug-in that owns the module applies it on its message thread like a move of your own mouse, so Cubase's undo and
  automation see it. Protected tracks (Mixology's padlock) and a running Auto-Edit / Auto Mix / Auto-Master are
  read-only meanwhile.
  **Built (step 1, 2026-11-16, RackLink layout 2):** `rack::Request` (Change::value: module key + control key + where
  the knob goes, 0..1; Change::state: module on / off; Gesture begin / move / end / set for a drag). `Link::request
  (slot, generation, ...)` puts it in the unit's queue (32 places, many senders, lock-free; tickets from 1; the slot's
  generation keeps a request from reaching a plug-in that took over the slot) and refuses at once with a reason when
  the unit is gone, locked or its queue is full. The owner serves it from its timer with `Publisher::serve (now, lock,
  apply)`: balanced gestures (a drag left open by a closed view ends after 2 s), moves of one knob in one batch merged,
  everything refused while `lock` is set ("Auto Mix is running"). `Link::answer (slot, generation, ticket)`: pending /
  applied / refused (the plug-in's reason) / gone. Units publish `lock` and carry `generation`; the JSON has `gen` and
  `lock` per unit. Tests: "Rack requests: ..." in tests/test_rack_link.cpp (4 senders + the owner on threads, ASan /
  UBSan / TSan clean). Next: step 2, each plug-in publishes its controls' keys and ranges; step 3, each plug-in's
  serve() + apply() (JUCE: beginChangeGesture / setValueNotifyingHost / endChangeGesture) and the padlock reaching
  the Links (Mixology knows it; the Link must publish it as its lock).
- **The top bar comes alive:** BYPASS (this track's Sonicology plug-ins off, level-matched for a fair before / after),
  UNDO / REDO (the Rack's own moves), A/B (two snapshots of every Sonicology setting in the project), SAVE CHAIN (a
  track's Voxology + Link settings as a chain to load on another track).
- **Insert order check:** from the levels, whether the Link really sits after Voxology; a warning when not.
- **MODULES page:** TURN ON HERE where the track's plug-in has the module.

## Stage 3: the Coach (vocals Beta 26, tracks Beta 28, the master Beta 29, the project view Beta 31; concept `rack-coach.png`)

**Built 2026-11-18 (Beta 26: Mixology 0.34.0, Voxology 0.64.0, Masterology 0.38.0), vocals:**
- The COACH page (rack.js `coachPage` / `coachBar`; header button next to SESSION) on a track with Voxology (not BEAT).
  Its data come from Voxology in `Unit::report` (RackLink layout 4: JSON text up to `kReportBytes` 8 KB, sent with the
  details only, dropped whole when too long); requests go back as `Change::coach` (control `plan` / `apply` / `undo`).
- Voxology: `AutoEditController::startPlan` listens and decides exactly as Auto-Edit does but keeps the plan; the job
  then plays the take through the plan (`VocalChain::render`) and measures it again; `vox::coach::planJson` writes
  heard, issues (regions against `toneTarget`, sibilance against `sibilanceTargetDb`, swing, noise, clipping), the
  13-module chain with each setting's reason, bands before / after / target, measured results and ADJUST FIRST.
  `applyPlan` sets it like an Auto-Edit (one UNDO step; after UNDO the plan can be applied again). STYLE / INTENSITY on
  the page turn Voxology's `aeStyle` / `aeIntensity`; the reference is Voxology's.
- Preview: tools/rack/rack_mock.js simulates it (`?coach=ready` starts on a finished plan).

**Built 2026-11-18 (Beta 28: Mixology 0.36.0, Voxology 0.66.0, Masterology 0.40.0), Mixology's tracks:**
- `mix::makeCoachPlan (hash, report, channels, mix)` (MixPlanner.cpp): AUTO's refusals, then all five pages' rules in
  ONE joint `Rules` (like Polish for one track: a fix goes on the page that suits it, EQ and Tone never both fix it); an
  FX return gets `leadSpace`. `mix::coach::planJson` (TrackCoach.h / .cpp): heard (role, seconds, swing, width, masking,
  rumble, noise), issues (from each change's heard word / phrase), the Link's modules with each setting (from -> to, why),
  bands now / with the plan (the Analyze Mix spectrum + the EQ and Tone Balance responses now / planned, each minus the
  response at analysis; against the track's mids, 1.5 dB / oct tilt), region results, moves, notes.
- Mixology (the mix owner, on the master) serves `Change::coach` with control "plan.<hash>" / "apply.<hash>" /
  "undo.<hash>" / "discard.<hash>" (BridgeHub::rackCoach): listens first through trackListen (forCoach) when the track
  wasn't heard (8 s of it playing, gives up after 90 s); APPLY rebuilds the plan and refuses if it changed (shows the
  new one), then one History step "Coach on '<track>'"; UNDO = undoStep (AUTO's Undo, shared). Its Unit::report
  carries `coachReport(shownTracks)`: {"track":<hash>, kind, state, progress, hearing, need, style, role, heardSeconds,
  heardEnough, analyzed, canUndo, plan}; `rack::viewJson` keeps a report that starts {"track":<shown hash>,.
- rack.js: coachOf returns Voxology's Coach on a vocal it processes, else Mixology's (kind "mix") on a track with a
  Link; the page's setup shows ROLE / STYLE / HEARD; RESULTS adds the moves in words; no SAVE CHAIN.

**Built 2026-11-18 (Beta 29: Mixology 0.37.0, Voxology 0.67.0, Masterology 0.41.0), the master:**
- Masterology: `AutoMasterController::startPlan` listens like Auto-Master (the whole song, or BUILD NOW = `finish`
  after 12 s) and decides the same, keeps the plan; the job renders the loudest 12 s through the planned chain
  (`renderMaster`) and measures it again (`analyseMix`). `polisher::coach::planJson` (MasterCoach.h / .cpp): heard
  (the listen, the song's loudness and range, the loudest 12 s, punch, stereo bass, correlation), issues (7 tone bands
  against the genre's or reference's aim, quiet, peaks, squashed, wide low end, phase), the 12 modules with each
  setting's reason (moves first), bands before / aim / after, results (loudness LUFS, true peak dBTP, punch dB, stereo
  bass %, each band against the aim), notes. `applyPlan` = Auto-Master's apply (one UNDO step); UNDO only while that
  step is still the last one. The analysis rests while the song plays (`ScopedPace`, as Voxology's `vox::pace`).
- Steps: `plan` / `finish` / `cancel` / `apply` / `undo` / `discard`; GENRE / INTENSITY / TARGET turn `amGenre` /
  `amIntensity` / `amTarget`. coachReport: {kind "master", state, progress, heardSeconds, canFinish, hearing, genre(s),
  intensity(ies), target(s), reference, canUndo, plan}.
- Shared RackLink: `Change::coach` requests pass a unit's lock (the Coach's own listen locks the unit; BUILD NOW and
  STOP must still go; each plug-in refuses what doesn't fit: Voxology refuses while Auto-Edit runs from its own window).
- rack.js: coachOf on the Master track (kind "master"): GENRE / INTENSITY / TARGET (REFERENCE when wide), BUILD NOW
  with the seconds heard, RESULTS rows each on their own scale with their unit; STOP for every Coach while it listens.

**Built 2026-11-18 (Beta 31: Mixology 0.39.0, Voxology 0.69.0, Masterology 0.43.0), the project view:**
- WHAT TO FIX FIRST, the SESSION page's right panel: one numbered list for the project, worst first. Mixology's part:
  BridgeHub::fixReport ({"kind":"fix", analyzed, busy, seconds, style, fromProject, items[{id, from "setup" | "mix",
  kind, title, need, text, tracks[names], tool, done}], more}), the report of the Mixology that owns the mix while a
  Rack shows the Full Mix (rack.js asks for "fullmix" on the SESSION page: wantTrack()). rack.js adds the Rack's own
  warn lines (together(), units' lines), the insert-order check and another beta's plug-in; one topic is said once
  (Mixology's setup check and the Rack both notice a vocal without a Link, or a shared track name). Each point: its
  tracks (open on the RACK page), COACH (when the track has a Coach), the one-click fixes, the smart tool's name.
  SESSION rows count what is worth fixing on each track.

**Built 2026-11-18 (Beta 32: Mixology 0.40.0, Voxology 0.70.0, Masterology 0.44.0), PREVIEW:**
- The COACH page's bottom bar: PREVIEW (a ready plan that changes something) -> KEEP PLAN | PLAN / YOURS | BACK TO
  YOURS, and how the two are matched. Coach steps "preview", "flip", "keep", "back" (Mixology: "<step>.<hash>"); the
  report adds "preview" ("plan" / "yours"), "previewGainDb" and "planGapDb" (the plan minus yours, + = louder). The
  louder side is turned down to the other, never up. Voxology and Masterology measure the gap (the clip rendered both
  ways; MATCH's place while it plays); Mixology estimates it (EQ / Tone Balance curves over the heard spectrum) and
  turns the track's Link down (LinkRegistry layout 7). Nothing goes into UNDO / History until KEEP; a preview ends
  (yours back) when no Rack shows the track for 4 s. A save during a preview keeps yours (Voxology, Masterology).

**Built 2026-11-18 (Beta 33: Mixology 0.41.0, Voxology 0.71.0, Masterology 0.45.0), a chain you can reorder:**
- The user's pick ("start with 2"). Changes the plan's "no reordering" only inside one plug-in: the DAW still decides the
  order of the plug-ins on a track. Change::order (control: the module's key or "reset"; value: its place among the
  unit's movable modules); Module::movable / moved ride in the module slot's switchable word (bits 1 / 2), so the
  layout stayed. rack.js: drag a movable row in SIGNAL CHAIN (6 px before it starts, so a click still picks it; only
  among that plug-in's movable rows; a gold line shows where it lands), RESET ORDER under the rows, UNDO / REDO.
- Voxology: vox::InsertOrder (Tone EQ, Dynamic EQ, De-Esser, Rider, Compressor, Saturation); VocalChain dips the
  inserts' output over one 256-sample chunk, switches while silent, comes back over the next (the space's tails carry
  on); render() and a fresh start play the order at once. Saved as the state's "insertOrder" (and in MY CHAINS).
- Mixology: proc::Order (EQ, Compressor, Multiband, Transients, Tone Balance, Loudness, Harmonics, Analog, Clipper,
  Width); proc::Chain fades the modules that change place out through their own weights, switches once all are
  silent, fades them back in. Saved as the processing state's "order", in each Link and in Mixology's Full Mix.
- Masterology keeps its order (Auto-Master's loudness search and the limiter at the end depend on it): it refuses.

**Built 2026-11-20 (Beta 35: Mixology 0.42.0, Voxology 0.72.0, Masterology 0.46.0), GUIDED MIX:**
- The audit's "first ten minutes": a GUIDED MIX button on the SESSION page (nothing happens unless pressed; the user:
  pros just don't press it). Its steps take the left column (SESSION: beside WHAT TO FIX FIRST; COACH: in place of
  SIGNAL CHAIN): setup (the warn points not from Analyze Mix), listen (Mixology's Analyze Mix started from the Rack:
  Coach step "mixanalyze.0", then "mixfinish.0" once it heard kEnoughSeconds, "mixcancel.0"; fixReport's hearing /
  enough / playing), what to fix first, the Coach on each track (guideTracks: lead vocal, vocals, bass, kick, snare,
  hats / drums, beat, the rest; only tracks with a Coach), the master, finished. Step text follows the Coach's state;
  guideObserve marks kept / already set; SKIP; any step clickable; progress in localStorage per project (track keys).

**AI voice / room / noise cleanup, step 1 done 2026-11-20** (the user: shoot-out later, "start step 1"): Voxology's
docs/ai-cleanup/STEP1.md. DeepFilterNet3 (MIT / Apache) cuts held sung notes (speech-trained); nothing open and
sellable removes a bedroom's echo; cost is fine. **Step 2 done the same day** (the user: "teach the AI to handle
singing"; their Cymatics / Looperman packs allowed for training): Voxology's docs/ai-cleanup/STEP2.md. Retrained on a
Kaggle GPU (5.5 h): male singer and female rapper now pass (far better than Voxology's classic Denoiser), the female
singer and the deep male chant still lose some notes. No plug-in code yet. **Round 2** (2026-11-21, the user picked another
round): the chant and the female singer in noise fixed; her quieter held notes on a clean take still get turned down.
**Round 3** (2026-11-21, after finding the root cause): with a plug-in safety net (the AI looks at the take plus a faint
hiss it never adds to the sound) no notes lost on any clean take or the user's takes, 5 % at worst on the hardest
real-noise case. **Step 3** (2026-11-21, the user: build it in): Voxology 0.73.0, Cleanup's Mode Classic / AI; Auto-Edit
picks AI for noisy or roomy takes; the Rack shows Mode. Beta 36.

**Next:** the user's test of Beta 36 (the AI cleanup in Voxology); the
blind shoot-out when they have time.

**The plan as written before building:**

- **AUTO RACK SETUP:** Source (the track's role, from Mixology / Voxology), Style (the song style, already shared),
  Gentle / Balanced / Strong, Reference (Voxology's references for a vocal, Masterology's for the master).
- **ANALYZE & BUILD asks the plug-ins that own the track to run what they already have:** Voxology's Auto-Edit (vocals,
  Honey Tune aware), Mixology's per-track AUTO rules and Analyze Mix, Masterology's Auto-Master. Their plans come back
  to the Rack; nothing changes until you apply (as now).
- **The concept's panels, with real data:** ANALYSIS RESULTS (the measured spectrum, issues marked with their dB against
  the style), RECOMMENDED SIGNAL CHAIN (the modules the plans turn on or change, each with its reason line), WHAT I
  HEARD / WHY I BUILT THIS RACK / ADJUST FIRST (from the plans' own heard / did lines), BEFORE vs PROPOSED (the plan's
  moves drawn on the measured curve), RESULTS as measured numbers ("mud +4.1 → +1.2 dB", "level swing 9 → 4 dB",
  "s peaks −3 dB"): **no made-up percentages** like the concept's "Clarity +32%".
- **BUILD RACK / APPLY TO TRACK** applies the plans through the plug-ins (with undo), SAVE AS PRESET, TWEAK MANUALLY
  (to the RACK page). A project view: what to fix first across tracks (Mixology's Mix Assistant points).
- Needs stage 2's request queue. Analysis needs playback, as in the plug-ins today.

## The user's answers (2026-11-14): nothing open

1. **Art files:** received (`art/`). Nothing else is needed to start; "The art files" says what is drawn in CSS / SVG.
2. **Buttons that work only later** (A/B, BYPASS, UNDO / REDO, SAVE CHAIN, ANALYZE, power): left out until they work,
   not shown greyed. Stage 1's top bar holds the track picker, the page picker, BACK TO ... and the suite status.
3. **The subtitle changes** (the user let us pick): **YOUR WHOLE SUITE IN ONE VIEW**, two-tone like the original
   ("YOUR WHOLE SUITE" in the cyan, "IN ONE VIEW" in the white), letter-spaced the same. The old subtitle line is cut
   off the lettering art and the new one set as text.
4. **Mixology Link gets a RACK button too** (the user said yes; when is ours to pick): first thing in stage 2.

## The art files (received 2026-11-14, `art/`)

Seven PNGs from the user, real transparency, clean edges (checked on magenta), 1254 x 1254 or 2172 x 724. Originals are
kept here; step 1 trims them, sizes them to 2x and saves WebP into `plugin/ui/rack/` (shared in all three repos, about
about 0.9 MB together with the rest of the art; the plug-ins already show WebP, also on macOS 11).

| File | What it becomes |
|---|---|
| `01_sonicology_rack_logo_emblem.png` (crystal cube in gold orbits) | the logo at the top left |
| `02_sonicology_rack_lettering.png` (chrome SONICOLOGY + red RACK, with the subtitle) | the title; its subtitle line is cut off and "YOUR WHOLE SUITE IN ONE VIEW" set as text |
| `03_red_blue_header_art.png` (red / blue chrome bar with a dark middle) | the top bar's plate and the bottom meter bar, stretched by its straight middle (CSS border-image) |
| `04_honeycomb_tile.png` (one cream glass hex in a gold frame) | not used: the user wants the honeycomb exactly as in the concept screens (2026-11-15), so the honeycomb backgrounds are made from the concept art itself (below) |
| `05_gold_knob.png` (gold knob, blue ring, gold tabs, a pointer) | every knob: cut into a still bezel (tabs and ring) and a turning cap (the disc with the pointer), the value arc drawn in SVG over the blue ring |
| `06_hex_cell.png` (navy hex, gold frame, red / blue inner glow) | the CHAIN FLOW hexes; "picked" adds the concept's red glow, "off" dims it (CSS) |
| `07_frame_corner.png` (red / blue chrome over a gold corner) | the panels' corners (mirrored for all four), joined by a gold CSS edge |

**The honeycomb backgrounds** (`rack_honey_glass/dark/ivory.webp`, made by `tools/rack/make_art.py` from
`concepts/rack-coach.png`): the concept's own glass cells (cut from the clean honeycomb under its left column) set in a
regular lattice that tiles without a seam, with gold walls drawn in the concept's colours. Glass: the left column under
the list (in its gold frame), the Chain Flow strip, the Modules page's strip. Dark (gold walls on navy, warm glass in the
corner): behind the logo. Ivory (thin lines of shaded gold, like the glass honeycomb's walls): the knob panel. Side-by-sides with the concept were
checked.

**The light view** (the user, 2026-11-15): clicking the cube switches between the dark view and a light one: divine white
and ivory with a pearly sheen and gold instead of the dark blues; the gold, the red / blue chrome, the ON pills, meters
and EQ colours stay. The Chain Flow hexes stay navy with their glowing icons (the user's choice). Its own art: `rack_honey_pearl`, `rack_bar_pearl` (the chrome bar with a pearl
inside), all from `make_art.py`. The Rack keeps the choice in the browser storage when it can; in step 4 each plug-in
passes its saved choice (`theme`) and saves the new one (`onTheme`) with its settings, so it survives everywhere.

**The EQ graph** (the user, 2026-11-15: "just like the original"): the concept's drawing. A gold glowing curve; the area
between it and 0 dB filled in the colour of the band that shapes it (gold where a band lifts, red where it cuts 2 dB or
more, blue for gentle cuts and low / high cuts); round glossy points; a red line down from a hard cut with the spectrum
glowing red under it; the spectrum as a filled glowing blue area with a spiky edge; scale +12 to -18 dB; the bands as
coloured pills under the graph (BANDS: 1 Body, 2 Mud ...). So each plug-in publishes, per EQ module, its bands (type, Hz,
dB, Q, name) and, where it has one, a spectrum (dB per bin on the graph's scale, 0 = the 0 dB line).

Drawn in CSS / SVG (no file needed): the panels' gold bevels and navy glass, buttons and pills, ON toggles, meters,
graphs, the swooshes behind the top corners (from 03 and 07), text.

## Files

- `concepts/rack-main.png`, `concepts/rack-modules.png`, `concepts/rack-coach.png`: the picked look (1448 x 1086).
- `art/`: the user's art files (originals, with their README).
- `concept-chat-2026-11-13.md`: the earlier concept chat that produced them (its "separate plug-in" plan was
  replaced by this one).
