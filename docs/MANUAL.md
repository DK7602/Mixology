# Mixology manual

Mixology is part of **Sonicology**: Voxology (vocals), Mixology (the mix) and Masterology (mastering), made to work
together. In your DAW's plug-in list all three are under Sonicology.

Manual for version 0.21.1 (the interface, live meters, the faders in Cubase, FL Studio, Ableton Live, Logic Pro and Cakewalk, Mixology Link, Analyze Mix, the smart
tools: Auto Mix, Polish, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match, the processing: EQ,
Dynamics, Tone and Stereo on any track or on the full mix, Space: reverb, delay and effects on a track or an FX
channel, ducking under the vocal, the Master page's finishing knobs, the suite link: Mixology sees Voxology
on your vocals and Masterology after it, the setup check, presets, and the History: undo any change Mixology made).

**Setup check.** While the song plays, Mixology watches for problems in the Cubase project that no mixing move can
fix, and puts them at the top of the Mix Assistant (Overview), and in the Tracks page's top bar (click it for the
fix):
- **Your mix arrives in mono**: your tracks play in stereo (or are panned), but left and right are the same at the
  Stereo Out. Usually a mono group channel they play through; Mixology names the one it most likely is (for
  example your music group "Beat") and says which button to click in Cubase.
- **Vocals without Mixology Link**: a vocal with Voxology on it but no Mixology Link, so Mixology can't hear it on
  its own (add the Link under Voxology).
- **Mixology on a mono channel**: it belongs on the Stereo Out.
- **Two tracks with the same name**: Mixology tells tracks, their faders and their Mixology Links apart by name. Until
  you rename one, Auto Mix and the smart tools leave both alone (they say so), and the advice about them may be off.

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
| The same in FL Studio (mixer inserts) and Ableton Live (tracks and return tracks) | Built and tested against a simulator, not yet in the real programs |
| The same in Logic Pro and Cakewalk, with Mixology as their Mackie Control (8 channels at a time) | New: built and tested against a simulator, not yet in the real programs |
| Mixology Link on a track: its level on the Tracks page, a Trim, the Assist EQ | Live |
| Analyze Mix and the Mix Assistant's advice; track roles; the analyzer's track colours | Live |
| Auto Mix, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match | Live |
| EQ, Dynamics, Tone, Stereo pages: processing on the track you pick (in its Mixology Link) or on the full mix | Live |
| Mono Check (Stereo page) | Live |
| Space page: reverbs, delay, chorus, phaser, special effects on a track or an FX channel, ducking | Live |
| Polish (and Auto Mix): EQ, compression, vocal reverb / delay and the mix's tone proposed for you | Live |
| Master page's finishing knobs (Glue, Tilt, Width, Trim on the Full Mix) | Live |
| Suite link: Voxology on your vocals (VOX tag, no second compressor / reverb / delay), Masterology's goal on the Master page, the hand-off to Masterology | Live (needs Voxology 0.52 and Masterology 0.29.3 or newer) |
| Setup check: a mono group or output on the way, vocals without Mixology Link (Mix Assistant, Tracks page) | Live |
| Presets: Mixology's starting points and yours, for tracks, FX channels and the Full Mix | Live |
| History: every change Mixology makes, step by step; go back to any point and forward again | Live |
| Mixology opens as you left it: the History, the advice and the restore points come back with the project | Live (passed in Cubase) |
| Your ears per vocal track: Too quiet / Sounds right / Too loud for one lead vocal track | Live |
| The Mix Assistant knows what's fixed (Done points) and opens the tool that fixes a point | Live: this is what we're testing now |

Nothing changes your sound until you ask: every processing module starts switched off (the audio passes
through bit for bit). What can change it: the Cubase faders and pans Mixology moves, the Assist EQ / Trim in
each Mixology Link, and the modules you switch on (see "Processing" below).

## Where to put it

Insert **Mixology** on the **Stereo Out** (after your mix, before Masterology). It measures the whole mix
there (FL Studio: the Master insert; Ableton Live: the Main track). The Tracks page shows your channels through the
DAW bridge.

Insert **Mixology Link** on the tracks you want Mixology to hear one by one, after the track's other plug-ins.
It is light (no latency, very little CPU) and passes the sound through, apart from its Trim and the processing
you switch on for that track.

## Your DAW

