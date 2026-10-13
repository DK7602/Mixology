# Milestone 1 test: Mixology moves one real Cubase fader

**Passed** in Cubase Artist 14.0.41 (2026-10-13). Kept for reference: from Milestone 2 there is no "test channel"
any more (every strip moves, the padlock now protects a channel). Current test: [M2_TEST.md](M2_TEST.md).

Goal: prove that Mixology can move ONE MixConsole fader you choose, exactly, see you move it, and never
touch the wrong channel. About 20 minutes. Work in a **copy** of a project (or a new project with a few
audio tracks), not your only copy of a song.

Before you start: do the one-time setup in the manual (Help ? button, "The Cubase bridge").

When something doesn't match: Settings (gear) > Cubase Bridge > **Copy log**, paste it into the chat with
Claude, and say which step number failed. A short screen recording helps too.

## A. Connect

1. Open the test project. Insert **Mixology** on the Stereo Out and open its window.
   - Expect: Settings > Cubase Bridge shows a green dot, "Connected to Cubase (script v3)".
     (v1 or v2 means an old script is still installed: copy the new `Mixology` folder again and restart Cubase.)
   - Press **Refresh** and look at the log: "script v3: N named channel(s)" should match your tracks.
   - Expect: the Tracks page shows your first 12 channels with their real names, fader values and pans.
2. Compare 3 channels with the MixConsole.
   - Expect: the dB under each strip matches the MixConsole fader value; pan matches (C, L20, R35...).

## B. Mixology moves the fader

3. On the Tracks page click the padlock on a test channel (e.g. a mono audio track). It turns green.
4. Settings > Cubase Bridge: type `-6`, press **Set dB**.
   - Expect: the Cubase fader shows exactly **-6.00**. The result line says "Set '...' to -6.00 dB".
5. Repeat with **0 dB**, **-12**, **+6**, **-∞**, then **-1 dB** / **+1 dB** a few times.
   - Expect: each lands exactly (within 0.05 dB) on Cubase's display. -∞ shows "-oo".
6. Drag the test channel's fader on the Tracks page.
   - Expect: the Cubase fader follows smoothly; no other fader moves.
7. Pan L / C / R, Mute, Solo buttons.
   - Expect: Cubase follows. **Report** whether Mute and Solo switch on AND off correctly (this part of
     Cubase's API isn't documented, so this tells us if the binding type is right).

## C. Cubase moves the fader

8. Move the test channel's fader in the MixConsole with the mouse.
   - Expect: Mixology's strip follows within about 0.1 s, and the log says "Fader moved in Cubase".
9. Move OTHER channels' faders in Cubase.
   - Expect: their strips follow too (read-only).

## D. Never the wrong channel

10. Rename the test channel in Cubase.
    - Expect: Mixology pauses ("renamed"). Set dB does nothing. Click the padlock again to continue.
11. Drag another track above the test track in the Project window (so the order changes).
    - Expect: pause; nothing moves. Re-lock the test channel at its new place and Set dB works again.
12. Bank arrows on the Tracks page (◀ / ▶ shift one, ◀◀ / ▶▶ twelve).
    - Expect: the strips show other channels. **Report** if the bank does NOT move (the bank buttons use
      an undocumented part of the API). Expect a pause if the locked channel left its slot.
13. Hide a channel in the MixConsole (Visibility), and show it again.
    - Expect: nothing moves. (The script lists hidden channels too, like Steinberg's own scripts; report
      whether the strips change when you hide one.)
14. (Skipped for now: script v3 doesn't read Cubase's channel IDs, see the note in RESUME-HERE.)

## E. Automation and playback

15. On the test channel switch **W** (write automation) on.
    - Expect: Set dB is refused: "is writing automation (W is on)". Switch W off again.
16. Switch **R** (read) on for a channel that has volume automation, press play, try Set dB.
    - Report what happens (Cubase's automation will probably move the fader back; that's expected and is
      why Milestone 2 warns about it).
17. Play the song while doing a few Set dB moves.
    - Expect: no clicks or dropouts, no automation written.
18. Edit > History (or MixConsole's History): **report** whether Mixology's moves appear in the undo list.

## F. Restore and reconnect

19. Press **Restore original**.
    - Expect: volume, pan, mute and solo go back to how they were before step 4.
20. Close and reopen the Mixology window.
    - Expect: it reconnects within a couple of seconds; the lock is still there.
21. In the MIDI Remote Manager use **Reload Scripts** (or disable / enable the Mixology Bridge surface).
    - Expect: Mixology reconnects; the lock resumes if the same channel is still in the same slot.
22. Mono and stereo tracks, a group channel, an FX channel, an instrument track: repeat step 4 on each.
    - Expect: all work; Stereo Out never appears in the bank.

## What to send back

- Which steps passed / failed (numbers are enough).
- Answers to steps 7, 12, 14, 16, 18.
- The copied log.
