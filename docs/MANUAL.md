# Mixology manual

Manual for version 0.1.0 (first build: the interface, live meters, and the Cubase bridge test).

**Voxology** makes each vocal sound great. **Mixology** makes all your tracks work together.
**Polisher** turns the finished mix into the master.

## What works in this build

| Part | Status |
| --- | --- |
| Interface: all 8 pages, light / dark look | Ready |
| Overview analyzer, loudness, peaks, headroom check | Live (what Mixology hears on its channel) |
| Stereo page: vectorscope and correlation | Live |
| Master page: loudness history, hand-off check for Polisher | Live |
| Tracks page: your real Cubase channels (names, faders, pan, mute, solo) | Live, through the Cubase bridge |
| Moving one Cubase fader from Mixology (test channel) | Live: this is what we're testing now |
| EQ, Dynamics, Space, Tone controls | Preview: they move but don't process audio yet |
| Analyze Mix, Auto Mix, Vocal Pocket, Mix Assistant suggestions | Coming (Milestones 3 and 4) |

Mixology passes your audio through untouched in this build.

## Where to put it

Insert **Mixology** on the **Stereo Out** (after your mix, before Polisher). It measures the whole mix
there. The Tracks page shows your channels through the Cubase bridge.

## Light or dark

Click the **beaker logo** (top left). Same layout and colours; the background and knobs turn white.
Mixology remembers your choice.

## Pages

- **Overview**: the mix analyzer (your whole mix, coloured by frequency), L / R peak meters and
  loudness, quick tone knobs, and the Mix Assistant (what Mixology hears right now).
- **Tracks**: the MixConsole as Mixology sees it, 12 channels at a time. Arrows move the bank.
  The colour bar on top of each strip is the Cubase track colour.
- **EQ**: six bands. Drag a point to change frequency and gain; scroll on it to change the width (Q).
- **Dynamics**: Compressor, Multiband, Transient, Gate / Expand, Limiter.
- **Space**: Reverb, Delay, Chorus, Phaser, Hall, Room, Special FX, each with its own controls,
  plus EQ and Ducking (the effect gets quieter while the vocal sings).
- **Tone**: your mix's tonal balance against the style's target (red line), and loudness.
- **Stereo**: how wide your mix is, and whether it holds up in mono.
- **Master**: loudness over the last minute and a checklist for handing the mix to Polisher.

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

## Moving a fader (the test channel)

While we prove the bridge, Mixology only moves **one channel you choose**:

1. On the **Tracks** page, click the **padlock** on a channel. It turns green.
2. Drag that channel's fader, pan, **S** or **M**, or use **Settings > Cubase Bridge** to type an exact
   value (for example `-6`) and press **Set dB**.
3. **Restore original** puts the channel back to how it was before Mixology's first move.

Safety rules (always on):

- Only the locked channel can move. Every move carries the channel's name and Cubase's channel ID, and the
  Cubase script refuses it if anything else sits in that slot when it arrives.
- If the channel moves (bank change, tracks reordered or hidden) or is renamed, Mixology **pauses** and
  asks you to lock it again.
- A channel with automation **Write (W)** on is never moved, so no automation gets recorded by accident.
- The Stereo Out is never part of the bank.

## Mix styles

The style in the header (for example "Hip Hop - Modern Clean") is what Mixology aims for. Today it sets
the Tone page's target curve. Later it steers Auto Mix and the Mix Assistant.

## How it works with Voxology and Polisher

1. Voxology on each vocal: tuning, cleanup, de-essing, compression, space.
2. Mixology on the Stereo Out: balance all tracks against each other (levels first, then masking and width).
3. Polisher after Mixology (or in a mastering project): final EQ, loudness and limiting.

Leave Polisher 3 to 6 dB of headroom: the Master page tells you when the mix peaks are in the right place.
Mixology never limits the mix bus; that's Polisher's job.

## Troubleshooting

- **"Can't find the virtual MIDI ports"**: loopMIDI isn't running, or the port names differ. Names must
  contain `Mixology to Cubase` and `Cubase to Mixology`. Then press **Reconnect**.
- **"Waiting for the Mixology Bridge script"**: the script isn't active in Cubase. Check the MIDI Remote tab
  (step 4 above). After copying the script, restart Cubase or use **Reload Scripts** in the MIDI Remote
  Manager.
- **The script disappears after restarting Cubase**: a known Cubase issue with some script surfaces. Add it
  again with **+** in the MIDI Remote tab.
- **Paused**: the locked channel moved or was renamed. Click its padlock again.
- **Strange notes on an instrument track**: untick "In 'All MIDI Inputs'" for both Mixology ports (setup step 3).
- **Send us the log**: Settings > Cubase Bridge > **Copy log**, then paste it into the chat.
