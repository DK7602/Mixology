# Mixology manual

Manual for version 0.4.0 (the interface, live meters, every Cubase fader, Mixology Link, Analyze Mix, and the
smart tools: Auto Mix, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match).

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
| Auto Mix, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match | Live: this is what we're testing now |
| EQ, Dynamics, Space, Tone controls | Preview: they move but don't process audio yet (Milestone 5) |

Mixology itself passes your audio through untouched. What changes your sound: the Cubase faders and pans it
moves, and the Assist EQ / Trim in each Mixology Link.

## Where to put it

Insert **Mixology** on the **Stereo Out** (after your mix, before Masterology). It measures the whole mix
there. The Tracks page shows your channels through the Cubase bridge.

Insert **Mixology Link** on the tracks you want Mixology to hear one by one, after the track's other plug-ins.
It is light (no latency, very little CPU) and passes the sound through, apart from its Trim.

## Light or dark

Click the **beaker logo** (top left). Same layout and colours; the background and knobs turn white.
Mixology remembers your choice.

## Pages

- **Overview**: the mix analyzer (your whole mix as a rainbow, and each kind of track as a coloured line:
  Kick, Bass, Drums, Vocals, Instruments, Other), L / R peak meters and loudness, quick tone knobs, **Analyze
  Mix**, and the Mix Assistant.
- **Tracks**: the MixConsole as Mixology sees it, 12 channels at a time. Arrows move the bank.
  The colour bar on top of each strip is the Cubase track colour. Every strip moves its real Cubase channel
  (see "Moving faders" below). Meters show on strips whose track has Mixology Link.
- **EQ**: six bands. Drag a point to change frequency and gain; scroll on it to change the width (Q).
- **Dynamics**: Compressor, Multiband, Transient, Gate / Expand, Limiter.
- **Space**: Reverb, Delay, Chorus, Phaser, Hall, Room, Special FX, each with its own controls,
  plus EQ and Ducking (the effect gets quieter while the vocal sings).
- **Tone**: your mix's tonal balance against the style's target (red line), and loudness.
- **Stereo**: how wide your mix is, and whether it holds up in mono.
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
- **Tracks you can hardly hear**, tracks running very hot, and headroom for Masterology.
- **Channels without Mixology Link**: listed, so you know what the advice leaves out.

**Your tracks** (button under the Mix Assistant) shows what Analyze Mix heard for each track: its role, level
after the fader, peak and width. **Live view** switches the Mix Assistant back to the live readings.

**Track roles.** Mixology guesses each track's role from its name ("808", "Lead Vox", "Hi Hat"...), and from its
sound when the name doesn't say. Click a strip's icon on the Tracks page (or the role in Your tracks) to set it
yourself, or to **leave a track out of Analyze Mix** (a reference song, a printed stem). Your choices are saved
with the project and the advice updates straight away, without listening again.

## The smart tools (left of the analyzer)

Every tool works from the last **Analyze Mix**, so analyze first (and again after big changes). Each one opens a
list: every change it wants to make, **now → new**, and why, in plain words. Untick anything you don't want.
**Nothing changes until you press Apply.** Afterwards **Undo** puts back exactly what that tool changed.

- **Balance**: fader levels. Each kind of instrument against the others (kick, 808 / bass, snare, hats, keys,
  synths, guitars) using your genre's usual balance; only clear outliers move. Then the lead vocal against the
  rest of the mix (your style's target), ad-libs under the vocal, and room for Masterology: if the mix peaks
  above about -6 dBTP, everything comes down together. Tracks of one kind move together, so their balance among
  themselves stays yours. Channels without a Link (e.g. a printed beat) move with the rest of the beat.
- **Stereo Architect**: pans. Kick, bass, snare and the lead vocal in the middle. Pairs and doubles
  ("Gtr L" / "Gtr R", "Electric Rhythm" / "Electric Rhythm (D)", "Keys 1" / "Keys 2") spread left and right:
  if one is already out on a side, it stays and its partner mirrors it. A mix that leans to one side is evened
  out with the tracks pulling it there.
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

**Safety.** Protected channels (padlock) are never touched (faders, pans or EQ). Channels writing automation (W)
are skipped. A fader moves at most 6 dB per Apply unless you press **Allow moves over 6 dB**. If you moved a fader
since Analyze Mix, the tool leaves it alone (analyze again). When channels are outside the 12 in view, the bank
steps through your project and comes back by itself (a second or two per page); nothing else can be moved
meanwhile. **Restore all** (Tracks page) still takes everything back to before Mixology's first change.

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
Mixology never limits the mix bus; that's Masterology's job.

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
- **Strange notes on an instrument track**: untick "In 'All MIDI Inputs'" for both Mixology ports (setup step 3).
- **Send us the log**: Settings > Cubase Bridge > **Copy log**, then paste it into the chat.
