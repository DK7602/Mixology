# Milestone 5, round 1 test: EQ, Dynamics, Tone and Stereo make sound

Goal: the EQ, Dynamics, Tone and Stereo pages now change the sound of the track you pick (inside its Mixology
Link) or of the full mix (inside Mixology on the Stereo Out). Nothing changes until you switch something on.
About 30 minutes. **Use a copy of a project** (File > Save As... first).

When something doesn't match: a screenshot of the page and which step number. If Cubase misbehaves,
Settings (gear) > Cubase Bridge > **Copy log** and paste it into the chat.

## A. Install

1. Close Cubase. Copy **both** `Mixology.vst3` and `Mixology Link.vst3` into
   `C:\Program Files\Common Files\VST3\` (replace both: they must come from the same download, an older Link
   can't talk to this Mixology). The Cubase script did not change.
2. Open your project copy (Mixology on the Stereo Out, Mixology Link on your tracks, as before).
   Open one Link window: it is a little taller and has a new strip at the bottom: **PROCESSING: Nothing on**.

## B. Nothing changes until you ask

3. Play the song. Bypass Mixology and a few Links in Cubase and back on.
   **Report**: does it sound exactly the same either way? (It should: everything starts switched off.)

## C. EQ (one track)

4. Open the **EQ** page. Top right, pick your lead vocal in the dropdown (the list shows every track with a
   Mixology Link, plus **Full Mix**). The page says "Off on 'Lead Vox'".
5. Drag the light-blue point (2.5 kHz) up to about +3 dB while the song plays. The EQ switches itself on (green
   **ON**). You hear the vocal get clearer. The vocal's Link window now shows **EQ** in its PROCESSING strip.
6. Press **ON** to switch it off and on again: compare. Try **Smooth** and **Natural** (the curve changes shape).
   Double-click a point: that band goes back to flat.
7. In Cubase, open the vocal's Link automation lanes (or its generic editor): "EQ 4 Gain" shows your move.
   **Report**: does the sound follow the page right away (no clicks, no delay you notice)?

## D. Dynamics

8. **DYNAMICS** page (same track stays picked). **COMPRESSOR**: turn **THRESHOLD** down until the orange GR
   meter shows 3 - 6 dB while the vocal sings. The white dot rides the curve with the vocal's level.
   **Report**: does the GR meter move with the singing? Does the vocal sound more even?
9. **GATE / EXPAND** on a track with noise between notes (or **TRANSIENT** on a drum: Attack +50 %).
10. **LIMITER** on any track: Ceiling −3 dB. The note says "5.3 ms lookahead" (or similar): Cubase lines the
    track up, so it stays in time. **Report**: does that track stay in time with the others?
11. **Reset this module** (bottom left) puts the knobs back to where they started.

## E. Tone

12. **TONE** page. **TONE BALANCE** on the vocal: Air +2 dB, Presence +1 dB. The gold line shows what it does.
13. Quick listen to **HARMONIC** (Drive 40 %, Mix 50 %), **ANALOG TOUCH** (Tape 50 %), **CLIP CONTROL** on a
    drum bus or loud track (Threshold −6 dB). The green lamps on the left show which modules are on.
    **Report**: anything that sounds broken (crackles, distortion you didn't ask for)?

## F. Stereo

14. **STEREO** page, pick a stereo track (keys, pads, a stereo guitar): **WIDTH** 150 %, then 50 %.
    **LOW WIDTH** 0 % on a stereo bass / 808 keeps its lows in the middle.
15. **MONO CHECK** (left): a red "LISTENING IN MONO" badge; the whole mix plays in mono. Go to another page:
    it switches off by itself. **Report**: does mono check work, and switch off when you leave?

## G. Full mix

16. Pick **Full Mix** in any page's dropdown (all four pages share the pick). A gentle EQ (e.g. +1 dB air with
    the pink point) works on the whole mix, before Masterology. Switch it off again.
17. **ANALYZE MIX** still works as before (it hears the mix before Mixology's own processing).

## H. It stays

18. Save the project, close it, open it again. Every track's processing and the Full Mix processing are back.
19. In a Link window press **On** (bottom right) to switch all of that track's processing off and on: a quick
    before / after. **Report**: does it work?
20. With processing on 10 or so tracks: Cubase's performance meter (Studio > VST Performance).
    **Report**: about how high does it read?

## What to send back

- Steps 3, 5 - 7, 8, 10, 13, 15, 18 and 20 (what you saw / heard).
- A screenshot of the Dynamics page while the compressor works.
