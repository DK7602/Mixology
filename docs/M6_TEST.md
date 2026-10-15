# Milestone 6 tests

## Version 0.10.0 (10 minutes): the History

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
