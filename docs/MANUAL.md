# Mixology manual

Manual for version 0.8.1 (the interface, live meters, every Cubase fader, Mixology Link, Analyze Mix, the smart
tools: Auto Mix, Polish, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match, the processing: EQ,
Dynamics, Tone and Stereo on any track or on the full mix, Space: reverb, delay and effects on a track or an FX
channel, ducking under the vocal, the Master page's finishing knobs, the suite link: Mixology sees Voxology
on your vocals and Masterology after it, and the setup check).

**Setup check.** While the song plays, Mixology watches for problems in the Cubase project that no mixing move can
fix, and puts them at the top of the Mix Assistant (Overview), and in the Tracks page's top bar (click it for the
fix):
- **Your mix arrives in mono**: your tracks play in stereo (or are panned), but left and right are the same at the
  Stereo Out. Usually a mono group channel they play through; Mixology names the one it most likely is (for
  example your music group "Beat") and says which button to click in Cubase.
- **Vocals without Mixology Link**: a vocal with Voxology on it but no Mixology Link, so Mixology can't hear it on
  its own (add the Link under Voxology).
- **Mixology on a mono channel**: it belongs on the Stereo Out.

**Voxology** makes each vocal sound great. **Mixology** makes all your tracks work together.
**Masterology** turns the finished mix into the master.

## What works in this build

| Part | Status |
| --- | --- |
| Interface: all 8 pages, light / dark look | Ready |
| Overview analyzer, loudness, peaks, headroom check | Live (what Mixology hears on its channel) |
| Stereo page: vectorscope and correlation | Live |
| Master page: loudness history, hand-off check for Masterology | Live |
| Tracks page: your real Cubase channels (names, faders, pan, mute, solo), 12 at a time | Live, both ways, through the Cubase bridge |
| Mixology Link on a track: its level on the Tracks page, a Trim, the Assist EQ | Live |
| Analyze Mix and the Mix Assistant's advice; track roles; the analyzer's track colours | Live |
| Auto Mix, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match | Live |
| EQ, Dynamics, Tone, Stereo pages: processing on the track you pick (in its Mixology Link) or on the full mix | Live |
| Mono Check (Stereo page) | Live |
| Space page: reverbs, delay, chorus, phaser, special effects on a track or an FX channel, ducking | Live |
| Polish (and Auto Mix): EQ, compression, vocal reverb / delay and the mix's tone proposed for you | Live |
| Master page's finishing knobs (Glue, Tilt, Width, Trim on the Full Mix) | Live |
| Suite link: Voxology on your vocals (VOX tag, no second compressor / reverb / delay), Masterology's goal on the Master page, the hand-off to Masterology | Live (needs Voxology 0.47 and Masterology 0.26 or newer) |
| Setup check: a mono group or output on the way, vocals without Mixology Link (Mix Assistant, Tracks page) | Live: this is what we're testing now |

Nothing changes your sound until you ask: every processing module starts switched off (the audio passes
through bit for bit). What can change it: the Cubase faders and pans Mixology moves, the Assist EQ / Trim in
each Mixology Link, and the modules you switch on (see "Processing" below).

## Where to put it

Insert **Mixology** on the **Stereo Out** (after your mix, before Masterology). It measures the whole mix
there. The Tracks page shows your channels through the Cubase bridge.

Insert **Mixology Link** on the tracks you want Mixology to hear one by one, after the track's other plug-ins.
It is light (no latency, very little CPU) and passes the sound through, apart from its Trim and the processing
you switch on for that track.

## Light or dark

Click the **beaker logo** (top left). Same layout and colours; the background and knobs turn white.
Mixology remembers your choice.

## Pages

