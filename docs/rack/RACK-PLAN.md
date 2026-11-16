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

## Stage 2: quick controls (planned, not built)

- **First: a RACK button on Mixology Link's window** (the user said yes, 2026-11-14; when is ours to pick). It opens
  the Rack straight on that Link's track. Stage 2, not 1, because the Link's window is small (600 x 536): it has to
  grow to the Rack's size while the Rack is open and shrink back after, which needs testing in Cubase, and stage 1 is
  complete without it (the three big plug-ins open the Rack).

- **Turn modules on / off and turn their main knobs from the Rack**, in any plug-in on any track. Each plug-in
  publishes its module's controls (name, range, unit, curve), so the shared view draws the gold knobs for any module.
- **How:** a request queue per slot in RackLink (the way Mixology already asks a Link to change its Assist EQ): the
  plug-in that owns the module applies it on its message thread like a move of your own mouse, so Cubase's undo and
  automation see it. Protected tracks (Mixology's padlock) and a running Auto-Edit / Auto Mix / Auto-Master are
  read-only meanwhile.
- **The top bar comes alive:** BYPASS (this track's Sonicology plug-ins off, level-matched for a fair before / after),
  UNDO / REDO (the Rack's own moves), A/B (two snapshots of every Sonicology setting in the project), SAVE CHAIN (a
  track's Voxology + Link settings as a chain to load on another track).
- **Insert order check:** from the levels, whether the Link really sits after Voxology; a warning when not.
- **MODULES page:** TURN ON HERE where the track's plug-in has the module.

## Stage 3: the Coach (planned, not built; concept `rack-coach.png`)

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
