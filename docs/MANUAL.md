# Mixology manual

Manual for version 0.5.0 (the interface, live meters, every Cubase fader, Mixology Link, Analyze Mix, the smart
tools: Auto Mix, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match, and now the processing:
EQ, Dynamics, Tone and Stereo on any track or on the full mix).

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
| EQ, Dynamics, Tone, Stereo pages: processing on the track you pick (in its Mixology Link) or on the full mix | Live: this is what we're testing now |
| Mono Check (Stereo page) | Live |
| Space page, Master page's finishing knobs | Preview: they move but don't process audio yet (next rounds) |

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
- **Space**: Reverb, Delay, Chorus, Phaser, Hall, Room, Special FX, each with its own controls,
  plus EQ and Ducking (the effect gets quieter while the vocal sings). The picture comes alive while the song
  plays and moves with its beat (Cubase's tempo): lights bloom and fade with the reverb's Decay, echoes run down
  the delay's tunnel at its Time, the phaser's rings swirl at its Rate, and so on. It only moves while the Space
  page is open.
- **Tone**: Tone Balance, Loudness, Clip Control, Harmonic, Analog Touch, Final Touch; your mix's tonal balance
  against the style's target (red line), and loudness.
- **Stereo**: Width (and Low / Mid / High Width), Mono Check, and how wide your mix is.
- **Master**: loudness over the last minute and a checklist for handing the mix to Masterology.

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
- **Auto Mix**: all four together.
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

**Mono Check** (Stereo page): the whole mix plays in mono while the red badge shows. It switches itself off when
you leave the Stereo page or close Mixology, so a mixdown can never come out mono by mistake.

CPU: a module that's off costs nothing. An EQ and a compressor on a track cost about 1.5 % of one CPU core.

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
3. Masterology after Mixology (or in a mastering project): final EQ, loudness and limiting.

Leave Masterology 3 to 6 dB of headroom: the Master page tells you when the mix peaks are in the right place.
Final limiting is Masterology's job: keep Mixology's Limiter off on the Full Mix (or use it only as a safety).

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
- **A track sounds 5 ms late**: its Limiter is on and Cubase's delay compensation is switched off (Constrain
  Delay Compensation). Switch that off again, or the Limiter.
- **Send us the log**: Settings > Cubase Bridge > **Copy log**, then paste it into the chat.
