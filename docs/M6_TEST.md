# Milestone 6 tests

## Version 0.16.0 (5 minutes): the Mix Assistant knows what's fixed

Mixology 0.16.1 (0.16.0 said "the dips are in" for the old 5 kHz dip when the advice came from a 0.15 project).
1. Open a point like **Instruments cover the vocal's words**: under HOW there is **OPEN VOCAL POCKET ›**. It opens Vocal
   Pocket with its dips (Balance, Clean Up and Polish points have theirs).
2. Apply the dips. Close the window: the point moves to the end of the list with a green tick, "Done since Mixology
   listened: play the song to check it". Open it: it says what changed.
3. With Auto Analyze on, play the song about 20 seconds: the point goes (or comes back if the dip wasn't enough;
   then Vocal Pocket offers a slightly deeper one).
4. On your song with 'Hats - Verse' (Pocket -2 dB at 5 kHz from the old Vocal Pocket): Vocal Pocket now moves it to
   where the words are (1.6 - 4 kHz) instead of saying "nothing covers the vocal".

**Report**: any point that stays after its fix, or a tool button that opens the wrong tool?

## Version 0.15.0 (5 minutes): your ears per vocal track

Mixology 0.15.0 only.
1. Analyze Mix on a song with two or more lead vocal tracks (e.g. Lead Vox 1 - Verse, Lead Vox 2 - Chorus). Open the
   Mix Assistant's vocal point.
2. **Set it for:** shows **All vocals** and each vocal track. Click one: it lights up, and the line under it says where
   that track sits while it sings.
3. Press **Too quiet** (or Sounds right / Too loud): the window stays open with that track still picked; its line now
   says "this track's setting". Set another track the same way.
4. Open **BALANCE**: the track you called too quiet comes up against the band, the ones that sound right stay where
   they are against it.
5. **Follow the whole vocal** on a track takes its own setting away. Close and reopen the project: the settings stay.

**Report**: does each track end up where your ears want it?

## Version 0.14.0 (10 minutes): Mixology opens as you left it (passed)

Mixology 0.14.0 only (Voxology 0.50.1 and Masterology 0.29.1 stay).
1. In a song: Analyze Mix (or let Auto Analyze give advice), then change a few things in Mixology (a fader on the Tracks
   page, a knob on the EQ page, a preset, Auto Mix). Open the **History** (clock-arrow button): the steps are there.
2. Save the project, close it (or quit Cubase), open it again and open Mixology.
3. **History**: the same steps, under **EARLIER SESSIONS (SAVED WITH THE PROJECT)**, with their date. Press **GO BACK
   HERE** on one of them: the faders and knobs go back, as before.
4. **Advice**: the Mix Assistant shows the same cards, with **Last session (date, time)** over them. Open AUTO MIX: it
   works straight away, no Analyze Mix needed. With Auto Analyze on, press play: after about 20 seconds the line
   changes to "Auto Analyze, updated ..." (and the cards, if the mix sounds different now).
5. **Restore**: a fader Mixology moved before you closed still shows its ↺ on the Tracks page, and ↺ puts it back to how
   it was before Mixology's first change.

**Report**: anything that didn't come back, or came back wrong?

## Version 0.13.0 (10 minutes): your DAW, and "fix this first"

