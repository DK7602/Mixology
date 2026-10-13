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

### M1 - Cubase bridge proof (this build, you test it)
Goal: prove that Mixology can move ONE real MixConsole fader safely, and see you move it.
- `Mixology Bridge` MIDI Remote script + SysEx protocol (framework-free C++ with tests).
- In Mixology: Settings > Cubase Bridge panel: port status, channel name, live fader dB, "Lock this channel",
  set to a dB value, nudge +/- 1 dB, Restore, and a log you can copy.
- Safety: writes only to the locked channel, only while its name still matches; any bank / selection / rename change
  pauses writing until you re-confirm. A snapshot of the original value is taken before the first move (Restore).
- Pass when (your test in Cubase): set exact dB values (0 dB, -6, -12, -inf, +6) and the Cubase fader matches within
  0.1 dB; moving the Cubase fader shows in Mixology within 0.1 s; renaming / reordering / banking never moves another
  channel; no automation gets written; playback never stutters; Restore puts the fader back.

### M2 - Tracks page goes live
- Bank of channels with real names, colours, fader, pan, mute, solo, both directions, no feedback loops.
- Mixology Link plug-in (first version: Trim + meters + identity). Link and bridge identities are matched by name and
  colour; a channel without a confident match is shown but can't be written.
- Lock / exclude channels, Stereo Out excluded from Auto Mix, warning + skip for channels whose volume automation is
  in Read mode (playback would undo our move).
- Test whether moves land in Cubase's MixConsole History (undo); our own Restore works either way.

### M3 - Listening (analysis)
- Every Link publishes loudness (momentary / short-term), true peak, crest factor, a 1/3-octave spectrum, stereo width
  and correlation into a lock-free shared registry, tagged with song position (the same proven method as
  Voxology's Unmask link).
- Track roles guessed from name + sound (kick, snare, hats, perc, 808 / bass, keys, synth, guitar, lead vox, ad-libs,
  FX), always editable.
- Analyze Mix listens across the song (play 20-60 s of the loudest parts, or the whole song) instead of one moment.
- Overview analyzer shows each role in its colour; Mix Assistant gives real suggestions in plain language with a
  "Do I need to fix it?" line, like Voxology's Learn.

### M4 - Auto Mix and the smart tools
- Auto Mix: relative levels and pans from genre targets (Hip Hop Modern Clean, Trap, G-Funk, Boom Bap, R&B...),
  preview old -> new for every fader, Apply moves the real faders, one-click Restore.
- Vocal Pocket: finds the instruments masking the lead vocal and proposes gentle, track-specific dips.
- Low End: kick vs 808 masking, sidechain-style ducking suggestions.
- Stereo Architect, Clean Up (resonances / mud), Reference Match (loudness-matched comparison with a reference song).
- Hard limits: never more than +/-6 dB per move without asking, never touch locked channels or the Stereo Out, never
  aim the mix at a loudness target, keep 3-6 dB of headroom for Masterology.

### M5 - Processing pages
- EQ, Dynamics (compressor, multiband, transient, gate / expand, limiter), Tone (warmth, presence, air, glue, punch),
  Stereo (width, low / high width, pan assist, mono check, correlation) running inside each track's Link.
- Space: Reverb, Delay, Chorus, Phaser, Hall, Room, Special FX on an FX channel's Link, each with its own correct
  controls and scene picture, plus EQ and ducking.
- Master page: monitoring and gentle finishing only (no limiter fight with Masterology).
- Auto Mix may then also propose EQ / dynamics moves through the Links.

### M6 - Suite and polish
- Recognise Voxology on a track (no double compression of vocals), hand-off notes for Masterology.
- Built-in manual (docs/MANUAL.md shown by the ? button), presets, undo history, CPU checks with 40+ tracks.
- Later: code signing, timeline clip-gain study (separate feasibility test).

## Things we test instead of assume
| Question | Where it gets answered |
| --- | --- |
| Can a VST3 open the virtual MIDI ports while Cubase also uses them? | M1 (fallback: tray relay app) |
| Exact dB <-> fader value curve in Cubase 14 | M1 (calibrated from Cubase's own display text) |
| Do remote moves write automation when a track is in Write / Latch? | M1 / M2 |
| Do remote moves show in MixConsole History (undo)? | M2 |
| Does every channel type (mono, stereo, group, FX, instrument) bank and report correctly? | M2 |
| Track name / colour from VST3 channel context in Cubase Artist 14 | M2 |
| CPU with a Link on every track | M3 |

## Your one-time setup (for M1)
1. Install loopMIDI (free, Tobias Erichsen) and create two ports named exactly `Mixology to Cubase` and
   `Cubase to Mixology`. (Windows 11 with the new Windows MIDI Services can use its built-in loopback instead.)
2. Copy the `Mixology` folder from `bridge/cubase` into
   `Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\` and restart Cubase.
3. In Studio Setup > MIDI Port Setup untick "In 'All MIDI Inputs'" for both Mixology ports.
Full steps and the test checklist: docs/M1_TEST.md.
