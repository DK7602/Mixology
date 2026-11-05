#pragma once

#include "mixology/AssistEq.h"
#include "mixology/MixAnalysis.h"
#include "mixology/ProcParams.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/** Milestone 4: turns Analyze Mix's report into concrete moves that you look over before anything changes.

    - Balance: fader levels. Instruments against each other (role targets per genre; only clear outliers
      move), the lead vocal against the rest of the mix (the style's target, or the level you set), ad-libs
      under the vocal, all at the same overall loudness. Fader moves since Analyze Mix are allowed for, so
      applying a plan and opening it again finds nothing more to do. Tracks of one kind move
      together, so their balance among themselves stays yours. Room for Masterology (mix peaks around
      -6 dBTP) is offered separately: one gain on the whole mix, the Full Mix's Final Output (Mixology on the
      master channel, before Masterology), so every track, FX return and group goes down together. With a music
      group (a group channel the instruments play through, e.g. Voxology's "Beat"), the music moves on that one
      fader.
    - Stereo Architect: pans. Low end and the lead vocal in the middle, doubles spread left / right (a name pair
      only when Analyze Mix heard both play at the same time, moving together: Report::pairs), a lopsided mix
      evened out.
    - Clean Up: Assist EQ low cuts on tracks with rumble (and on vocals), mud dips where the mix is muddy.
    - Vocal Pocket: Assist EQ dips where instruments cover the vocal's words.
    - Polish (Milestone 5, round 3): processing in each track's Mixology Link, by the per-track rules below, and the
      Full Mix's tone against the style. A track whose Space already plays (up to three effects: Space layers) is
      left alone and its effects are named; two reverb types on one track (Reverb, Hall, Room, Special FX) get a
      tip: stacking spaces can wash it out.
    - Auto Mix: all of them together.

    The per-track rules (one set, used by Polish / Auto Mix for every track and by each processing page's AUTO
    button for the one track picked there: makeTrackPlan), per page, for every role:
    - EQ (the Link's 6-band EQ): a low cut by role (rumble; kick, bass and a whole beat only below their notes),
      mud (250 - 500 Hz heavy for the role and style), a boxy kick or snare, a harsh peak, a dull or bright top, a
      dull vocal's presence and air, a dull snare's crack, a boomy low end (when Tone Balance can't).
    - Dynamics: a compressor where the level swings (vocals, bass, keys, guitars, synths; at most gentle glue on a
      beat), Multiband for a bass whose low end jumps, the Transient shaper on a squashed or spiky kick / snare, a
      gate or expander only where Analyze Mix heard a steady noise floor between phrases. Never the Limiter.
    - Space: the lead vocal's reverb (on an FX channel with a Link, ducking under it; else a little on the vocal),
      ad-libs' delay, a small room / plate / hall on snare, keys, guitars, synths and percussion by the style
      (trap dry, hip hop nearly dry, pop / R&B / worship wetter, rock rooms); kick, bass, hats and a beat stay dry.
    - Tone: Tone Balance only (Warmth, Presence, Air), a broad tilt per role and style, at most about 3 dB.
    - Stereo: the low end of a stereo kick / bass / beat in the middle; a narrow stereo keys / synth / guitar /
      backing vocal a little wider (up to 140 %), a very wide one that falls apart in mono a little narrower. Never
      pans (Stereo Architect does the whole mix's), never widens a lead vocal, nothing on mono tracks.
    They correct clear problems only (a track that fits gets nothing), with small moves (EQ 1.5 - 4 dB, never more
    than 6), and never fight Voxology (a vocal where it already does EQ, compression, reverb or delay keeps those).
    Settings are worked out from what the processing was when Analyze Mix listened, and every rule counts what the
    track's other modules changed since then in the same range (Tone Balance's air against the EQ's, the Assist EQ's
    mud and pocket dips against the EQ's), so applying a plan and opening it again, AUTO then Auto Mix, or Auto Mix
    then AUTO, finds nothing more to do.

    Nothing here touches Cubase: the plug-in applies the plan you approve (faders and pans through the bridge,
    the Assist EQ through each track's Mixology Link). Framework-free and unit-tested. */
namespace mixology::mix
{
    /** One Cubase channel as Mixology sees it right now (the bridge's fader scan and the Links). */
    struct ChannelNow
    {
        std::uint32_t hash = 0;
        std::string name;
        bool faderKnown = false;
        double faderDb = 0.0;
        bool hasPan = false;
        double pan = 0.0;          // -100 = L100, 0 = centre, +100 = R100
        bool panUnknown = false;   // a stereo track on Cubase's Combined Panner: Cubase only reports its left edge
        int channels = 0;          // its Mixology Link's channels (1 mono, 2 stereo, 0 unknown): only mono tracks get panned
        bool sharedName = false;           // another track has the same name: Mixology can't tell their faders and
                                           // Links apart, so plans leave it alone
        bool hasFaderAtAnalysis = false;   // the fader when Analyze Mix listened: moves since then are allowed for
        double faderAtAnalysis = 0.0;      // (so applying a plan and opening it again doesn't repeat its moves)
        bool musicBus = false;     // the music group you marked: the instruments play through it
        bool locked = false;       // protected (padlock)
        bool hasLink = false;      // a Mixology Link sits on it (its Assist EQ can be set)
        AssistSettings assist;     // that Link's Assist EQ now
        bool assistAtAnalysisKnown = false;   // ... and when Analyze Mix listened (the advice heard the mix with it)
        AssistSettings assistAtAnalysis;
        bool hasProc = false;      // ... and its processing (M5): now, and when Analyze Mix listened
        proc::Values proc {};
        bool procAtAnalysisKnown = false;
        proc::Values procAtAnalysis {};
        std::vector<std::uint32_t> duck;   // the tracks its Space ducks under now

        /** Voxology on this channel (M6, the suite link): what it already does to the vocal, so Polish doesn't
            add a second compressor, reverb or delay. */
        struct Voxology
        {
            bool on = false;           // a Voxology (Vocal mode) sits on it, processing
            bool compressor = false, reverb = false, delay = false, eq = false;
            double compressionDb = 0.0;
            // What its last Auto-Edit heard in the recording itself: fixed at the source, before any mixing.
            bool recordedHot = false, recordedClipped = false;
            double recordedPeakDb = -100.0;
            int clippedSpots = 0;
        };
        Voxology voxology;
    };

    /** Mixology's own Full Mix processing (for the mix's tone against the style, and room for Masterology). */
    struct MixNow
    {
        bool known = false;
        proc::Values now {}, atAnalysis {};
        bool atAnalysisKnown = false;
        bool onMaster = true;              // that Mixology sits on the master channel (or the host doesn't say where)
        bool peakKnown = false;            // the mix's true peak since play where Final Output sits (after the Full
        double peakBeforeFinalDb = -120.0; // Mix processing, Final Output's own gain taken back out), dBTP
    };

    enum class Tool { autoMix, balance, stereo, cleanUp, pocket, polish };

    /** The processing pages, as AUTO works on them (the interface's page ids: "eq", "dynamics", "space", "tone",
        "stereo"). Never Master: that is Masterology's job. */
    enum class Page { eq, dynamics, space, tone, stereo };
    const char* pageKey (Page);
    const char* pageName (Page);   // "EQ", "Dynamics", "Space", "Tone", "Stereo"
    std::optional<Page> pageFromKey (const std::string&);
    const char* toolKey (Tool);
    const char* toolName (Tool);
    std::optional<Tool> toolFromKey (const std::string&);

    struct Change
    {
        std::uint32_t hash = 0;
        std::string name;
        Role role = Role::other;
        std::string kind;          // "level" (dB), "pan" (-100..+100), "lowcut" (Hz, 0 = off), "mud" / "pocket" (dB),
                                   // processing: "eq", "comp", "width", "space", "tone"
        double from = 0.0, to = 0.0;
        double wanted = 0.0;       // levels: the whole move's target, before the 6 dB limit
        bool big = false;          // levels: the whole move is more than 6 dB (Apply stops at 6 unless you allow more)
        double hz = 0.0;           // mud / pocket: where the dip sits
        double fromHz = 0.0;       // ... and where the one there now sits (0: none)
        std::string why;

        // Processing (Milestone 5): controls of the track's Mixology Link (or of Mixology's Full Mix) set together.
        std::vector<std::pair<int, float>> proc;   // proc::Id, value
        bool fullMix = false;
        bool setDuck = false;                      // space: also set the tracks the effect ducks under
        std::vector<std::uint32_t> duck;
        std::string fromText, toText;              // e.g. "Flat" -> "+2.0 dB at 3.5 kHz"
        std::string page;                          // the processing page its controls are on ("eq", "dynamics", "space",
                                                   // "tone", "stereo"); "" for faders, pans, the Assist EQ, the Full Mix

        // AUTO's one line: what it did and what it heard, in a few words ("cut 3 dB of mud at 300 Hz" / "muddy")
        std::string did;
        std::string heard;                         // a word for "'Keys' sounded ..." ("muddy", "dull")
        std::string heardPhrase;                   // or a whole phrase ("swung 9 dB in level")
        bool byStyle = false;                      // the move follows the style (the line names it)
    };

    struct Plan
    {
        Tool tool = Tool::autoMix;
        bool valid = false;        // false: Analyze Mix hasn't run (summary says what to do)
        std::string title;
        std::string summary;
        std::vector<Change> changes;
        std::vector<std::string> notes;   // what it left alone, and why
        std::vector<std::string> first;   // fix these before applying (a vocal recorded too hot): shown above the changes

        /** Room for Masterology, offered on its own (you tick it): one gain on the whole mix, the Full Mix's Final
            Output, headroomDb (< 0) quieter. Every track, FX return and group goes down together (also those that
            didn't play while Analyze Mix listened), the balance stays exactly, and it is one change to undo.
            0 = not needed, or not possible (a note says why). */
        double headroomDb = 0.0;
        std::string headroomWhy;
        std::optional<Change> headroomChange;   // the Full Mix change it makes (Final Touch on, its Output)

        std::vector<std::string> groupHints;   // channels that look like a group the tracks play through (ask: music group?)

        /** AUTO (makeTrackPlan): one plain line, what it changed and why ("AUTO: cut 3 dB of mud at 300 Hz: 'Keys'
            sounded muddy for Pop - Modern"), or why nothing ("'Kick' already fits: nothing to change"). */
        std::string line;
    };

    constexpr double kMaxMoveDb = 6.0;
    constexpr double kMaxFaderDb = 6.0;   // top of a Cubase channel fader

    Plan makePlan (Tool, const Report&, const std::vector<ChannelNow>&, const MixNow& mix = {});

    /** AUTO on a processing page: the same rules Polish and Auto Mix use, for that page's controls on one track (its
        name hash; 0 = the Full Mix: refused, the line says to pick a track). Only that track's processing changes, and
        for the lead vocal's Space, the FX channel its reverb goes on (the line says so). Nothing to change: no
        changes and the line says why. plan.valid false: AUTO can't work here (no Analyze Mix, Full Mix, the track
        didn't play); the line says why. */
    Plan makeTrackPlan (Page, std::uint32_t hash, const Report&, const std::vector<ChannelNow>&, const MixNow& mix = {});
    /** AUTO needs to hear this track first (Analyze Mix didn't hear it play long enough: heardEnough). Not for an FX
        channel's Space: that works from the lead vocal going into it. */
    bool autoListensFirst (Page, std::uint32_t hash, const Report&, const std::vector<ChannelNow>&);
}