- **Overview**: the mix analyzer (your whole mix as a rainbow, and each kind of track as a coloured line:
  Kick, Bass, Drums, Vocals, Instruments, Other), L / R peak meters and loudness, six quick tone knobs (the full
  mix's EQ bands: the EQ page with Full Mix picked shows the same), **Analyze Mix**, and the Mix Assistant.
- **Tracks**: the MixConsole as Mixology sees it, 12 channels at a time. Arrows move the bank.
  The colour bar on top of each strip is the Cubase track colour. Every strip moves its real Cubase channel
  (see "Moving faders" below). Meters show on strips whose track has Mixology Link.
- **EQ**: six bands on the track you pick (or the full mix). Drag a point to change frequency and gain; scroll on
  it to change the width (Q); double-click it to make that band flat again.
- **Dynamics**: Compressor, Multiband, Transient, Gate / Expand, Limiter, with in / out meters and gain reduction.
- **Space**: Reverb, Delay, Chorus, Phaser, Hall, Room, Special FX on a track or an FX channel, each with its
  own controls, plus the effect's EQ and Ducking (the effect gets quieter while the vocal sings). See "Space"
  below. While an effect is in use and the song plays, its picture comes alive with the beat (Cubase's tempo):
  lights bloom and fade with the reverb's Decay, echoes run down the delay's tunnel at its Time, the phaser's
  rings swirl at its Rate, a wave of light rolls through the hall, the Special FX crystal turns on every beat with
  shock waves and sparks. An effect nobody uses stands still.
- **Tone**: Tone Balance, Loudness, Clip Control, Harmonic, Analog Touch, Final Touch; your mix's tonal balance
  against the style's target (red line), and loudness.
- **Stereo**: Width (and Low / Mid / High Width), Mono Check, and how wide your mix is.
- **Master**: loudness over the last minute, a checklist for handing the mix to Masterology, and four finishing
  knobs on the Full Mix (before Masterology): **Glue** (gentle bus compression), **Tilt** (+ brighter, − warmer),
  **Width** and **Trim** (the level going into Masterology). Leave the loudness to Masterology.

Knobs: drag up / down (hold Shift for fine steps), scroll, or use the arrow keys. Double-click resets.

## The Cubase bridge (one-time setup, about 5 minutes)

Mixology moves the real MixConsole faders through Cubase's own MIDI Remote system and two virtual MIDI
ports. Nothing is installed inside Cubase except a small script.

1. Install **loopMIDI** (free, by Tobias Erichsen). Click **+** twice and name the ports exactly
   `Mixology to Cubase` and `Cubase to Mixology`. Tick "Autostart" in loopMIDI's tray menu.
2. Copy the `Mixology` folder from the download's `Cubase MIDI Remote` folder into
   `Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\`.
3. Start Cubase. Open **Studio > Studio Setup > MIDI Port Setup** and untick **In 'All MIDI Inputs'**
   for both Mixology ports.
4. Open the **MIDI Remote** tab (lower zone). "Mixology Bridge" should appear on its own. If it doesn't,
   click **+** (Add MIDI Controller Surface), pick vendor **Mixology**, model **Bridge**, input
   `Mixology to Cubase`, output `Cubase to Mixology`.
5. Open Mixology. **Settings (gear) > Cubase Bridge** turns green, and the Tracks page shows your channels.

## Moving faders

Every strip on the Tracks page moves its real MixConsole channel:

- **Drag** the fader. **Double-click** the fader cap for 0 dB.
- **Mouse wheel** over a fader: 0.5 dB per notch (hold Shift for 0.1 dB).
- **Click the level** under the fader and type an exact value (`-6`, `0`, `-inf`), then Enter. Mixology keeps
  adjusting until Cubase shows exactly that number.
- **Pan** knob, **S** (solo) and **M** (mute).
- Moves made in Cubase show up in Mixology right away.

**Padlock = protect.** Click a strip's padlock and it turns gold: Mixology will never move that channel (later
also Auto Mix won't). It is saved with the project and follows the channel when the bank moves.

**Restore.** Before Mixology changes a channel the first time, it remembers how it was. A small **↺** on a strip
puts that channel back; **↺ Restore** on the top bar puts back every channel Mixology changed (channels outside the
12 in view: move the bank to them and press Restore again). Settings > Cubase Bridge > **Keep the mix as it is
now** makes the current mix the new starting point.

Safety rules (always on):

- Every move names the channel you were looking at. If the bank moved or tracks were reordered or renamed in
  between, the move is refused (in Mixology, and again by the Cubase script) instead of landing on another channel.
- A channel with automation **Write (W)** on is never moved (red **W** on the strip), so no automation gets
  recorded by accident. With **Read (R)** on (green **R**) you get a warning: if that channel has volume
  automation, playback moves the fader back.
- The Stereo Out is never part of the bank.
- Two channels with the same name: both can be moved, but Restore and Mixology Link can't tell them apart
  (the name shows in amber). Give one a different name.

## Mixology Link

Mixology Link is a small plug-in for each track. Version 1:

- **Shows the track's level** on its strip in Mixology's Tracks page (the meter shows the level after the fader,
  like Cubase's own meters). Its own window shows peaks and loudness too.
- **Knows which track it is on**: Cubase tells it the track's name and colour. Mixology pairs it with the channel
  of the same name (green chain icon on the strip). If two tracks share a name, the colour decides; if that's the
  same too, the chain turns amber and nothing is paired until you rename one.
- **Trim**: a clean volume change before the fader (-24 to +24 dB). Reference Match uses it to level-match your
  reference song.
- **Assist EQ** (bottom of its window): a **Low Cut** (12 dB / octave), a **Mud** dip (200 - 600 Hz) and a
  **Pocket** dip (1 - 6 kHz). Clean Up and Vocal Pocket set them; you can also change them like any plug-in
  parameter in Cubase (automation works). **Clear** takes them out. When they're off, the Link passes the audio
  through bit for bit.

- **Processing** (Milestone 5, bottom of its window): the EQ, Dynamics, Tone and Stereo modules Mixology's pages
  switched on for this track (see "Processing" below). **On / Off** switches all of them off for a before / after.
  Only the Limiter adds latency (5 ms, while it's on); Cubase compensates.

Each Link also listens to its track about five times a second (tone in 31 bands, loudness, punch, stereo width,
left / right balance) for Analyze Mix and the analyzer's track colours. It runs on a background thread and
costs very little.

## Analyze Mix and the Mix Assistant

Analyze Mix listens to every track that has Mixology Link, and to the whole mix, while the song plays. Then the
Mix Assistant tells you, in plain words, what stands out, with **Do I need to fix it?** and how.

1. Insert Mixology Link on your tracks (after their other plug-ins). Pick your mix style in the header.
2. Press **ANALYZE MIX**. Mixology first reads every channel's fader in Cubase (a second or two; the Tracks
   page flickers while it pages through).
3. Press play in Cubase. Play the busiest part (the hook or the drop): **20 seconds is enough**, up to 60.
   Press **Finish now**, or it finishes by itself at 60 seconds.
4. The Mix Assistant lists what it found, the things worth fixing first. Click a point to read it in full.

What it looks at:

- **Vocal level**: how loud the lead vocal sits against the rest of the mix while it sings, compared with your
  style (country, pop, R&B and folk sit the vocal more forward than rock). The rest of the mix is what Mixology
  hears on the Stereo Out, so a beat without a Link still counts.
- **Kick and 808 / bass**: whether they fight over the same low frequencies when they hit together.
- **Words covered**: instruments as loud as the vocal at 1.6 - 4 kHz (where words are understood).
- **Tone against your style**: muddy low mids, harsh upper mids, too much or too little low end, dull or very
  bright top.
- **Low rumble** in tracks that don't need low end (vocals, keys...): a low cut makes room.
- **Stereo**: low end that isn't centred; tracks or a mix that could fall apart in mono.
- **Left / right balance**: a mix that is louder on one side, and the tracks pulling it there (for example
  several guitars all panned hard left).
- **A mono mix**: left and right of the Stereo Out the same (a mono group or output on the way). Panning then
  changes nothing, so Stereo Architect leaves the pans alone.
- **Tracks you can hardly hear**, tracks running very hot, and headroom for Masterology.
- **Channels without Mixology Link**: listed, so you know what the advice leaves out.

**Your tracks** (button under the Mix Assistant) shows what Analyze Mix heard for each track: its role, level
after the fader, peak and width. **Live view** switches the Mix Assistant back to the live readings.

**Track roles.** Mixology guesses each track's role from its name ("808", "Lead Vox", "Hi Hat"...), and from its
sound when the name doesn't say. Click a strip's icon on the Tracks page (or the role in Your tracks) to set it
yourself, or to **leave a track out of Analyze Mix** (a reference song, a printed stem). Your choices are saved
with the project and the advice updates straight away, without listening again.

## The smart tools (left of the analyzer)

Every tool works from the last **Analyze Mix**, so analyze first (and again after big changes). Fader moves since
then are allowed for: apply a plan, open it again, and it finds nothing more to do (unless something was limited
to 6 dB). Each one opens a
list: every change it wants to make, **now → new**, and why, in plain words. Untick anything you don't want.
**Nothing changes until you press Apply.** Afterwards **Undo** puts back exactly what that tool changed (a fader or pan you moved yourself since then stays as you set it).

- **Balance**: fader levels. Each kind of instrument against the others (kick, 808 / bass, snare, hats, keys,
  synths, guitars; one "Drum Machine" track counts as the whole kit) using your genre's usual balance; only clear
  outliers move. Then the lead vocal against the rest of the mix, at most 4 dB per Apply, and ad-libs under the
  vocal. The overall loudness stays the same, so you can judge before / after fairly (louder always sounds
  "better" at first). Tracks of one kind move together, so their balance among themselves stays yours. Channels
  without a Link (e.g. a printed beat) move with the rest of the band; when such a channel carries a big part of
  the band, Mixology doesn't judge the instruments against each other (it can't hear the beat's parts).
  **Your vocal, your ears.** At the top: "Your vocal right now sounds: Too quiet / Right / Too loud". Mixology
  measures where your vocal sits; your answer sets where it should sit, saved with the song. The advice, Balance
  and Auto Mix follow it from then on ("Use the style's level" goes back to the genre's usual level). The same
  buttons are on the Mix Assistant's vocal points.
  **Room for Masterology** is a separate line at the bottom, unticked: every fader that plays goes down together
  (e.g. -12.8 dB when the mix peaks at +6.8 dBTP). The balance stays the same, the mix gets quieter, Masterology
  brings the level back. Tick it last, once the balance sounds right.
- **Stereo Architect**: pans. Kick, bass, snare and the lead vocal in the middle. Pairs and doubles
  ("Gtr L" / "Gtr R", "Electric Rhythm" / "Electric Rhythm (D)", "Keys 1" / "Keys 2") spread left and right:
  if one is already out on a side, it stays and its partner mirrors it. A mix that leans to one side is evened
  out with the tracks pulling it there. It only pans **mono** tracks: for a stereo track Cubase doesn't tell
  Mixology where its panner really sits. If Cubase reports pans your mix doesn't match (stereo tracks with Cubase's
  Combined Panner show one side only, e.g. "L100" on a full-width track), it leaves the pans alone and says so.
- **Clean Up**: Assist EQ. Low cuts on tracks with rumble they don't need (and on vocals: about 90 Hz), and mud
  dips (200 - 500 Hz) on the tracks carrying it when the mix is muddier than your style.
- **Vocal Pocket**: Assist EQ. Gentle dips (2 - 4 dB) at 1.6 - 4 kHz in the instruments that cover the vocal's
  words, each where it overlaps the vocal most.
- **Polish**: processing in each track's Mixology Link, worked out from what Analyze Mix heard:
  - **EQ**: a dull lead vocal gets a little presence (around 3.5 kHz) and, if it has little air, a gentle high
    shelf; a harsh one a narrow dip; a boxy kick a dip around 300 - 500 Hz. Gentle moves (at most 3 dB), in an EQ
    band that's free (or the one already doing that job: it's adjusted, never doubled).
  - **Compressor**: where a vocal or the bass swings a lot in level between loud and quiet parts (about 4 dB off
    the loud parts). A track that is even already (Voxology on the vocal, for example) or that has a compressor
    switched on is left alone.
  - **Low end in the middle**: a stereo kick or 808 whose lows are spread gets them centred (Stereo, Low Width 0 %).
  - **Space**: a reverb for the lead vocal. If you have an FX channel with Mixology Link (named like "FX 1-...",
    "Verb", "Delay"...), it goes there, 100 % wet, ducking under the vocal; send the vocal to it in Cubase at 0 dB
    (the list reminds you). Its **LEVEL** is set so the reverb sits where your style wants it under the vocal (about
    20 dB under in hip hop, 17 in pop / rock, 14 in gospel); with a send already in place, Mixology hears how much
    goes in and allows for it. If a reverb on an FX channel is already there, Polish checks its level and offers to
    turn it down (or up) when it's more than 3 dB off. When the vocal needs no EQ, the list says why ("already
    sounds balanced"). Without one, a little plate on the vocal itself (12 %). Hip hop, trap, R&B: a short plate;
    gospel, worship, cinematic: a hall. Ad-libs get a ping-pong delay (in the urban and pop styles). An effect you
    set up yourself is left alone.
  - **The mix's tone**: when the whole mix is clearly short of (or has too much) low end, presence or air against
    your style, a gentle Tone Balance on the Full Mix.
  Apply and open it again: it finds nothing more to do. Undo puts every setting back.
- **Auto Mix**: all of the above together (levels, pans, Clean Up, Vocal Pocket and Polish).
- **Reference Match**: put a finished song you like on its own track under yours (same bars), insert Mixology
  Link on it and mute the track (its Link still hears it). Analyze Mix, then pick it in Reference Match. You see
  both tonal curves, five regions in words ("2.4 dB less low end"), loudness and width. **Hear the reference**
  plays it alone, its Trim matched to your mix's loudness, so you compare sound, not volume; **Back to your mix**
  mutes it again. The reference is always left out of the mix advice. Loudness differences are Masterology's
  job: don't push your mix up to match a mastered song.

**Groups (e.g. the "Beat" group Voxology listens to), found by ear.** Analyze Mix compares what the Stereo Out
plays with what the Links hear, moment by moment and band by band. From that it works out how your vocals and your
instruments reach the Stereo Out: a group fader at -6, a compressor on a group, Cubase's pan law. Whatever the
channels are called, a group is never counted as extra music and never moved as if it were; the levels Mixology
works with include what happens on the way (Your tracks shows it). Optional: tell Mixology which group the
instruments play through (its icon on the Tracks page > **Music group**, or the question in the Mix Assistant /
Balance): then Balance moves the music on that one fader instead of each instrument. A printed beat (real extra
music without a Link) is told apart: the Links can't explain it. Mixology then counts its fader in the
instruments' level, never counts the group as extra music, and moves the music on that one fader (vocal against
the music, room for Masterology) instead of moving every instrument and the group twice.

**Muted tracks.** A Mixology Link still hears its track when the track is muted (that's how Reference Match hears
your muted reference song). Analyze Mix and the analyzer's colours leave muted channels out, and channels that
aren't soloed while something else is.

**Safety.** Protected channels (padlock) are never touched (faders, pans or EQ). Channels writing automation (W)
are skipped. A fader moves at most 6 dB per Apply unless you press **Allow moves over 6 dB**. If you moved a fader
since Analyze Mix, the tool leaves it alone (analyze again). When channels are outside the 12 in view, the bank
steps through your project and comes back by itself (a second or two per page); nothing else can be moved
meanwhile. **Restore** (Tracks page) takes everything back to before Mixology's first change: faders, pans, mute /
solo and the Assist EQ Mixology set (the ↺ on a strip does it for that track). A fader, pan or Assist EQ you changed
yourself since stays as you set it. **Keep the mix as it is now** (Settings) makes the current state, Assist EQ
included, the new starting point.

## Processing (EQ, Dynamics, Tone, Stereo)

These four pages work on **one target at a time**, picked in the dropdown at the top right of each page (all four
share the pick):

- **A track**: every track with Mixology Link is in the list. The processing runs inside that track's Link
  (after its Trim and Assist EQ), so it's part of the track, like an insert plug-in.
- **Full Mix**: the processing runs inside Mixology on the Stereo Out, before Masterology.

Switching on:

- Every module starts **off**. Turn any of its knobs (or drag an EQ point) and it switches itself on; the green
  **ON** button switches it off and on again (a quick before / after).
- The lamps in the left column show which modules are on for this target (they pulse while they work on the sound).
- **Reset this module** puts its knobs back where they started.
- In a Link window, the **PROCESSING** strip lists what's on for that track; its **On / Off** button switches all
  of that track's processing off at once (a before / after for the whole track).
- Everything is a normal Cubase parameter of that Link (or of Mixology for the full mix): saved with the project,
  and it can be automated.

The modules (the sound runs through them in this order):

| Page | Module | What it does |
| --- | --- | --- |
| Dynamics | Gate / Expand | Turns the track down between notes. Gate closes by the full Range; Expander turns quiet parts down gently (2:1). |
| EQ | 6 bands | Bells, shelves, low / high cut. **Precision**: exact curves, 24 dB / octave cuts. **Smooth**: wider bells, 12 dB / octave cuts. **Natural**: small moves broad, big moves narrower (console style). |
| Dynamics | Compressor | Threshold, Ratio, Attack, Release, Makeup, Knee (Soft / Medium / Hard). |
| Dynamics | Multiband | Lows, mids and highs each held together by their own compressor; the knob says how much (it finds its own threshold). 3 or 2 bands. |
| Dynamics | Transient | Attack + / − (the front of each hit), Sustain + / − (the tail), Speed, Output, Mix; Focus on the lows or highs. |
| Tone | Tone Balance | Warmth, Presence, Air (gentle shelves and a bell), Glue (light compression), Punch (attack). |
| Tone | Loudness | Trim, Density (a blended-in compressor: fuller), Low End and Top End shelves. |
| Tone | Harmonic | Drive, Even / Odd (warm or edgy), Tone (brighter / darker harmonics), Mix. |
| Tone | Analog Touch | Tape (soft saturation, low bump, softer top), Console (subtle edge, channel bleed), Noise (hiss while it plays), Mix. |
| Tone | Clip Control | Rounds off peaks over the Threshold; Softness; Oversampling 1x - 8x (cleaner); Mix. |
| Stereo | Width | Width, Low / Mid / High Width (the side signal split at Low Below / High Above). 100 % = as it is. Mono tracks: nothing to widen (pan them). |
| Tone | Final Touch | Width, Air, Glue, Output. |
| Dynamics | Limiter | Nothing passes the Ceiling (sample or true peaks). Adds 5 ms of delay while it's on; Cubase lines the track up. On the full mix, leave the final limiting to Masterology. |

**Mono Check** (Stereo page): the whole mix plays in mono while the red badge shows (the scope and meters keep
showing the mix as it really is, so you can still see its stereo picture). It switches itself off when
you leave the Stereo page or close Mixology, so a mixdown can never come out mono by mistake.

CPU: a module that's off costs nothing. An EQ and a compressor on a track cost about 1.5 % of one CPU core.

## Space (reverb, delay and effects)

Space gives a track (or an FX channel) one effect: **Reverb** (a plate), **Delay**, **Chorus**, **Phaser**,
**Hall**, **Room** or **Special FX**. It runs inside that track's Mixology Link, at the end of its processing.

1. Top of the Space page: pick the **track** (Space never works on the Full Mix: a reverb on everything washes
   the mix out).
2. Pick the effect on the left and press **ON** (or turn one of its knobs, or pick a preset). One effect per
   track: press ON on another effect and it takes over (the old one fades out, no click). The green lamp on the
   left shows the effect this track uses. Each effect keeps its own knob settings.
3. **USED AS**:
   - **On this track**: **MIX** blends the effect into the track. Good for a touch of room or chorus on one sound.
   - **FX channel (100 % wet)**: only the effect comes out, and **MIX** becomes **LEVEL**: how loud the effect
     comes back (how much of each track goes in is its Send in Cubase). This is how pros do vocal reverb and delay: in
     Cubase, add an **FX channel** (Project > Add Track > Effect, or any FX channel you have), put **Mixology Link**
     on it, pick it here, and send your vocals to it with their **Sends** in the MixConsole. One reverb for all
     the vocals sounds like one room. A track whose name looks like an FX channel ("FX 1-...", "Verb", "Delay",
     "Return") is set to FX channel by itself the first time you switch Space on.
4. **EFFECT EQ** (right): the effect's low cut and high cut (200 Hz and 9 kHz to start: a reverb without boomy
   lows or hissy highs sits better). Double-click: back to the start.
5. **DUCKING**: pick the track(s) to duck under (usually your lead vocal, or All vocals) and the **AMOUNT**. The
   effect dips while they play and blooms in the gaps, so the vocal stays clear. The bar shows how much it is
   ducked right now. The tracks it ducks under need Mixology Link (each Link tells the others how loud its track is).

| Effect | Knobs |
| --- | --- |
| Reverb (plate) | Pre-delay (a gap before the reverb: keeps words clear), Decay (how long it rings), Damping (highs fade faster above this), Width, Mix |
| Delay | Time (note values, locked to Cubase's tempo: 1/8, dotted 1/8, 1/4...), Feedback (how many repeats), Tone (darker repeats), Ping-Pong (repeats bounce left / right), Mix |
| Chorus | Rate, Depth, Delay, Width, Mix |
| Phaser | Rate, Depth, Feedback, Center, Mix |
| Hall | Pre-delay, Decay (1 - 12 s), Size, Damping, Mix |
| Room | Pre-delay, Decay (short), Size, Early / Late (more walls or more tail), Mix |
| Special FX | **Shimmer** (pitched echoes, Shift, climb inside a big reverb), **Drift** (a huge drifting wash), **Swell** (each new note's effect fades in, like a reversed reverb), **Freeze** (holds the sound like an endless pad). Feedback, Motion, Tone, Mix |

Good starting points: lead vocal on an FX channel with **Vocal Plate** + a second FX channel with **Dotted 1/8**
delay, both ducking under the lead vocal. Adlibs: **Ping-Pong 1/8**. Drums: **Drum Room** on the track, Mix 15 %.

Cubase is told how long each effect rings (its "tail"), so a reverb is never cut off when the track goes quiet.
Switching Space on or off makes Cubase restart that plug-in for a moment (a tiny gap in its sound): do it while
stopped if you can. A Space effect uses 0.3 - 2.3 % of one CPU core (Delay the least, Hall the most).

## Mix styles

The style in the header (for example "Hip Hop - Modern Clean") is what Mixology aims for. Click it (or
**PRESETS**) to choose: first the **genre**, then its **style** in the pop-up. The arrows next to the name step
through the styles of the genre you're in.

| Genre | Styles |
| --- | --- |
| Hip Hop | Modern Clean, Boom Bap, West Coast, G-Funk, Old School, Lo-Fi, Pop Rap |
| Trap & Drill | Hard 808, Melodic, Drill, Rage, Plugg |
| R&B & Soul | Smooth R&B, 90s R&B, Alternative R&B, Neo-Soul, Classic Soul, Funk |
| Pop | Modern, Dance, Synth / 80s, Indie, Acoustic, Ballad, K-Pop, Hyperpop |
| Rock | Classic, Hard, Alternative, Indie, Punk / Pop Punk, Grunge, Metal, Modern Metal, Soft / Light, Blues Rock |
| Country | Modern, Country Pop, Classic, Outlaw / Southern, Bluegrass, Americana |
| Gospel | Contemporary, Traditional, Mass Choir, Urban Gospel |
| Worship | Modern, Live / Big Room, Acoustic, Hymns |
| Electronic | House, EDM / Big Room, Techno, Drum & Bass, Dubstep, Future Bass, Ambient |
| Latin | Reggaeton, Latin Pop, Salsa / Tropical, Bachata, Regional Mexican |
| Afro & Caribbean | Afrobeats, Amapiano, Reggae, Dancehall |
| Jazz & Blues | Jazz, Smooth Jazz, Blues |
| Folk & Acoustic | Singer-Songwriter, Folk, Indie Folk |
| Cinematic | Epic Trailer, Orchestral, Soft Score |

The style sets the Tone page's target curve and what Analyze Mix compares your mix with (tone, how loud the
vocal sits). Its genre also sets the levels Balance and Auto Mix aim for. The style is saved with the
project.

## How it works with Voxology and Masterology

1. Voxology on each vocal: tuning, cleanup, de-essing, compression, space.
2. Mixology on the Stereo Out: balance all tracks against each other (levels first, then masking and width).
3. Masterology after Mixology on the Stereo Out (or in a mastering project): final EQ, loudness and limiting.

Leave Masterology 3 to 6 dB of headroom: the Master page tells you when the mix peaks are in the right place.
Final limiting is Masterology's job: keep Mixology's Limiter off on the Full Mix (or use it only as a safety).

### The three talk to each other (the suite link)

In the same Cubase project, the three plug-ins tell each other what they do. Nothing to set up: just use
Voxology 0.47, Mixology 0.8 and Masterology 0.26 (or newer).

- **Voxology on a vocal.** The Tracks page shows a small gold **VOX** tag under the track's icon (hover it to
  see what Voxology does there: compressing, de-essing, its reverb or delay...). Mixology then:
  - treats the track as a vocal (the lead, or the ad-libs when the track's name says so),
    even when its name doesn't say "vocal". Your own choice in the role menu still wins.
  - never stacks a second compressor where Voxology compresses, a second reverb on the lead vocal where Voxology
    adds its reverb, or a ping-pong delay on ad-libs where Voxology adds its delay. Auto Mix and Polish say so in
    their notes ("Voxology compresses it already").
  - warns when the lead gets reverb from Voxology **and** from an FX channel: one space is usually enough.
  - A **BEAT** tag: Voxology in Beat mode on that track (it only listens to the beat for the key).
- **Masterology after Mixology.** The Master page's HAND-OFF panel shows what Masterology aims for (for example
  "Masterology: −14 LUFS · −1.0 dBTP"; hover it for the goal and genre) and a check that it is there.
- **Mixology's hand-off to Masterology.** Mixology tells Masterology the mix style you picked, the mix's peaks
  and loudness, and (after Analyze Mix) how its tone sits against the style and whether the mix is mono.
  Masterology shows it and can start from the matching genre.

The link only works inside one program: Voxology, Mixology and Masterology must run in the same Cubase (they do,
unless you mix in one project and master in another; then Masterology simply works on its own as before).

## Troubleshooting

- **"Can't find the virtual MIDI ports"**: loopMIDI isn't running, or the port names differ. Names must
  contain `Mixology to Cubase` and `Cubase to Mixology`. Then press **Reconnect**.
- **"Waiting for the Mixology Bridge script"**: the script isn't active in Cubase. Check the MIDI Remote tab
  (step 4 above). After copying the script, restart Cubase or use **Reload Scripts** in the MIDI Remote
  Manager.
- **The script disappears after restarting Cubase**: a known Cubase issue with some script surfaces. Add it
  again with **+** in the MIDI Remote tab.
- **Connected, but no channels on the Tracks page**: press **Refresh** in Settings > Cubase Bridge. If
  the slots stay empty, check that the script says v3 or later, then **Copy log** and send it.
- **"That strip now shows ..."**: the bank or the track order changed between your click and the move. Nothing
  was moved; just try again on the strip as it is now.
- **A strip has no meter**: insert Mixology Link on that track. If the Link is there but the chain icon is amber,
  another track has the same name: rename one.
- **The Link window says "Track name not reported"**: tell us (Cubase normally passes the name on).
- **Analyze Mix says "No Mixology Link found"**: insert Mixology Link on your tracks first. Both plug-ins must be
  from the same download (an older Link can't talk to a newer Mixology). After an update, copy **both**
  .vst3 folders again and restart Cubase.
- **Analyze Mix waits at "Press play in Cubase"**: it only listens while the song plays.
- **A stereo track shows "Stereo" under its pan knob**: it uses Cubase's Stereo Combined Panner. Cubase only tells
  Mixology the panner's left edge (a full-width track would read "L100"), so Mixology never pans those tracks:
  pan them in Cubase.
- **Strange notes on an instrument track**: untick "In 'All MIDI Inputs'" for both Mixology ports (setup step 3).
- **A processing page says "its Mixology Link isn't answering"**: the track or its Link was removed. Pick
  another target.
- **A track isn't in the processing dropdown**: it has no Mixology Link, or its Link is from an older download
  (copy both .vst3 folders again).
- **A strip says "No pan"**: Cubase doesn't pan that channel: its output is mono (for example a mono group the track
  plays through), so it sits in the middle whatever its pan says. Cubase still reports such a channel as "L100".
  Mixology learns this the first time you (or Auto Mix) turn its pan, remembers it with the project, never moves
  that pan again, and counts the track as centred. To pan it, make the group / output stereo in Cubase; as soon as
  Cubase reports a real pan for it, Mixology pans it again.
- **A strip reads "L100" but you hear it in the middle**: turn its pan knob in Mixology once: it changes to "No pan"
  (see above). This needs the Cubase script v4 or later (Settings > Cubase Bridge shows the version; the log says
  when a newer one came with the download).
- **"Your mix arrives in mono"** (Mix Assistant): click it. In Cubase's MixConsole, the small button at the bottom of
  each channel shows one circle for mono, two linked circles for stereo: click the named group's button (playback
  stopped) to make it stereo.
- **The Stereo page says the mix is mono**: left and right are the same at the Stereo Out (usually a mono group on
  the way), so Width can't change anything there.
- **A track sounds 5 ms late**: its Limiter is on and Cubase's delay compensation is switched off (Constrain
  Delay Compensation). Switch that off again, or the Limiter.
- **Space: I hear nothing from the FX channel**: the tracks need a **Send** to that FX channel in Cubase (MixConsole,
  Sends), turned up. Mixology can't make sends for you.
- **Space: the dry vocal is twice as loud / phasey**: the FX channel's Link is set to **On this track** (it passes
  the sent vocal through). Set **USED AS** to FX channel (100 % wet).
- **Space: ducking does nothing**: the track it ducks under needs Mixology Link (and must be playing); AMOUNT must
  be above Off.
- **Space: the delay doesn't follow the tempo**: it follows Cubase's tempo while the song plays (120 BPM until it
  has heard one).
- **No VOX tag on a vocal with Voxology**: update Voxology to 0.47 or newer. The tag follows the track's name, so
  Voxology and the Tracks page must see the same name (rename the track in Cubase if two tracks share one).
- **The Master page doesn't show Masterology**: update Masterology to 0.26 or newer, and put it in the same project
  (after Mixology on the Stereo Out). It appears a moment after Masterology is inserted.
- **Send us the log**: Settings > Cubase Bridge > **Copy log**, then paste it into the chat.
