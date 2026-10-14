# Milestone 4 test: Auto Mix and the smart tools

## Round 3 (Beat is a group): do this first

- Q1. Tracks page: press **Restore** (gets your original faders and pans back).
- Q2. Click **Beat**'s icon (top of its strip) and choose **Music group**. The icon turns into a gold group symbol.
- Q3. **Analyze Mix** (any style). Open **BALANCE**, press **Too loud** or **Right** for the vocal, Apply once.
  The music now moves on the Beat fader only. Open Balance again: it should find nothing more to do.
  **Report**: how does it sound? Did the guitars stay where you put them?

## Round 2 (after your first test): what changed, what to try

Your first test found: Balance raised the vocal you already find too loud (Mixology aimed for Rock's usual
level), moves were cut at 6 dB so the balance came out wrong, Auto Mix made everything quieter, Undo needed the
tool to be reopened, and muted tracks still showed. Now:

- R1. Analyze Mix (same song, same part). Open **BALANCE**. At the top: "Your vocal right now sounds:" press
  **Too loud**. The list comes back: the vocal goes down about 2.5 dB against the band. Apply and listen.
  **Report**: better? Then try **Right** (nothing should move for the vocal).
- R2. The bottom line **Also make room for Masterology** is unticked. Leave it unticked while you balance.
  Tick it once to see it, Apply, listen (quieter, same balance), then Undo.
- R3. After **Undo**, the list comes back by itself, ready to Apply again (no closing and reopening).
- R4. Mute a track and press play: its colour leaves the analyzer. Analyze Mix leaves it out.
- R5. **STEREO ARCHITECT** on that song: if Cubase shows your Electric tracks at L100 but you hear them in both
  ears, it now says so and leaves the pans alone. **Report**: do you hear those guitars only on the left, or on
  both sides?
- R6. AUTO MIX: same as before, with your vocal setting. **Report**: better, worse, or different?

The original steps follow.

Goal: each tool on the Overview (Auto Mix, Balance, Clean Up, Vocal Pocket, Stereo Architect, Reference Match)
shows sensible changes, applies them to the real Cubase faders, pans and Mixology Links, and Undo puts
everything back. About 30 minutes. **Use a copy of a project** (File > Save As... first).

When something doesn't match: Settings (gear) > Cubase Bridge > **Copy log**, paste it into the chat with
Claude, and say which step number failed. Screenshots of the tool's list help a lot.

## A. Install

1. Close Cubase. Copy **both** `Mixology.vst3` and `Mixology Link.vst3` into
   `C:\Program Files\Common Files\VST3\` (replace both: they must come from the same download).
   The Cubase script did not change.
2. Open your project copy. Mixology on the Stereo Out, Mixology Link on your tracks (as in the M3 test).
   Open one Link window: at the bottom it now shows **ASSIST EQ: Off**. That's where Clean Up and Vocal Pocket work.

## B. Analyze first

3. Press **ANALYZE MIX** and play the busiest part for 20 - 60 s (as in M3).
4. While it plays: **Report** whether the Mix Assistant's points still make sense. New since M3:
   - "The vocal is buried" now counts a beat without a Link (e.g. a printed 2-track beat).
   - "The mix leans to the left / right" names the tracks pulling it there.

## C. Balance (fader levels)

5. Press **BALANCE**. A list opens: every fader it wants to move, **now → new**, and why. Nothing has moved yet.
   - **Report**: do the reasons make sense? Anything it should leave alone?
6. Untick one line. If you see "(limited)" and an **Allow moves over 6 dB** button, leave it as it is for now.
7. Press **Apply**. Watch the Tracks strips / Cubase MixConsole: the faders move. If some channels are outside
   the 12 in view, the bank steps through and comes back by itself.
   - **Report** if the bank doesn't come back to where it was, or a fader landed somewhere else than "new".
8. Listen. Then press **Undo Balance**: every fader goes back exactly.
   - **Report** if any fader isn't back where it was (Cubase's own display: same number as before).
9. Protect one channel (padlock on the Tracks page), open Balance again: that channel is under
   **Left as it is** ("protected"), never in the list.

## D. Stereo Architect (pans)

10. Press **STEREO ARCHITECT**. Pairs like "Electric Rhythm" + "Electric Rhythm (D)" are spread: one stays,
    its double goes to the other side. Kick, bass and the lead vocal go to the middle if they aren't.
    - **Report**: are the pairs right? (Your session from the M3 video had all four Electric tracks on L100.)
11. Apply, listen on headphones, then Undo (or keep it).

## E. Clean Up and Vocal Pocket (Assist EQ)

12. Press **CLEAN UP**: low cuts for tracks with rumble (and the vocal), mud dips if the mix is muddy.
    Apply. Open a changed track's **Mixology Link** window: ASSIST EQ shows the low cut / dip and its curve.
    Also try Cubase's own view of the Link's parameters: "Low Cut", "Mud Dip"... changed there too.
13. Press **VOCAL POCKET**: dips at 1.6 - 4 kHz in the instruments that cover the vocal. Apply, and listen:
    are the words clearer? **Report** yes / no / a little.
14. Press Undo in each (or **Clear** in a Link window): the Assist EQ goes back to Off.
15. Save, close and reopen the project: Assist EQ settings you kept are still there (they're Link parameters).

## F. Auto Mix

16. Press **AUTO MIX**: all of the above in one list. Untick anything you don't want. Apply.
    - **Report**: better, worse, or different? What would you change first?
17. Undo Auto Mix. Everything (faders, pans, Assist EQ) is back.

## G. Reference Match

18. Import a finished song you like onto a new track **under your song** (same bars). Insert **Mixology Link**
    on it and **mute** the track. Press play: the muted track's Link window should still show level.
    - **Report** whether the Link shows level while the track is muted (important!).
19. Analyze Mix. Press **REFERENCE MATCH**, pick that track. You get both curves, five regions in words,
    loudness and width.
20. Press **Hear the reference**: it plays alone (solo), its Trim set so it's as loud as your mix.
    **Back to your mix** mutes it again. **Report** whether the switch works and the volumes feel matched.

## What to send back

- Steps 5, 7, 8, 10, 13, 16, 18 and 20 (what you saw / heard).
- Screenshots of one tool's list (Auto Mix is best) and of Reference Match.
- The copied log.
