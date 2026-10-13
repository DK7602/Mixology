# Milestone 2 test: the whole Tracks page is live, plus Mixology Link

Goal: every strip on Mixology's Tracks page moves its real Cubase channel (safely), and **Mixology Link** on a
track shows that track's level in Mixology. About 30 minutes. Work in a **copy** of a project.

When something doesn't match: Settings (gear) > Cubase Bridge > **Copy log**, paste it into the chat with
Claude, and say which step number failed. A short screen recording helps too.

## A. Install

1. Copy **both** plug-ins from the download's `VST3` folder into `C:\Program Files\Common Files\VST3\`:
   `Mixology.vst3` (replace the old one) and the new `Mixology Link.vst3`. Close Cubase first.
   - The Cubase script did **not** change (still v3): nothing to copy there.
   - Start Cubase. If Mixology Link doesn't show up: Studio > VST Plug-in Manager > rescan.

## B. Every strip moves

2. Open the test project, Mixology on the Stereo Out, **Tracks** page. There is no "test channel" any more:
   drag the faders of three different channels.
   - Expect: each Cubase fader follows; no other fader moves.
3. Click the **dB number** under a strip, type `-6`, press Enter. Then `0`, `-12`, `+6`, `-inf`.
   - Expect: Cubase shows exactly that value (within 0.05 dB). `-inf` shows "-oo" in Cubase.
4. Mouse wheel over a fader: each notch is 0.5 dB (hold Shift: 0.1 dB). Double-click the fader cap: 0 dB.
5. Pan, **S** and **M** on a few channels, on and off again.
6. Move faders, pan, mute in **Cubase**.
   - Expect: Mixology follows. The log says "Fader moved in Cubase" **once** per move (not again and again).

## C. Safety

7. Click a strip's **padlock**: it closes and turns gold (protected).
   - Expect: dragging, typing a level, S and M on it are refused with a message. The bank arrows don't change that.
   - Save, close and reopen the project: still protected. Click the padlock again to allow moves.
8. Drag a track to another place in Cubase's Project window, then move strips in Mixology.
   - Expect: the strip you touch is the channel that moves (names on the strips follow the new order).
9. Rename a track in Cubase, then move it from Mixology. Expect: works, the strip shows the new name.
10. Switch **W** (write automation) on for a channel. Expect: a red **W** on its strip; moves are refused.
    Switch W off. Switch **R** on: a green **R**; the first move shows a warning (playback may move the fader back).

## D. Restore

11. Move four or five channels from Mixology. Expect: **↺ Restore (5)** on the top bar and a small **↺** on each
    changed strip.
12. Click one strip's **↺**. Expect: only that channel goes back (volume, pan, mute, solo).
13. Click **↺ Restore** on the top bar. Expect: all the others go back.
14. Move a channel, use the bank arrows so it is out of view, press **Restore**.
    - Expect: "1 more is outside the bank". Bank back, Restore again: it goes back.
15. Settings > Cubase Bridge > **Keep the mix as it is now**. Expect: the Restore count goes away.

## E. Mixology Link

16. Insert **Mixology Link** on 3 or 4 tracks: an audio track, an instrument track, a group, an FX channel.
    Put it **after** the track's other plug-ins (the last insert slot that is used). Open its window.
    - Expect: it shows the **track's name and colour** (Cubase tells the plug-in), a green dot
      "Paired: live on Mixology's Tracks page", and its meters move when you play.
    - **Report** if the name says "Track name not reported" (that would mean Cubase Artist doesn't pass it on).
17. In Mixology's Tracks page: those strips have a green **chain icon** and their meters move during playback.
    Pull one of them down in Mixology: its meter drops too (the meter shows the level after the fader).
18. Turn the Link's **Trim** knob: the track gets louder / quieter (meters in the Link and in Mixology).
19. Rename a track that has a Link. Expect: it stays paired (green chain) under its new name.
20. Give two tracks the same name, both with a Link. Expect: an amber chain icon (Mixology can't tell them apart),
    and the Link window says "Two tracks have this name". Give them different colours: they pair again.
21. Save, close and reopen the project. Expect: the Links pair again; Trim values are kept.
22. Optional: Mixology Link on 10 or more tracks, play the song. Expect: Cubase's performance meter barely moves.

## What to send back

- Which steps passed / failed (numbers are enough).
- Step 16: did the Link show the real track name and colour?
- From Milestone 1, still open: **did Mixology's fader moves show up in Edit > History** (undo list)? And with
  **R** on a channel that has volume automation, what happened when you pressed play after a Set dB?
- The copied log.
