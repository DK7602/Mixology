# Mixology build plan

Mixology is the intelligent multitrack mixing engineer for the suite:
**Voxology** makes each vocal great, **Mixology** makes all the tracks work together, **Masterology** masters the result.
Target host: Cubase Artist 14 on Windows (VST3). Same template as Masterology and Voxology: JUCE 8, framework-free C++20
engine with Catch2 tests, HTML / CSS / JS interface in the platform web view, GitHub Actions builds Windows on every push.

## How the pieces fit

```
 Cubase project
 ┌──────────────────────────────────────────────────────────────────────────────────────────┐
 │  Track: Kick  [Mixology Link]──┐                                                         │
 │  Track: 808   [Mixology Link]──┤  shared memory inside Cubase's process                  │
 │  Track: Vox   [Voxology][Link]─┤  (no routing needed: every instance of our plug-in       │
 │  FX:   Verb   [Mixology Link]──┤   lives in the same process and finds the others)        │
 │                                ▼                                                         │
 │  Stereo Out   [Mixology] ◄── main window: analysis, Mix Assistant, Auto Mix, all pages    │
 │               [Masterology]    │                                                         │
 │                                │ SysEx over two virtual MIDI ports                       │
 │  MIDI Remote script ◄──────────┘ ("Mixology Bridge", Steinberg's documented API)         │
 │        │                                                                                 │
 │        ▼                                                                                 │
 │  Real MixConsole faders, pan, mute, solo                                                 │
 └──────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Mixology** (main plug-in): the big window. Sits on the Stereo Out (or any channel). Talks to Cubase through the
  bridge, collects data from every Link, runs Analyze Mix / Auto Mix, hosts the Mix Assistant.
- **Mixology Link** (small plug-in, one per track): measures its track (loudness, peaks, spectrum, stereo, dynamics),
  learns the track's name and colour from Cubase (VST3 channel context), and later hosts that track's EQ, Dynamics,
  Tone and Stereo processing plus a Trim. On an FX channel it hosts a Space effect. Controlled from the main window.
- **Mixology Bridge** (Cubase MIDI Remote script): moves the real MixConsole controls and reports them back.
  The main plug-in opens the virtual MIDI ports itself, so there is no extra app to run. If Cubase or Windows blocks
  that, the fallback is a small tray app that relays the messages (the protocol stays the same).

## The look (locked)

The approved mockups are the reference for layout, colours, knobs and the beaker logo. What we change:
- Spelling and label slips in the art (e.g. "Keysh", repeated STEREO tab, two "HIGH WIDTH" knobs, "RATiO").
- Values that disagree with their knob position: every knob, meter and number is live and consistent.
- Track names and icons come from your real Cubase session (never the example names in the art).
- Wrong axes: the Dynamics transfer curve uses input dB vs output dB (the art shows a frequency axis).
- The mix stage shows loudness for information only (no "-8 to -6 LUFS target"; loudness targets are Masterology's job).

**Light mode:** click the beaker logo to switch. Same layout and accent colours (gold trim, red active tabs, red / orange /
blue knob glows, rainbow meters); backgrounds and knob bodies turn white. The choice is remembered.

Knobs, meters, faders and graphs are drawn in code (sharp at any Windows display scaling). Artwork reused from the
mockups: the beaker / atom logo, the chrome MIXOLOGY title and the seven Space scenes.

## Milestones

### M0 - Foundation (this build)
- Repo, CMake, CI (Windows build, unit tests, pluginval strictness 10, Steinberg VST3 validator, ASan engine tests).
- Full UI shell: all 8 pages (Overview, Tracks, EQ, Dynamics, Space, Tone, Stereo, Master), settings, help,
  light / dark mode. Browser preview with simulated data (mock.js) so the look can be checked without Cubase.
- The plug-in loads in Cubase and passes audio through untouched.

### M1 - Cubase bridge proof: PASSED (2026-10-13, Cubase Artist 14.0.41, script v3)
Goal: prove that Mixology can move ONE real MixConsole fader safely, and see you move it.
- `Mixology Bridge` MIDI Remote script + SysEx protocol (framework-free C++ with tests).
- In Mixology: Settings > Cubase Bridge panel: port status, channel name, live fader dB, "Lock this channel",
  set to a dB value, nudge +/- 1 dB, Restore, and a log you can copy.
- Safety: writes only to the locked channel, only while its name still matches; any bank / selection / rename change
  pauses writing until you re-confirm. A snapshot of the original value is taken before the first move (Restore).
- Pass when (your test in Cubase): set exact dB values (0 dB, -6, -12, -inf, +6) and the Cubase fader matches within
  0.1 dB; moving the Cubase fader shows in Mixology within 0.1 s; renaming / reordering / banking never moves another
  channel; no automation gets written; playback never stutters; Restore puts the fader back.

### M2 - Tracks page goes live: PASSED (2026-10-13, Cubase Artist 14)
- Bank of channels with real names, colours, fader, pan, mute, solo, both directions, no feedback loops. Every write
  names the channel the strip showed (slot + name hash) and is refused if that changed. Type a dB value, wheel nudges,
  per-channel and "Restore all" restore points.
- Mixology Link plug-in (first version: Trim + meters + identity). Link and bridge identities are matched by name and
  colour; without a confident match nothing is paired (amber chain icon). Links and Mixology are separate libraries,
  so they meet in a named shared-memory block (one per Cubase process).
- Protect channels (padlock, saved with the project), Stereo Out never in the bank, Write automation blocks moves,
  Read automation warns (playback would undo our move). "Exclude from Analyze / Auto Mix" moved to M3, where it
  first does something.
- Test whether moves land in Cubase's MixConsole History (undo); our own Restore works either way.

### M3 - Listening (analysis) (built: you test it, docs/M3_TEST.md)
Built: each Link analyses its track on a background thread 5 x a second (31 one-third-octave bands with stereo share,
K-weighted loudness, RMS / peak, correlation, width; TrackAnalyzer, 0.1 ms per frame) into the registry (layout v2).
Analyze Mix: reads every channel's fader first (bank scan, then back), listens while the transport runs (20 - 60 s),
then MixAnalysis gives roles (yours > name > sound), per-track stats and plain-language suggestions with
"Do I need to fix it?". Overview analyzer draws the six role groups; strips take the roles; role / leave-out saved
with the project. Song-position tagging was not needed: moments are taken together 5 x a second.
- Every Link publishes loudness (momentary / short-term), true peak, crest factor, a 1/3-octave spectrum, stereo width
  and correlation into the shared registry (M2 already carries peaks, loudness and Trim), tagged with song position
  (the same proven method as Voxology's Unmask link).
- Exclude channels from Analyze Mix / Auto Mix (e.g. a reference track or a printed stem).
- Track roles guessed from name + sound (kick, snare, hats, perc, 808 / bass, keys, synth, guitar, lead vox, ad-libs,
  FX), always editable.
- Analyze Mix listens across the song (play 20-60 s of the loudest parts, or the whole song) instead of one moment.
- Overview analyzer shows each role in its colour; Mix Assistant gives real suggestions in plain language with a
  "Do I need to fix it?" line, like Voxology's Learn.

### M4 - Auto Mix and the smart tools (tested in Cubase, follow-ups done: docs/M4_TEST.md)
Built: plans you approve before anything changes (MixPlanner: Balance, Stereo Architect, Clean Up, Vocal Pocket,
Auto Mix), applied across the whole project (BridgeSession::applyPlan pages the bank and comes back; per-apply Undo
with exact positions; Restore all reaches every page too). Mixology Link gained the Assist EQ (low cut, mud dip,
pocket dip; registry layout 4). Reference Match compares the mix with a reference song (muted, heard by its Link)
and switches A / B level-matched (solo + Trim). Ducking (sidechain) stays with the processing pages (M5).
- Auto Mix: relative levels and pans from genre targets (Hip Hop Modern Clean, Trap, G-Funk, Boom Bap, R&B...),
  preview old -> new for every fader, Apply moves the real faders, one-click Restore.
- Vocal Pocket: finds the instruments masking the lead vocal and proposes gentle, track-specific dips.
- Low End: kick vs 808 masking, sidechain-style ducking suggestions.
- Stereo Architect, Clean Up (resonances / mud), Reference Match (loudness-matched comparison with a reference song).
- Hard limits: never more than +/-6 dB per move without asking, never touch locked channels or the Stereo Out, never
  aim the mix at a loudness target, keep 3-6 dB of headroom for Masterology.

### M5 - Processing pages (in three test rounds)
Round 1 built (you test it, docs/M5_TEST.md): one processing chain (ProcChain, framework-free, 14 tests) in every
Mixology Link and in Mixology for the Full Mix: Gate / Expand, 6-band EQ (Precision / Smooth / Natural), Compressor,
Multiband (auto-threshold), Transient, Tone Balance, Loudness, Harmonic, Analog Touch, Clip Control (oversampled),
Stereo width (3 bands), Final Touch, lookahead Limiter (sample / true peak; latency only while on). 100 controls
from one table (ProcParams) become host parameters in both plug-ins and plugin/ui/procspec.js (generated, checked).
All modules start off: audio untouched, bit for bit. Mixology's pages pick a target (Full Mix or a Link) and talk to
the Link through the registry (layout 5: per-control requests, values and processing meters back). Mono Check on the
Stereo page (never saved, off when you leave the page). Link window: PROCESSING strip with On / Off.
Round 2 built (you test it, docs/M5_TEST.md): Space at the end of the chain on a track or an FX channel's Link
(100 % wet): Reverb (8-line FDN plate), Hall, Room (same engine, other sizes / early reflections), tempo-synced Delay
(ping-pong, tone in the loop), Chorus, Phaser, Special FX (Shimmer with a pitch shifter in the loop, Drift, Swell,
Freeze), crossfade when the effect changes, the effect's low / high cut, ducking under other tracks (registry
layout 6: every Link publishes its key level each block; Mixology tells a Link which tracks to duck under, the Link
keeps the list in its project), the host told about a long tail while Space is on (endless for Freeze). Delay lines
made on the message thread the first time Space is switched on. The Space page is live; its picture moves only while
the effect is in use. 41 more controls (141 in all).
Round 3 built (you test it, docs/M5_TEST.md): Polish (new tool, also inside Auto Mix) proposes processing through the
Links from Analyze Mix: vocal presence / air / harshness EQ (tilted band levels), a boxy kick dip, a compressor where a
vocal or the bass swings (new TrackReport.levelRange / loudPeak), stereo kick / 808 lows centred, the lead vocal's
reverb (on an FX channel with ducking, else a little on the vocal), ping-pong delay on ad-libs, the Full Mix's Tone
Balance against the style. Settings are absolute from the processing at analysis time (procAtAnalysis), so re-opening
proposes nothing more; Undo restores every control (and the duck list). Master page: Glue / Tilt / Width / Trim live on
the Full Mix (Final Touch + Tone Balance). Also: pans Cubase doesn't take (snap back to "L100") become "No pan",
saved with the project; Cubase script v4.