Settings (the gear) > **DAW**. Mixology finds the program it runs in (Cubase or Nuendo, Studio One, FL Studio, Ableton
Live, REAPER, Bitwig Studio, Cakewalk by BandLab / Sonar) and writes every tip, step and fix for it: where the master channel is, how to lower a
clip's gain, add a send, make a mono group stereo, keep the outputs stereo. "Found: ..." uses the one it found; pick
another if it got it wrong. Voxology and Masterology share it: pick it in any of the three and the others follow (the
newest choice wins, also after you reopen the project). BandLab: pick **Cakewalk** (BandLab's desktop DAW for
Windows). BandLab's web studio (in the browser or the app) can't load plug-ins, so the three can't run there.

**Moving faders works in Cubase, FL Studio, Ableton Live, Logic Pro and Cakewalk.** Mixology moves their faders
through a small bridge script in the DAW, or (Logic Pro, Cakewalk) as the DAW's Mackie Control surface (see "The DAW
bridge" below); the Bridge tab in Settings is named after your DAW. In another DAW, Analyze Mix, the Mix Assistant, Mixology Link and every tool's EQ, compression, space and the mix's tone
work as usual; the Tracks page and the fader and pan moves don't: Auto Mix and Balance list those moves for you to set
by hand (untick their rows to apply the rest). A level and pan control inside each Mixology Link, so this works in any
DAW, is planned (docs/PLAN.md, "Link Mix mode").

## Light or dark

Click the **beaker logo** (top left). Same layout and colours; the background and knobs turn white.
Mixology remembers your choice.

## Pages

