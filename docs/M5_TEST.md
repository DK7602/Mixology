# Milestone 5 tests

## Fix: the Electric guitars at "L100" (version 0.6.1, 5 minutes)

What happened: those four channels have no working pan in Cubase (mono tracks going into a mono group), and
Cubase reports such a channel as "L100". When Mixology set them to C, the Cubase script answered "done" (it repeated
what it was sent), so Mixology believed it; the next time Cubase reported them (Auto Mix walking the channels),
they read "L100" again. Now Mixology catches that, shows **No pan** on those strips, remembers it with the
project and never touches those pans again. The Cubase script is updated too (v4: it only reports what Cubase
really has).

1. Close Cubase. Copy both .vst3 folders as usual, **and** copy the `Mixology` folder from the download's
   `Cubase MIDI Remote` folder into `Documents\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\` again
   (replace it). Start Cubase. The Tracks page should say **Bridge v4**.
2. Tracks page: turn each Electric guitar's pan knob a little. Within a second each one reads **No pan**.
3. Run Auto Mix: the guitars stay **No pan**. Save, close and reopen the project: still **No pan**.
   **Report**: does it work?

## Round 2: Space (reverb, delay, effects, ducking), about 30 minutes

Goal: the Space page now makes real sound. A track (or an FX channel) gets one effect from the list on the left; the
effect can duck under your vocal. **Use a copy of a project** (File > Save As... first).

When something doesn't match: a screenshot of the page and which step number. If Cubase misbehaves,
Settings (gear) > Cubase Bridge > **Copy log** and paste it into the chat.

### A. Install

1. Close Cubase. Copy **both** `Mixology.vst3` and `Mixology Link.vst3` into `C:\Program Files\Common Files\VST3\`
   (replace both: an older Link can't talk to this Mixology). The Cubase script did not change.
2. Open your project copy. Play it: it should sound exactly as before (Space starts off everywhere).

### B. A vocal reverb on an FX channel (the way pros do it)

3. In Cubase: **Project > Add Track > Effect**. In the plug-in list pick **Mixology Link**, then **Add Track**.
   Cubase makes an FX channel (named something like "FX 1-Mixology Link") with Mixology Link on it.
4. Open the **MixConsole**. On your lead vocal channel, open its **Sends**: in the first slot pick the new FX
   channel, switch the send on and set it to about **−10 dB**. (Mixology can't make sends for you.)
5. In Mixology: **SPACE** page. Top: pick the FX channel in the dropdown. Left: **REVERB**. Press **ON**.
   **USED AS** should say **FX channel (100 % wet)** by itself (the name looks like an FX channel).
6. Play the song. You hear the vocal with a plate reverb. The picture comes alive with the beat; the badge on the
   picture says "In use on FX 1-...". Try the presets (top right) and **DECAY** 1 s, then 4 s.
   **Report**: do you hear the reverb? Does DECAY change how long it rings?
7. After the last word of a vocal phrase: does the reverb ring out naturally (not cut off)? **Report**.

### C. Ducking

8. Right side, **DUCKING**: **DUCK UNDER** = your lead vocal, **AMOUNT** −9 dB. While the vocal sings, the gold bar
   moves (the reverb dips); in the gaps it blooms back. Try AMOUNT −18 dB to hear it clearly.
   **Report**: does the reverb get out of the way of the words?

### D. Delay

9. Same FX channel: click **DELAY** on the left and press **ON** (it takes over from the reverb). Preset
   **Dotted 1/8**. The note under the knobs says how long one echo is at your song's tempo.
   **Report**: are the echoes on the beat? (Change the song's tempo in Cubase: they follow while it plays.)
10. **PING-PONG** 100 %: echoes bounce left / right (headphones).

### E. On a track

11. Pick a keys or guitar track, **CHORUS**, ON, **USED AS: On this track**, MIX 30 %. **Report**: wider, shimmering?
12. **ROOM** on a drum track: preset **Drum Room**, MIX 15 %.

### F. Special FX

13. On the FX channel: **SPECIAL FX** > ON, preset **Shimmer +12** (an angelic octave shine), then **Freeze Pad**
    (the effect holds the vocal like a pad; switch it off to stop it). **Report**: anything that sounds broken
    (crackles, runaway noise)?

### G. It stays

14. Save the project, close it, open it again: the effects, their settings and the ducking come back.
    The Link windows' PROCESSING strip shows the effect (for example "Delay (FX, 100 % wet) ducks under Lead Vox").
15. With two or three Space effects running: **Studio > VST Performance**. **Report**: about how high does it read?

### What to send back

Steps 6, 7, 8, 9, 11, 13, 14 and 15, and a screenshot of the Space page while the reverb works.

## Round 1 retest (if you haven't yet, about 10 minutes)

What your first test found, and what changed:
- **ON / OFF buttons didn't react** (EQ, Transient, Tone Balance, Stereo...): the button redrew itself 30 times a
  second, so a normal click got lost. Fixed. (Turning a knob worked, that's why those turned modules on.)
- **Tone and Stereo seemed to do nothing**: checked with the real plug-in: Tone Balance Warmth +6 lifts the lows by
  5.8 dB, Width 200 % doubles the stereo spread. Your mix reaches the Stereo Out in **mono**, so Width on the
  Full Mix has nothing to widen: the Stereo page now says so.
- **Electric guitars at "L100"** though you hear them in both ears: those channels have no working pan in Cubase
  (most likely they feed a mono group), and Cubase then reports "L100". The first time Cubase ignores a pan move,
  Mixology now shows **No pan** under that strip and never touches its pan again.
- **Auto Mix had fewer changes after Undo**: Undo could skip a fader it had moved, and a tool opened while Undo was
  still moving faders saw them half-way. Both fixed.

1. **Power buttons**: on the EQ, Dynamics (Transient), Tone (Tone Balance) and Stereo pages, click **ON** / **OFF**
   a few times. **Report**: does every click switch it?
2. **Tone**: pick a track (or Full Mix), Tone Balance ON, **WARMTH** +6 dB then −6 dB, **AIR** +6 dB.
   **Report**: do you hear it now?
3. **Pans**: Tracks page, turn one Electric guitar's pan knob in Mixology. After a second it should read **No pan**.
   **Report**: does it?
4. **Auto Mix**: Analyze Mix, open Auto Mix, note how many changes, Apply, **Undo**, wait for the list to come back.
   **Report**: same number of changes as the first time?

## Round 1 original steps (EQ, Dynamics, Tone and Stereo)

Goal: the EQ, Dynamics, Tone and Stereo pages now change the sound of the track you pick (inside its Mixology
Link) or of the full mix (inside Mixology on the Stereo Out). Nothing changes until you switch something on.
About 30 minutes. **Use a copy of a project** (File > Save As... first).

When something doesn't match: a screenshot of the page and which step number. If Cubase misbehaves,
Settings (gear) > Cubase Bridge > **Copy log** and paste it into the chat.

### A. Install

1. Close Cubase. Copy **both** `Mixology.vst3` and `Mixology Link.vst3` into
   `C:\Program Files\Common Files\VST3\` (replace both: they must come from the same download, an older Link
   can't talk to this Mixology). The Cubase script did not change.
2. Open your project copy (Mixology on the Stereo Out, Mixology Link on your tracks, as before).
   Open one Link window: it is a little taller and has a new strip at the bottom: **PROCESSING: Nothing on**.

### B. Nothing changes until you ask

3. Play the song. Bypass Mixology and a few Links in Cubase and back on.
   **Report**: does it sound exactly the same either way? (It should: everything starts switched off.)

### C. EQ (one track)

4. Open the **EQ** page. Top right, pick your lead vocal in the dropdown (the list shows every track with a
   Mixology Link, plus **Full Mix**). The page says "Off on 'Lead Vox'".
5. Drag the light-blue point (2.5 kHz) up to about +3 dB while the song plays. The EQ switches itself on (green
   **ON**). You hear the vocal get clearer. The vocal's Link window now shows **EQ** in its PROCESSING strip.
6. Press **ON** to switch it off and on again: compare. Try **Smooth** and **Natural** (the curve changes shape).
   Double-click a point: that band goes back to flat.
7. In Cubase, open the vocal's Link automation lanes (or its generic editor): "EQ 4 Gain" shows your move.
   **Report**: does the sound follow the page right away (no clicks, no delay you notice)?

### D. Dynamics

8. **DYNAMICS** page (same track stays picked). **COMPRESSOR**: turn **THRESHOLD** down until the orange GR
   meter shows 3 - 6 dB while the vocal sings. The white dot rides the curve with the vocal's level.
   **Report**: does the GR meter move with the singing? Does the vocal sound more even?
9. **GATE / EXPAND** on a track with noise between notes (or **TRANSIENT** on a drum: Attack +50 %).
10. **LIMITER** on any track: Ceiling −3 dB. The note says "5.3 ms lookahead" (or similar): Cubase lines the
    track up, so it stays in time. **Report**: does that track stay in time with the others?
11. **Reset this module** (bottom left) puts the knobs back to where they started.

### E. Tone

12. **TONE** page. **TONE BALANCE** on the vocal: Air +2 dB, Presence +1 dB. The gold line shows what it does.
13. Quick listen to **HARMONIC** (Drive 40 %, Mix 50 %), **ANALOG TOUCH** (Tape 50 %), **CLIP CONTROL** on a
    drum bus or loud track (Threshold −6 dB). The green lamps on the left show which modules are on.
    **Report**: anything that sounds broken (crackles, distortion you didn't ask for)?

### F. Stereo

14. **STEREO** page, pick a stereo track (keys, pads, a stereo guitar): **WIDTH** 150 %, then 50 %.
    **LOW WIDTH** 0 % on a stereo bass / 808 keeps its lows in the middle.
15. **MONO CHECK** (left): a red "LISTENING IN MONO" badge; the whole mix plays in mono. Go to another page:
    it switches off by itself. **Report**: does mono check work, and switch off when you leave?

### G. Full mix

16. Pick **Full Mix** in any page's dropdown (all four pages share the pick). A gentle EQ (e.g. +1 dB air with
    the pink point) works on the whole mix, before Masterology. Switch it off again.
17. **ANALYZE MIX** still works as before (it hears the mix before Mixology's own processing).

### H. It stays

18. Save the project, close it, open it again. Every track's processing and the Full Mix processing are back.
19. In a Link window press **On** (bottom right) to switch all of that track's processing off and on: a quick
    before / after. **Report**: does it work?
20. With processing on 10 or so tracks: Cubase's performance meter (Studio > VST Performance).
    **Report**: about how high does it read?

### What to send back

- Steps 3, 5 - 7, 8, 10, 13, 15, 18 and 20 (what you saw / heard).
- A screenshot of the Dynamics page while the compressor works.