Install all three together: Mixology 0.13.0, Voxology 0.50.0 and Masterology 0.29.0 (older ones don't see the new link).
1. **FIX THIS FIRST**: on the vocal Voxology called TOO HOT, open Mixology's **AUTO MIX**. At the top, an orange box says
   Voxology heard it recorded too hot and how to lower its clip gain in Cubase. Lower it (the event's volume handle) until
   the loudest words peak around -6 dB, run Auto-Edit in Voxology again, and the box goes.
2. **DAW**: Mixology > Settings (gear): the top line says **DAW: Found: Cubase**. Pick Studio One: the tips change to
   Studio One's words (open the Mix Assistant's cards, Auto Mix's notes). Voxology's bottom bar (DAW, next to the
   version) and Masterology's Settings follow within a second.
3. Pick **Found: Cubase** again in any of the three: all three go back to Cubase.
4. (0.13.1) The list now has **Cakewalk** (by BandLab, and Sonar): BandLab's desktop DAW. BandLab's web studio can't
   load plug-ins.

**Report**: anything still saying Cubase where it shouldn't, or a step that's wrong for your DAW?

## Version 0.12.0 (10 minutes): one song style for all three

Install all three together: Mixology 0.12.0, Voxology 0.49.0 and Masterology 0.28.0 (older ones don't see the new
style link). Use a project with Voxology on a vocal, Mixology on the Stereo Out and Masterology after it.
1. In **Mixology**, pick a style in the header (for example Trap & Drill - Melodic).
2. Open **Masterology**: its genre pill now shows "TRAP & DRILL / Melodic" (Auto-Master's genre: Trap / Hip-Hop).
   Click the name: the same list of styles as Mixology. Pick R&B & Soul - Smooth R&B.
3. Back in **Mixology**: within a second the header says R&B & Soul - Smooth R&B, with a note at the bottom.
4. Open **Voxology** on the vocal: the small chip next to STYLE says "SONG · SMOOTH R&B" and its vocal STYLE says R&B
   (suggested, because you never picked it on that track). Now pick Melodic with the arrows, then change the song
   style again in Mixology: Voxology's vocal style stays Melodic (your choice).
5. Save, close and reopen the project: all three still show the last style you picked.

**Report**: did all three always agree? Did anything change that you didn't expect?

## Version 0.11.1 (5 minutes): Auto Analyze stays on

1. With Auto Analyze on, save the project, close Cubase, open the project again and open Mixology.
2. The switch is still green, and the Mix Assistant says "Auto Analyze is on". Do NOT press Analyze Mix.
3. Press play. The faders get read once (the strips step through, like Analyze Mix), then "Listening 5 s"... About
   20 seconds in, the advice appears ("Auto Analyze, updated (time)").
4. Pick another style in the header and keep playing: within about 20 seconds the advice is for the new style.

**Report**: did it come back on by itself and give advice without Analyze Mix?

## Version 0.11.0 (10 minutes): Auto Analyze, and the logo centred (passed)

1. Look at the logo: the beaker sits in the middle, the same amount of waveform on each side, same size as before.
2. Press **ANALYZE MIX** and let it finish. Under the advice there is a new switch: **AUTO ANALYZE**. Click it: the
   light turns green.
3. Play the song. The switch says "Listening 5 s" and so on. About 20 seconds in, Mixology checks the advice. The cards
   only change when something really changed: then the line at the top reads "Auto Analyze, updated (time)".
4. Now change something the advice talks about, for example pull the lead vocal's fader down 6 dB, and keep playing.
   The count starts again from 0 (a new version of your mix). About 20 seconds later the vocal advice should change.
5. Stop the song: the switch says "Press play". Click the switch again to turn it off.

**Report**: did the advice follow your change? Did the cards ever jump around while nothing changed?

## Version 0.10.0 (10 minutes): the History (passed)

You asked for an undo history. The new **clock-arrow button** in the top bar (left of the gear) opens it.
1. Make a few changes in Mixology: drag a fader on the Tracks page, turn an EQ knob on a vocal, load a preset, run
   **Auto Mix** (Apply). Open the History: each is one step, newest first. Click one to see what it changed.
2. Click **GO BACK HERE** on the first step (the fader drag): the later steps go dim, the faders move back in Cubase,
   the EQ and the preset go back. Listen.
3. **REDO** at the top brings back one step; **REDO TO HERE** on the newest brings back everything.
4. Go back one step, then turn a knob: the dimmed steps are gone (as in any editor).

**Report**: anything that didn't go back (or went back wrong)? Anything missing from the list?

## Version 0.9.2 (5 minutes): your three bugs from the presets test

Passed on your side already: the scroll bar (0.9.1) and the mono Beat warning (0.8.1). Thank you.
1. **PRESETS**: the **For:** list now lists Full Mix and every track with a Mixology Link. Pick **Lead Vox 1**: the
   vocal presets come first. Pick another track: the list follows.
2. **Channels without Mixology Link**: press **Analyze Mix** again. Only channels that really have no Link (here:
   **Beat**, unless it is marked as your music group) should be listed, never old names from before you renamed tracks.
3. **Icons from names**: "Keys - Verse", "Bass - Verses" and "Hats - Chorus" show keys, bass and hats, not a mic.
   A track named only "Verse 1" or "Hook" still shows a mic.

**Report**: any track whose icon is still wrong? Tell me its name and what it is.

## Version 0.9.1 (3 minutes): the Tracks page scroll bar (passed)

You asked for a scroll bar instead of the arrows. Install Mixology 0.9.1, open a song with more than 12 channels, go to
**Tracks**. The first time, "Reading your channels…" (the strips step through the project and come back). Then:
1. Drag the gold bar to the right: the text shows "13–24 of 41" and so on; let go and the strips jump there.
2. Click anywhere on the bar, and scroll the mouse wheel over it (one channel per notch).

**Report**: does it land on the right channels every time, also at the very end of the project?

## Version 0.9.0 (10 minutes): presets

1. Install Mixology 0.9.0 (both .vst3). Open Mixology, click **PRESETS** (top bar).
2. **For:** pick **Lead Vox 1**. The first presets are for vocals; with Voxology on it, "Lead Vocal: Sit in the Beat"
   comes first (EQ only: Voxology already compresses). **LOAD** it and listen. **UNDO** puts it back.
3. Pick your 808 (or kick) and load its preset; pick **Full Mix** and try "Mix Bus: Gentle Glue".
4. Make a sound you like on a track (any page: EQ, Dynamics...), then in PRESETS type a name and **SAVE**. Open another
   song: your preset is there under **My presets**.

**Report**: did the presets sound like a good starting point? Anything confusing in the sheet?

## Version 0.8.1 (5 minutes): the setup check

From your round 1: the **VOX tag works** (thank you). You also hit two setup problems by hand: the mono "Beat" group
and vocals without Mixology Link. Mixology now spots both while the song plays and says what to do.

1. Use Mixology 0.8.1 or newer (Voxology and Masterology stay as they are).
2. To see it work, put the problem back for a minute: switch **Beat** to mono again (its one-circle button) and press
   play. Within about 10 seconds the **Mix Assistant** (Overview) shows **"Your mix arrives in mono"** at the top, and the
   Tracks page's top bar says it too. Click it: it should name **'Beat'**. Switch Beat back to stereo: the warning goes
   within a few seconds of playing.
3. Remove Mixology Link from one vocal (or add Voxology to a track without a Link): the Mix Assistant says that vocal has
   no Mixology Link. Put the Link back.

**Report**: did it name the right channel? Anything it warned about that wasn't a problem?

## Round 1: Voxology, Mixology and Masterology talk to each other (about 15 minutes)

Goal: in one Cubase project the three plug-ins now share what they do. Each still works on its own exactly as before.
**Use a copy of a project** (File > Save As... first) that has Voxology on your vocals.

### A. Install all three (new versions of each)

1. Close Cubase.
2. **Mixology 0.8.0**: download as usual (Mixology repo, Actions, latest green run). Copy both .vst3 folders
   (Mixology and Mixology Link). The Cubase script didn't change (still Bridge v4).
3. **Voxology 0.47.0**: Voxology repo, Actions, latest green **Build & Test** run, artifact **Voxology-Windows**. Copy
   Voxology.vst3 (and HoneyTune.vst3).
4. **Masterology 0.26.0**: polisher repo, Actions, latest green run, artifact **Masterology-Windows**. Copy Masterology.vst3.
5. Start Cubase, open the project. Check the versions: Mixology's ? page, **VOXOLOGY v0.47.0** at Voxology's bottom
   right, **Masterology v0.26.0** in Masterology's settings (gear) pop-up.

### B. Voxology tells Mixology about your vocals

1. Open Mixology (on the Stereo Out), **Tracks** page. Every track with Voxology on it shows a small gold **VOX** tag
   under its icon. A Voxology in BEAT mode shows **BEAT**.
2. Hover a VOX tag: it says what Voxology does on that vocal (compresses about N dB, de-esses, adds its reverb...).
3. Open a Voxology window: the bottom bar ends with **MIXOLOGY ✓**.

**Report**: are the tags on the right tracks? Anything missing or on the wrong track?

### C. Auto Mix leaves Voxology's work alone

1. **Analyze Mix** (play about 30 seconds), then **Polish** (or Auto Mix).
2. Look at the list and the notes under it:
   - no compressor is proposed on a vocal Voxology compresses (note: "Voxology compresses it already"),
   - no reverb for the lead vocal when Voxology adds its reverb (note: "has Voxology's reverb"),
   - no ping-pong delay on ad-libs that have Voxology's delay,
   - if the lead has Voxology's reverb **and** your FX channel's reverb, a note says one space is usually enough.

**Report**: do the notes match what your Voxologys do?

### D. Masterology after Mixology

1. Put **Masterology in the last insert slot of the Stereo Out**, after Mixology.
2. Mixology, **Master** page: under HAND-OFF it now says what Masterology aims for (for example
   "Masterology: −9 LUFS · −1.0 dBTP") and a new check says Masterology is on.
3. In Masterology, click **Stream −14**: within a second Mixology shows −14 LUFS.
4. In Masterology, play the song with **LEARN ON**: the Learn panel shows **FROM MIXOLOGY** (the style you picked in
   Mixology, how loud the mix comes in and its peaks; after Analyze Mix also its tone against the style).
   - If Mixology's style is another genre than Masterology's (for example R&B vs Trap / Hip-Hop), the note is at the top
     with a button **USE R&B (MIXOLOGY'S STYLE)**: click it, the genre at the top changes.
   - Your mix played mono at the Stereo Out last time: Learn should say **YOUR MIX IS MONO** with what to check.

**Report**: what does Masterology's FROM MIXOLOGY note say? Does Mixology's Master page follow Masterology's target?