- **Overview**: the mix analyzer (your whole mix as a rainbow, and each kind of track as a coloured line:
  Kick, Bass, Drums, Vocals, Instruments, Other), L / R peak meters and loudness, six quick tone knobs (the full
  mix's EQ bands: the EQ page with Full Mix picked shows the same), **Analyze Mix**, and the Mix Assistant.
- **Tracks**: the MixConsole as Mixology sees it, 12 channels at a time. The scroll bar under the strips moves
  through all of them.
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
- **Master**: loudness over the last minute, a checklist for handing the mix to Masterology (the peaks and overall
  loudness count from the last time you pressed play), and four finishing
  knobs on the Full Mix (before Masterology): **Glue** (gentle bus compression), **Tilt** (+ brighter, − warmer),
  **Width** and **Trim** (the level going into Masterology). Leave the loudness to Masterology.

Knobs: drag up / down (hold Shift for fine steps), scroll, or use the arrow keys. Double-click resets.

## The DAW bridge (one-time setup, about 5 minutes)

Mixology moves the real mixer faders through a small script inside your DAW (Cubase's MIDI Remote, an FL Studio
controller script, an Ableton Live control surface) and two virtual MIDI ports. The Sonicology installer puts the
script for each DAW in place. Logic Pro and Cakewalk need no script: Mixology acts as a **Mackie Control** (a hardware
mixer controller both know), on the same two ports. Settings (gear) > **Bridge** (named after your DAW: "Cubase Bridge", "FL Studio
Bridge", "Ableton Live Bridge") shows these steps for the DAW you picked.

**The two ports (every DAW).**
- **Windows:** install **loopMIDI** (free, by Tobias Erichsen). Click **+** twice and name the ports exactly
  `Mixology to Cubase` and `Cubase to Mixology` (the names say Cubase; they work for every DAW). Tick "Autostart" in
  loopMIDI's tray menu.
- **Mac:** nothing to install: Mixology makes these two ports itself while it runs (from the moment the project
  plays in Cubase, FL Studio, Live or Logic; in another DAW once Mixology's window has been open). They exist only
  while the DAW runs, so don't restart the DAW to find them: open its MIDI or control surface settings again and pick
  them there.

### Cubase

1. The Sonicology installer puts the script in place. Without it: copy the `Mixology` folder from the download's
   `Cubase MIDI Remote` folder into `Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\` (Mac:
   `~/Documents/Steinberg/Cubase/MIDI Remote/Driver Scripts/Local/`).
2. Start Cubase. Open **Studio > Studio Setup > MIDI Port Setup** and untick **In 'All MIDI Inputs'**
   for both Mixology ports.
3. Open the **MIDI Remote** tab (lower zone). "Mixology Bridge" should appear on its own. If it doesn't,
   click **+** (Add MIDI Controller Surface), pick vendor **Mixology**, model **Bridge**, input
   `Mixology to Cubase`, output `Cubase to Mixology`.
4. Open Mixology (on the Stereo Out). **Settings > Cubase Bridge** turns green, and the Tracks page shows your
   channels.

### FL Studio

1. The installer puts the script in `Documents\Image-Line\FL Studio\Settings\Hardware\Mixology Bridge` (Mac:
   `~/Documents/Image-Line/FL Studio/Settings/Hardware/Mixology Bridge`).
2. In FL Studio: **Options > MIDI settings**. In the **Input** list pick `Mixology to Cubase`, tick **Enable**, set
   **Controller type** to **Mixology Bridge** and **Port** to **200**. In the **Output** list pick
   `Cubase to Mixology` and set its **Port** to **200** too (the same number: that is how FL knows where the script's
   answers go).
3. Open Mixology (on the Master insert). **Settings > FL Studio Bridge** turns green.

Mixology's strips are the mixer inserts in use: ones you renamed, or ones with an effect on them (Mixology Link
counts). Unused "Insert 37"s are left out, and the Master is never moved.

### Ableton Live

1. The installer puts the script in `Documents\Ableton\User Library\Remote Scripts\MixologyBridge` (Mac:
   `~/Music/Ableton/User Library/Remote Scripts/MixologyBridge`). Restart Live after installing.
2. In Live: **Settings > Link, Tempo & MIDI** (Live 11: Preferences > Link / Tempo / MIDI). In the first free **Control Surface** row pick **MixologyBridge**,
   Input `Mixology to Cubase`, Output `Cubase to Mixology`.
3. Open Mixology (on the Main track). **Settings > Ableton Live Bridge** turns green.

Mixology's strips are Live's tracks, then its return tracks, in mixer order; the Main track is never moved. Live
shows dB with one decimal, so an exact value you type lands within 0.05 dB.

### Logic Pro (Mac)

1. Nothing to install in Logic. Mixology (on the Stereo Out) makes the two ports as soon as the project plays.
2. Logic often finds the Mackie Control by itself (Settings > Logic Pro Bridge turns green). If not: **Logic Pro >
   Control Surfaces > Setup**, **New > Install**, pick **Mackie Designs: Mackie Control**, **Add**. With it selected,
   set **Output Port** to `Cubase to Mixology`, then **Input Port** to `Mixology to Cubase`.
3. Mixology's strips are the 8 channels the Mackie Control shows (Logic's tracks, in order). Logic shortens names to
   6 letters ("LdVcl"): Mixology matches them back to your tracks' full names through their Mixology Links. A short
   name that fits two of your tracks stays short (a guess could move the wrong fader): rename one so they differ.

### Cakewalk (Windows)

1. Nothing to install in Cakewalk (the two loopMIDI ports above are all it needs).
2. **Edit > Preferences > MIDI > Devices**: tick `Mixology to Cubase` under Inputs and `Cubase to Mixology` under
   Outputs, **Apply**.
3. **MIDI > Control Surfaces**: **Add New Controller/Surface** (the star), pick **Mackie Control**, Input Port
   `Mixology to Cubase`, Output Port `Cubase to Mixology`, **OK**, **Apply**.
4. Open Mixology (on the Master bus). **Settings > Cakewalk Bridge** turns green. The strips are the 8 tracks the
   Mackie Control shows; names are matched back to your tracks' full names as in Logic.

**How exact are Logic and Cakewalk?** A Mackie Control sees fader positions, not dB. Mixology knows where each DAW puts
0 dB and +6 dB, and learns the rest of the curve whenever the DAW's display shows a fader's dB (Logic does when you
edit a volume). Until then a move can be off by a fraction of a dB (more far below 0 dB): this is what we're testing.
Cakewalk's faders have 1024 steps, so a move lands within about 0.1 dB of what it aims for. Pans move in the DAW's
own steps (Logic: -64 to +63).

## Moving faders

Every strip on the Tracks page moves its real mixer channel (Cubase's MixConsole, FL Studio's mixer, Live's tracks):

- **Drag** the fader. **Double-click** the fader cap for 0 dB.
- **Mouse wheel** over a fader: 0.5 dB per notch (hold Shift for 0.1 dB).
- **Click the level** under the fader and type an exact value (`-6`, `0`, `-inf`), then Enter. Mixology keeps
  adjusting until your DAW shows exactly that number.
- **Pan** knob, **S** (solo) and **M** (mute).
- Moves made in your DAW show up in Mixology right away.

**More than 12 channels: the scroll bar** (top of the Tracks page) shows which 12 you see among all of them
("13–24 of 41"). Drag it, click where you want to be, or scroll the mouse wheel over it: the strips follow when you let
go (well under a second). The first time you open the Tracks page, Mixology reads every channel once to learn their
order ("Reading your channels…": the strips step through your project and come back). After you add, move or
rename tracks, touching the scroll bar reads them again.

**Padlock = protect.** Click a strip's padlock and it turns gold: Mixology will never move that channel (later
also Auto Mix won't). It is saved with the project and follows the channel when the bank moves.

**Restore.** Before Mixology changes a channel the first time, it remembers how it was. A small **↺** on a strip
puts that channel back; **↺ Restore** on the top bar puts back every channel Mixology changed (channels outside the
12 in view: move the bank to them and press Restore again). Settings > Bridge > **Keep the mix as it is
now** makes the current mix the new starting point. The restore points are saved with the project: Restore still
goes back to how the song was before Mixology's first change after you open it again.

Safety rules (always on):

- Every move names the channel you were looking at. If the bank moved or tracks were reordered or renamed in
  between, the move is refused (in Mixology, and again by the bridge script) instead of landing on another channel.
- A channel with automation **Write (W)** on is never moved (red **W** on the strip), so no automation gets
  recorded by accident (Cubase). FL Studio: no moves while it records. Live: no moves while it records with
  Automation Arm on. Logic and Cakewalk don't tell Mixology: keep Mixology's tracks in Read or Off while it moves
  faders. With **Read (R)** on (green **R**) you get a warning: if that channel has volume
  automation, playback moves the fader back.
- The Stereo Out (FL Studio's Master, Live's Main) is never part of the bank.
- Two channels with the same name: you can move them on the Tracks page, but Restore, Mixology Link and Auto Mix
  can't tell them apart (the name shows in amber, and the Mix Assistant says which name). Give one a different name.

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
   A point a smart tool can fix has a button for it (**OPEN VOCAL POCKET ›**, **OPEN BALANCE ›**, **OPEN CLEAN UP ›**,
   **OPEN POLISH ›**): it opens that tool with its changes, to look at and apply.
5. Once a tool (or you) fixed a point, it moves to the end with a green tick: **Done since Mixology listened: play
   the song to check it**. The point is still about the mix as Mixology heard it before the fix; when it hears the
   song again (Auto Analyze while it plays, or Analyze Mix) the point goes, or comes back if the fix wasn't enough
   (then the tool offers a little more).

What it looks at:

- **Vocal level**: how loud the lead vocal sits against the rest of the mix while it sings, compared with your
  style (country, pop, R&B and folk sit the vocal more forward than rock). The rest of the mix is what Mixology
  hears on the Stereo Out, so a beat without a Link still counts. Each lead vocal track is also measured on its own
  (while it sings), so you can set your ears for one track (see "One vocal track at a time" below).
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
- **Channels without Mixology Link**: listed, so you know what the advice leaves out. The list comes from the channels
  Analyze Mix reads at the start, so renamed or deleted tracks drop out of it.

**Auto Analyze** (the switch under the advice, after your first Analyze Mix): whenever the song plays, Mixology
keeps listening and checks the advice every 15 seconds; the cards change only when the advice really changed ("Auto
Analyze, updated 2:14 PM"). When you move a fader or change a track's processing, it listens again from that moment
(the first check 20 seconds later), so the advice is always about the mix as it is now. It pauses when the song stops,
while Mixology applies a plan and while you listen to your reference song; it keeps listening with Mixology's window
closed (once the window has been open, or from the start in Cubase, FL Studio, Live, Logic and Cakewalk). It
only gives advice: it never moves anything. Your choice is saved with the project: open the project again and Auto
Analyze is still on and carries on from the advice you had. (A project saved before Mixology 0.14 has no advice
saved: there it reads the faders once when you press play, as Analyze Mix does, and the first advice comes about
20 seconds in.) Pick another style in the header and the next check gives advice for that style.

**Open the project again** and the advice is still there: the Mix Assistant says **Last session (Oct 15, 9:12 PM)**
over it. Mixology keeps what Analyze Mix heard with the project, so the smart tools work straight away and a role
you change still updates the advice. With Auto Analyze on, it listens again when you press play and the cards (and
the line above them) change when the mix sounds different now. Press **Analyze Mix** any time to start fresh.

**Recorded too hot?** When Voxology's Auto-Edit finds a vocal clipped or over the top of the meter in the recording
itself, Auto Mix, Balance, Polish and Vocal Pocket show **FIX THIS FIRST** above their changes: lower that vocal's clip
gain (how, in your DAW), then let Analyze Mix hear it again (Auto Analyze does it by itself). Their level moves are worked
out from how loud the vocal is now, so fixing it after would change the balance again.

**Your tracks** (button under the Mix Assistant) shows what Analyze Mix heard for each track: its role, level
after the fader, peak and width. **Live view** switches the Mix Assistant back to the live readings.

**Track roles.** Mixology guesses each track's role from its name ("808", "Lead Vox", "Hi Hat"...), and from its
sound when the name doesn't say. It reads whole words, and what the track is wins over the part of the song:
"Keys - Verse" is keys, "Hats - Chorus" is hats, "Vox Double" is ad-libs, "Vox Delay" is an FX return. A song part on
its own ("Verse 1", "Hook") counts as a vocal. Click a strip's icon on the Tracks page (or the role in Your tracks) to set it
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
  **One vocal track at a time.** A verse and a hook on their own tracks can want different levels. On the Mix
  Assistant's vocal point, **Set it for:** lists your lead vocal tracks: click one (it lights up), and Mixology says
  where that track sits while it sings; then **Too quiet / Sounds right / Too loud** set it for that track only (the
  window stays open for the next one). **All vocals** sets the whole vocal again; **Follow the whole vocal** takes a
  track's own setting away. Balance and Auto Mix then move each vocal track to its own level (the band makes room for
  the average), and the advice checks each one. Saved with the song.
  **Room for Masterology** is a separate line at the bottom, unticked: every fader that plays goes down together
  (e.g. -12.8 dB when the mix peaks at about +6.8 dB). The balance stays the same, the mix gets quieter, Masterology
  brings the level back. Tick it last, once the balance sounds right.
- **Stereo Architect**: pans. Kick, bass, snare and the lead vocal in the middle. Pairs and doubles
  ("Gtr L" / "Gtr R", "Electric Rhythm" / "Electric Rhythm (D)", "Keys 1" / "Keys 2") spread left and right:
  if one is already out on a side, it stays and its partner mirrors it. A mix that leans to one side is evened
  out with the tracks pulling it there. It only pans **mono** tracks: for a stereo track Cubase doesn't tell
  Mixology where its panner really sits. If Cubase reports pans your mix doesn't match (stereo tracks with Cubase's
  Combined Panner show one side only, e.g. "L100" on a full-width track), it leaves the pans alone and says so.
- **Clean Up**: Assist EQ. Low cuts on tracks with rumble they don't need (on vocals about 90 Hz; on keys and synths
  50 Hz, and only for rumble below about 45 Hz: their low notes are part of the sound), and mud
  dips (200 - 500 Hz) on the tracks carrying it when the mix is muddier than your style.
- **Vocal Pocket**: Assist EQ. Gentle dips (2 - 4 dB) at 1.6 - 4 kHz in the instruments that cover the vocal's
  words, each where it overlaps the vocal most (always inside 1.6 - 4 kHz, the range the advice measures). When
  Mixology heard a track with its dip already in and it still covers the words, the dip goes a little deeper (at
  most 6 dB; past that, turn the track down or pan it instead). A dip made since Mixology listened counts as done; a
dip outside 1.6 - 4 kHz (e.g. at 5 kHz, from Mixology before 0.16) doesn't count: Vocal Pocket moves it.
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
| Dynamics | Limiter | Nothing passes the Ceiling (sample or true peaks). Adds 5 ms of delay while it's on (switching it never clicks); your DAW lines the track up, also while the plug-in is bypassed. On the full mix, leave the final limiting to Masterology. |

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
     "Return") is set to FX channel by itself the first time you switch Space on (not one whose name says it plays
     something, like "Space Pad").
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

Your DAW is told that each Mixology Link and Mixology can ring on for up to a minute (their "tail"), so a reverb or
delay is never cut off when the track goes quiet, and switching Space on or off never interrupts the sound. A Space
effect uses 0.3 - 2.3 % of one CPU core (Delay the least, Hall the most). Every feedback (Shimmer, Drift, Delay)
always dies away once the sound stops, at any setting.

## Mix styles

The style in the header (for example "Hip Hop - Modern Clean") is what Mixology aims for. Click it (or **Mix
style** at the top of PRESETS) to choose: first the **genre**, then its **style** in the pop-up. The arrows next to the name step
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

**One style for the whole song (Voxology, Mixology and Masterology).** The three plug-ins share this list of styles.
Pick the style in any of them and the others in the project follow within a second (Mixology shows "Style: ...
(picked in another plug-in of the suite)"). The newest choice wins, also after you reopen the project. What each one
does with it:
- **Mixology**: everything above (the Tone page's target, Analyze Mix, Balance and Auto Mix levels).
- **Masterology**: the Auto-Master genre the style belongs to (Hip Hop and Trap: Trap / Hip-Hop; R&B and Gospel:
  R&B; Pop, Worship, Latin and Afro: Pop; Electronic: EDM; Rock: Rock; the rest: Acoustic). Your master doesn't
  change until you run Auto-Master again.
- **Voxology**: suggests the vocal STYLE Auto-Edit aims for (Rap for Hip Hop, Melodic for melodic trap, R&B for
  R&B...), only on a vocal track where you haven't picked the vocal style yourself. One song can have rap verses and
  a melodic hook: set each track's vocal style with its arrows and it stays.

## Presets

**PRESETS** (top bar) saves and loads a track's sound: its processing (EQ, Dynamics, Tone, Stereo, Space), on any
track with Mixology Link, or the Full Mix.

1. **For:** pick the track (it starts on the one the processing pages show). Its kind is shown next to it.
2. **For 'Lead Vox'**: the presets made for that kind of track come first. Click **LOAD**.
3. Not right? **UNDO** (at the top) puts back exactly what the track had.

- **Mixology presets**: starting points for each kind of track: the lead vocal ("Sit in the Beat" when Voxology is on
  it: EQ only, no second compressor; "Clear & Upfront" without Voxology), backing vocals, ad-libs, kick, 808, snare,
  hats, percussion, keys and pads, guitar; FX channel returns (vocal plate, hall, 1/4 echo); and the Full Mix (gentle
  glue, warm and wide, bright and modern). When Voxology compresses a vocal, the presets with a compressor aren't
  offered first for it.
- **My presets**: dial in a sound you like, type a name and **SAVE**. Yours are kept for every song and every project
  (in one small file next to Mixology's settings). Saving under a name you already used asks **REPLACE?** first.
  **DELETE** asks **SURE?** first.
- A preset sets every processing control (what it doesn't use is switched off), so a track sounds the same whatever
  it had before. It never touches the fader, the Trim, the Assist EQ or what Space ducks under. A track keeps its own
  routing: an FX channel stays a 100 % wet return.

## History (undo any change Mixology made)

The **clock-arrow button** in the top bar (left of the gear) opens the **History**: every change Mixology makes, newest
first, with the time. A step is one thing you did in Mixology:
- a fader drag, a pan, mute or solo, a Trim (on the Tracks page);
- knobs turned on one page of one track ("EQ on 'Lead Vox 1'"): turning more knobs there within a few seconds adds to
  the same step, so a drag is one step, not fifty;
- a preset loaded, Space ducking set;
- Auto Mix, Polish, Balance, Clean Up, Vocal Pocket, Stereo Architect (each one step, with every fader, pan, Assist EQ
  and control it changed), and their own Undo;
- Restore (one channel, or Restore all).

Click a step to see each change ("'Kick' fader −3.2 → −5.0 dB"). Then:
- **GO BACK HERE**: the mix goes back to how it was right after that step. Every later step is undone: faders and pans
  move back in Cubase, processing and Assist EQ go back in the Links. The undone steps stay, dimmed.
- **REDO TO HERE** on a dimmed step: forward again, up to that step.
- **UNDO** / **REDO** at the top: one step at a time. **UNDO EVERYTHING** at the bottom: back to the start.
- Change something new after going back, and the dimmed steps are gone (as in any editor).

Good to know:
- A fader or pan you moved in Cubase itself after Mixology moved it stays where you put it (the log says so). Moves
  you make in Cubase are not Mixology's: Cubase's own Edit > History has those.
- Faders need the DAW bridge connected; processing changes don't. A step that moved faders waits for the bridge as a
  whole (undoing only half of it would leave the History wrong): connect the bridge, then undo it.
- A track renamed or removed since: its processing changes are skipped (the message names it).
- The history is saved with the project (the last 200 steps), the same for every Mixology window. Open the song again
  and the steps are still there, under **EARLIER SESSIONS (SAVED WITH THE PROJECT)**, with their date: GO BACK HERE and
  UNDO work on them too (faders need the DAW bridge connected). A very long history keeps its newest steps. With the
  advice, Mixology adds about 1 MB to the project file (up to about 2 MB after a two-minute Auto Analyze on 30 tracks).

## How it works with Voxology and Masterology

1. Voxology on each vocal: tuning, cleanup, de-essing, compression, space.
2. Mixology on the Stereo Out: balance all tracks against each other (levels first, then masking and width).
3. Masterology after Mixology on the Stereo Out (or in a mastering project): final EQ, loudness and limiting.

Leave Masterology room: mix peaks between −6 and −3 dBTP, and the mix no louder than about 3 dB over Masterology's
target. The Master page's HAND-OFF checks tell you (with Masterology's own target and ceiling when it's there).
Final limiting is Masterology's job: keep Mixology's Limiter off on the Full Mix (or use it only as a safety).

### The three talk to each other (the suite link)

In the same project, the three plug-ins tell each other what they do. Nothing to set up: just use Voxology 0.52,
Mixology 0.21 and Masterology 0.29.3 (or newer). Update all three together (older and newer versions don't see each
other); the Sonicology installer always has matching ones.

- **Voxology on a vocal.** The Tracks page shows a small gold **VOX** tag under the track's icon (hover it to
  see what Voxology does there: compressing, de-essing, its reverb or delay...). Mixology then:
  - treats the track as a vocal (the lead, or the ad-libs when the track's name says so),
    even when its name doesn't say "vocal". Your own choice in the role menu still wins.
  - never stacks a second compressor where Voxology compresses, a second reverb on the lead vocal where Voxology
    adds its reverb, or a ping-pong delay on ad-libs where Voxology adds its delay. Auto Mix and Polish say so in
    their notes ("Voxology compresses it already").
  - warns when the lead gets reverb from Voxology **and** from an FX channel: one space is usually enough.
  - A **BEAT** tag: Voxology in Beat mode on that track (it only listens to the beat for the key).
  - answers each Voxology: Voxology's bottom bar says **MIXOLOGY ✓** only when Mixology found that track, and
    otherwise why not (**ADD LINK**: no Mixology Link on it; **RENAME TRACK**: two tracks share the name; **NO TRACK
    NAME**: the DAW doesn't give it; **NOT A VOCAL THERE**: you set another role). After Analyze Mix it also says how
    loud the vocal sits against the beat, and Voxology's level tips say so when Mixology's bridge moves that fader.
- **Masterology after Mixology.** The Master page's HAND-OFF panel shows what Masterology aims for (for example
  "Masterology: −14 LUFS · −1.0 dBTP"; hover it for the goal and genre) and a check that it is there. Its checks
  use those numbers: peaks under −3 dBTP (2 dB under Masterology's ceiling when it's lower), and a mix more than 3 dB louder
  than the target is flagged (Masterology would only turn it down: ease off the bus compression and limiting).
- **Mixology's hand-off to Masterology.** Mixology tells Masterology the mix style you picked, the mix's peaks
  and loudness since you last pressed play, whether the mix is mono right now, and (after Analyze Mix) how its tone
  sits against the style, including what Mixology's own Full Mix EQ and Tone Balance add. Masterology shows it and
  can start from the matching genre. The Mixology on your master channel does this (with Mixology on a group too,
  the one on the channel called Stereo Out / Master / Main; when it goes, another takes over).
- **One song style** for all three (see Mix styles): pick it in any of them, the others follow. Only the project that
  plays takes part: with two projects open, the one that isn't active doesn't change the other's style or DAW, and
  loading a preset doesn't switch the song's style when the project already has one. Settings > DAW "Found: ..."
  picks the DAW Mixology found for all three.

The link only works inside one program: Voxology, Mixology and Masterology must run in the same DAW, in the same
process (they do, unless you mix in one project and master in another; then Masterology simply works on its own as
before). DAWs that can run each plug-in in its own process keep them apart: in Bitwig set Settings > Plug-ins >
Plug-in hosting to "By manufacturer" or "Together", in REAPER don't run them as separate / dedicated processes, in
FL Studio don't make them bridged.

## Performance (big sessions)

Mixology is built for 40+ tracks. Measured with 48 tracks, each with Mixology Link, and Mixology on the Stereo Out
(the CPU check, run on every build; one core of a cloud machine, 48 kHz, 256-sample buffer):

| Setup | All 48 tracks + Mixology | Per track |
| --- | --- | --- |
| As inserted (every module off) | about 7 % of one core | 0.13 % |
| Typical after Auto Mix (EQ + compressor on every track, two reverbs) | about 17 % of one core | 0.34 % |
| Every module on every track (no real mix does this) | about 2 cores | 4.3 % |

Cubase spreads the tracks over all your CPU's cores, so a typical session uses a small part of one. The Links'
analysis (Analyze Mix's ears) adds about 3 % of a core in the background.

What costs the most, per track: Multiband, Space (reverb / delay), Harmonics, Clip Control and Analog Touch (up to
about 1 % of a core each). EQ, Tone Balance, Loudness, Final Touch and Stereo are nearly free.

If Cubase crackles or the performance meter peaks:
- While mixing, use a bigger buffer (Studio > Studio Setup > your audio device > Control Panel: 256 or 512 samples). Small buffers (64)
  are for recording.
- Keep ASIO-Guard on (Studio > Studio Setup > VST Audio System): it lets Cubase process tracks ahead on all cores.
- One reverb on an FX channel (with Sends) instead of Space on every track.
- Mixology's window draws moving meters and pictures: close it when you don't need it.

## Troubleshooting

- **"Can't find the virtual MIDI ports"**: loopMIDI isn't running, or the port names differ. Names must
  contain `Mixology to Cubase` and `Cubase to Mixology`. Then press **Reconnect**.
- **"Waiting for the Mixology Bridge script"**: the script isn't active in Cubase. Check the MIDI Remote tab
  (step 4 above). After copying the script, restart Cubase or use **Reload Scripts** in the MIDI Remote
  Manager.
- **The script disappears after restarting Cubase**: a known Cubase issue with some script surfaces. Add it
  again with **+** in the MIDI Remote tab.
- **FL Studio: "Waiting for the Mixology Bridge script"**: in Options > MIDI settings, check that the Input
  `Mixology to Cubase` is enabled with Controller type **Mixology Bridge**, and that it and the Output
  `Cubase to Mixology` have the **same Port number** (200). "Mixology Bridge" missing from the Controller type list:
  the script isn't in `Settings\Hardware\Mixology Bridge` (run the installer again), then restart FL Studio.
- **FL Studio: an insert is missing from the Tracks page**: Mixology only shows inserts in use. Rename it or put an
  effect on it (Mixology Link counts), then press Refresh.
- **Ableton Live: MixologyBridge isn't in the Control Surface list**: the folder must be
  `User Library/Remote Scripts/MixologyBridge` (run the installer again), then restart Live.
- **Logic: the Bridge tab keeps waiting**: check Logic Pro > Control Surfaces > Setup has a Mackie Control with
  Output `Cubase to Mixology` and Input `Mixology to Cubase` (set the output first). Then press **Reconnect**.
- **Cakewalk: the Bridge tab keeps waiting**: both ports must be ticked in Preferences > MIDI > Devices (Inputs and
  Outputs), and the Mackie Control must use them (MIDI > Control Surfaces). Open a project: Cakewalk's Mackie Control
  only talks while one is loaded. Then press **Reconnect**.
- **Logic / Cakewalk: a strip shows a short name ("LdVcl")**: Mixology has no Mixology Link with a name that fits it,
  or two of your tracks fit it. Insert Mixology Link on the track, or rename one of the two.
- **Mac: the two Mixology ports aren't in your DAW's MIDI list**: Mixology makes them while it runs: press play once
  (or open Mixology's window), then open the DAW's MIDI or control surface settings again. Don't restart the DAW for
  them: the ports go away with it. Prefer fixed ports? Make two IAC Driver buses (Audio MIDI Setup > IAC Driver) named
  exactly `Mixology to Cubase` and `Cubase to Mixology`: Mixology uses those.
- **Two projects open in your DAW**: Mixology keeps each project's History, advice, padlocks and roles apart and
  shows the one that plays. It hears every open project's Mixology Links, though: a track called the same in both
  counts as two tracks with one name (the Mix Assistant says so). Close the project you're done with.
- **Connected, but no channels on the Tracks page**: press **Refresh** in Settings > Bridge. If
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
- **A stereo track shows "Stereo" under its pan knob (Cubase)**: it uses Cubase's Stereo Combined Panner. Cubase only tells
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
- **No VOX tag on a vocal with Voxology**: update all three to matching versions (Voxology 0.52, Masterology 0.29.3).
  Voxology's bottom bar says what's missing (ADD LINK, RENAME TRACK...). The tag follows the track's name, so
  Voxology and the Tracks page must see the same name (rename the track in Cubase if two tracks share one).
- **The Master page doesn't show Masterology**: update Masterology to 0.29.3 or newer, and put it in the same project
  (after Mixology on the Stereo Out). It appears a moment after Masterology is inserted.
- **Send us the log**: Settings > Bridge > **Copy log**, then paste it into the chat.
