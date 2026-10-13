# Milestone 3 test: Analyze Mix and the Mix Assistant

Goal: Mixology Link listens to each track, Analyze Mix hears the whole song, and the Mix Assistant's advice
makes sense for your mix. About 20 minutes. Use a real song of yours (a copy of the project).

When something doesn't match: Settings (gear) > Cubase Bridge > **Copy log**, paste it into the chat with
Claude, and say which step number failed. Screenshots of the Mix Assistant help a lot.

## A. Install

1. Close Cubase. Copy **both** `Mixology.vst3` and `Mixology Link.vst3` into
   `C:\Program Files\Common Files\VST3\` (replace both). They must come from the same download: the new Link
   and the new Mixology talk a newer language. The Cubase script did not change.

## B. Links on your tracks

2. Open a song with 6 to 15 tracks: kick, 808 / bass, drums, a lead vocal, instruments.
   Insert **Mixology Link** on each (after its other plug-ins). Mixology on the Stereo Out.
3. Press play. On the **Overview** page the analyzer now shows coloured lines over the rainbow:
   Kick (red), Bass (orange), Drums (yellow), Vocals (green), Instruments (blue), Other (purple).
   - **Report** if a line looks wrong for what's playing (e.g. the vocal line shows while only the beat plays).
4. On the **Tracks** page, look at the icons on the strips. Click one: a menu lets you pick its role, or leave it
   out of Analyze Mix. Pick a role for a track named "Audio 01" or similar.

## C. Analyze Mix

5. Press **ANALYZE MIX** (Overview). It says "Reading faders" for a second or two (the Tracks page pages through
   your channels and comes back), then "Press play in Cubase".
   - **Report** if the Tracks page doesn't come back to the same channels afterwards.
6. Play the busiest part of the song. Watch the bar: after 20 s press **Finish now** (or let it run to 60 s).
7. The Mix Assistant lists what it found. Click each point and read it.
   - **Report** for each point: does it match what you hear? (Yes / No / Not sure is enough.)
8. Press **Your tracks**: check each track's role. Change one that is wrong: the advice updates.
   Click **Included** on one track to leave it out: its mentions disappear from the advice.
9. Press **Live view** and back.
10. Change the mix style (header) to something quite different (e.g. Rock - Classic), press Analyze Mix again,
    play the same part. **Report** whether the advice changed in a way that makes sense.
11. Save, close and reopen the project: your role changes and left-out tracks are still there (strip icons).

## D. Try a fix

12. Pick one "Worth fixing" point and do what it says (e.g. lower a track, or put a low cut on a vocal).
    Analyze Mix again. **Report** whether that point went away or changed.

## What to send back

- Steps 3, 5, 7, 10 and 12 (what you saw).
- Screenshots of the Mix Assistant list (and one opened point).
- The copied log.
