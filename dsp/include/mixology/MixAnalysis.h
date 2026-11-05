#pragma once

#include "mixology/TrackAnalyzer.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/** Analyze Mix: listens to every track (through its Mixology Link) and the whole mix while the song plays,
    then explains in plain language what stands out, each point with "Do I need to fix it?".

    Framework-free and unit-tested. The plug-in feeds it one Moment about five times a second while the
    transport runs; evaluate() can run again later (e.g. after you correct a track's role) without listening
    again, because the moments are kept. */
namespace mixology::mix
{
    // ---------------------------------------------------------------------------------------------
    // Track roles (same keys as the interface's ROLES table)

    // New roles go at the end: projects save roles by key ("beat"), but the order is the analyzer's and the tables'.
    // beat: a whole instrumental on one track (a 2-track beat, very common in hip hop, R&B and pop demos): the music
    // against the vocal, never an instrument among the others.
    enum class Role { kick, snare, hats, perc, bass, keys, synth, guitar, vox, adlibs, fx, other, beat };
    constexpr int kRoleCount = 13;

    const char* roleKey (Role);
    std::optional<Role> roleFromKey (const std::string&);

    /** From the channel name ("808", "Lead Vox", "Hi Hat", "Keys - Verse"...); nothing when the name doesn't say. */
    std::optional<Role> roleFromName (const std::string& name);
    /** A name as the words roleFromName reads: "LeadVox2 (Hook)" -> "lead vox 2 hook". */
    std::string nameWords (const std::string& name);

    /** "Gtr L" / "Gtr R", "Vocals" / "Vocals (D)", "Keys 1" / "Keys 2": the shared name, and which side the
        name asks for (-1 left, +1 right, 0 none) and its order among the takes (0 = the original). A name only says
        who MIGHT pair: Report::pairs says whether they really play together. */
    struct PairName
    {
        std::string base;
        int side = 0;
        int order = 0;
        bool explicitPair = false;   // L / R / double / (D): clearly one of a pair, whatever its role
    };
    PairName pairName (const std::string& name);

    /** The six colours of the Overview analyzer: Kick, Bass, Drums, Vocals, Instruments, Other. */
    enum class Group { kick, bass, drums, vocals, instruments, other };
    constexpr int kGroupCount = 6;
    Group groupOf (Role);
    const char* groupKey (Group);

    // ---------------------------------------------------------------------------------------------
    // Input

    struct TrackSample
    {
        std::uint32_t hash = 0;    // name hash (same as the Cubase bridge's); 0 = the host gave no name
        std::string name;
        double faderDb = 0.0;      // the channel's Cubase fader (Link measures before it)
        bool faderKnown = false;
        AnalysisFrame frame;       // what its Mixology Link heard (before the fader)
    };

    struct Moment
    {
        AnalysisFrame mix;         // what Mixology on the Stereo Out heard
        std::vector<TrackSample> tracks;
    };

    /** The mix style's targets (from the style picked in the header). */
    struct Style
    {
        std::string key = "hiphop.modern";   // "genre.style"
        std::string name = "Hip Hop - Modern Clean";
        double bass = 4.0, scoop = 1.5, air = 2.0;   // tonal target (dB), same as the Tone page
        double vocalOffset = 0.0;    // your ears: added to the style's vocal level target (dB)
        bool vocalFromYou = false;   // ... and you set it ("sounds right / too loud / too quiet")
    };

    struct TrackSettings
    {
        std::optional<Role> role;  // your correction
        bool excluded = false;     // left out of Analyze Mix (reference tracks, printed stems...)
        bool reference = false;    // your reference song (Reference Match); always left out of the mix advice
        bool musicBus = false;     // a group channel the music plays through (not the vocals), e.g. Voxology's "Beat"
        bool noPan = false;        // Cubase doesn't pan it (mono track into a mono group): learned by the bridge, kept here
        std::optional<double> vocalOffset;   // your ears for this vocal track: dB on top of the style's level (else the whole vocal's)
        std::optional<Role> fromVoxology;   // Voxology sits on it (a vocal, or ad-libs): live, never saved
    };

    /** Tracks that play through the music group (when you marked one): everything but vocals, ad-libs and effects. */
    bool viaMusicBus (Role);

    // ---------------------------------------------------------------------------------------------
    // Output

    struct TrackReport
    {
        std::uint32_t hash = 0;
        std::string name;
        Role role = Role::other;
        std::string roleFrom;      // "you", "name" or "sound"
        bool excluded = false;
        bool faderKnown = false;
        double activity = 0.0;     // share of the listening time it was playing
        double loudness = -120.0;  // after the fader, while playing (LUFS-like)
        double peakBeforeFader = -120.0;
        double width = 0.0;        // 0 mono .. 1 all sides
        double lowWidth = 0.0;     // the same below 160 Hz
        double crest = 0.0;        // peak minus RMS (dB): punchy / transient sounds score high
        double levelRange = 0.0;   // how far its level swings while it plays (dB, loud minus quiet parts, ~1 s averages)
        double loudPeak = -120.0;  // its peaks in the loud parts (dBFS before the fader, 90th percentile)
        double balance = 0.0;      // left minus right (dB) after the fader: + leans left
        double correlation = 1.0;  // left against right while it plays (1 = mono, 0 = unrelated, below 0 = out of phase)
        double lowRange = 0.0;     // how far its low end (up to 160 Hz) swings while it plays (dB, like levelRange)
        double activeSeconds = 0.0; // how long it played while Mixology listened
        double activeRms = -120.0; // its usual level while it plays (dBFS before the fader, median of the moments)
        /** A noise floor between its phrases: moments it didn't play but wasn't silent either (hiss, hum, bleed),
            before the fader. floorMoments 0 = none heard (silent between phrases, or it never stops). */
        int floorMoments = 0;
        double floorRms = -120.0, floorPeak = -120.0;   // median of those moments (dBFS)
        double floorSpread = 0.0;                       // how steady it is (P90 - P10 of those moments, dB)
        std::vector<double> bands; // average spectrum after the fader (dB per 1/3 octave)
        double maskDb = -120.0;    // its 1.6 - 4 kHz level against the lead vocal's while it sings (> -6: covers it)
        bool hasVocalRatio = false; // a lead vocal track: it against the rest of the mix while it sings (dB),
        double vocalRatio = 0.0;
        double vocalTarget = 0.0;   // ... and where it should sit: the style's level plus your setting
        bool vocalOwn = false;      // ... this track's own setting (else the whole vocal's)
        bool rumble = false;       // a lot of energy below 100 Hz it doesn't need
        bool reference = false;    // your reference song
    };

    /** Two tracks whose names make them look like a pair or a double ("Vox Chorus 1" / "Vox Chorus 2", "Gtr" /
        "Gtr (D)"), checked by ear from every moment Analyze Mix kept: did they play at the same time, and do their
        levels move together while they do? A real double (the same part played or sung twice) does both; two parts
        of a chorus, a call and its response, a lead and its ad-libs don't. Only real doubles get spread left / right. */
    struct PairCheck
    {
        std::uint32_t a = 0, b = 0;   // the two tracks (name hashes, as TrackReport::hash)
        std::string nameA, nameB;
        int activeA = 0, activeB = 0; // moments each one played
        int both = 0, either = 0;     // moments both / either of them played
        double overlap = 0.0;         // both / either: 1 = always together, 0 = never at the same time
        double alike = 0.0;           // how alike their levels move while both play (correlation of the moments'
                                      // RMS, the best of a moment either way: -1 .. 1; 1 for two steady levels)
        enum class Verdict
        {
            notHeard,   // one of them (nearly) didn't play while Analyze Mix listened, or not together long enough
            apart,      // they play at different times (different parts, a call and its response, ad-libs)
            unlike,     // at the same time, but their levels don't move together (different lines)
            together    // a double: at the same time, moving together
        };
        Verdict verdict = Verdict::notHeard;
    };
    /** What it takes to call a name pair a double. Tuned with test signals (tests/test_analysis.cpp, "Doubles by ear",
        40 songs each, the Links measuring within 30 ms of each other): real doubles overlap 0.83 - 0.99 and move alike
        0.25 - 0.79 (sloppy ones the low end); two different lines sung at the same time overlap 0.93 - 0.98 but move
        alike only -0.05 - 0.19; a call and its response, two halves of a chorus overlap 0, ad-libs under a lead
        0.06 - 0.20. When in doubt a pair keeps its pans (and the plan says why). */
    constexpr int kPairMinMoments = 10;        // each played at least 2 s, and both together 2 s
    constexpr double kPairOverlap = 0.65;
    constexpr double kPairAlike = 0.3;

    /** Your mix against your reference song (Reference Match). */
    struct ReferenceReport
    {
        bool chosen = false;       // a track is marked as the reference
        bool heard = false;        // ... and it played while Mixology listened
        std::uint32_t hash = 0;
        std::string name;
        double loudness = -120.0;  // as it reached the fader (LUFS-like, while playing)
        double width = 0.0;
        std::vector<double> bands; // tilted and lined up like Report::mixBands
        double loudnessDiff = 0.0; // reference minus your mix (LU): Masterology closes this, not the mix
        double widthDiff = 0.0;    // your mix minus the reference (0..1 scale)

        struct Region
        {
            std::string name;      // "Low end (40 - 125 Hz)"
            double diff = 0.0;     // your mix minus the reference (dB)
            std::string text;
        };
        std::vector<Region> regions;
    };

    struct Suggestion
    {
        std::string id;            // stable key ("kick-bass", "mud"...)
        std::string kind;          // "warn" (worth fixing), "info" (have a look), "calm" (fine)
        std::string icon;          // interface icon name
        std::string title;
        std::string text;
        std::string need;          // "Do I need to fix it?" answer
        std::vector<std::string> steps;
        std::vector<std::string> tracks;   // the tracks it is about
    };

    struct Report
    {
        bool valid = false;
        double secondsHeard = 0.0;
        Style style;
        std::vector<TrackReport> tracks;
        std::vector<Suggestion> suggestions;
        std::vector<double> mixBands;     // average mix spectrum (dB), and the style's target, both
        std::vector<double> targetBands;  // normalised to the same overall level
        double mixLoudness = -120.0, mixPeak = -120.0, mixCorrelation = 1.0, mixBalance = 0.0;
        int mixMoments = 0;               // moments the Stereo Out itself was heard (the mix numbers need 5)
        std::vector<std::string> unheard; // channels in Cubase without a Link (not part of this analysis)
        bool hasVocalRatio = false;       // the lead vocal against the rest of the mix while it sings (dB),
        double vocalRatio = 0.0;          // and where it should sit: the style's target plus your offset
        double vocalTarget = 0.0;
        double styleVocalTarget = 0.0;    // the style's own target (without your offset)
        double unheardLoudness = -120.0;  // the channels without a Link together (the mix minus the Links), LUFS-like
        /** Channels without a Link that add no sound of their own (the Links explain the whole mix): most likely
            a group the tracks play through. Mixology asks whether it is the music group. */
        std::vector<std::string> groupHints;

        /** How the tracks reach the Stereo Out, worked out from the sound: what the Stereo Out plays against what
            the Links hear (after their faders), moment by moment, band by band. Group faders, group processing and
            pan law all show up here, whatever the channels are called, so nothing is counted twice. */
        bool routingFromSound = false;
        double vocalPathDb = 0.0;     // the vocals' change on the way (e.g. -3 for a mono vocal panned centre)
        double musicPathDb = 0.0;     // the instruments' change on the way (e.g. a group fader at -6)
        double routingFitDb = 0.0;    // how well that explains the Stereo Out (median error, dB)
        double mixWidth = 0.0;
        bool monoMix = false;              // left and right of the Stereo Out are the same: panning changes nothing
        ReferenceReport reference;
        /** Every name pair among the tracks (same name apart from a number, a side or a double tag), checked by ear. */
        std::vector<PairCheck> pairs;
    };

    /** The check of two tracks' name pair (either order), or nothing when their names don't pair. */
    const PairCheck* pairCheck (const Report&, std::uint32_t a, std::uint32_t b);

    /** AUTO on a processing page works from what Analyze Mix heard of that track: it played at least this long
        (and isn't left out). Otherwise AUTO listens to it while you play a part where it plays (kAutoListenSeconds). */
    constexpr double kAutoHeardSeconds = 4.0;
    constexpr double kAutoListenSeconds = 8.0;
    /** The report's track (by name hash), or nullptr. */
    const TrackReport* trackIn (const Report&, std::uint32_t hash);
    /** Analyze Mix heard this track well enough for AUTO (played kAutoHeardSeconds or more, not left out). */
    bool heardEnough (const Report&, std::uint32_t hash);
    /** A track AUTO listened to on its own (a chorus Analyze Mix didn't hear): `fresh` is the report of those moments.
        Returns `into` with that one track's report (and the name pairs it is in) taken from `fresh`; everything else
        (the rest of the mix, the style, the advice) stays as it was. Its levels get the path to the master channel
        `into` found by ear when `fresh` heard too little to find it. */
    Report mergeTrack (const Report& into, const Report& fresh, std::uint32_t hash);

    /** Rumble on a track: its energy below what a low cut for its kind would remove. That is the bands up to
        lowCutTopBand (about 100 Hz; about 45 Hz for keys and synths, whose low notes are part of the sound). */
    int lowCutTopBand (Role);
    int lowCutTopHz (Role);

    /** Where the style wants the lead vocal against the rest of the mix (dB). */
    double vocalTargetFor (const std::string& styleKey);

    /** The mix's tone against the style's target, by region (dB, + = more than the style): what Polish's Tone
        Balance works from, and what Mixology hands to Masterology. */
    struct ToneAgainstStyle
    {
        bool valid = false;
        double low = 0.0;       // 40 - 125 Hz (bands 3 - 8)
        double lowMid = 0.0;    // 200 - 400 Hz (bands 10 - 14)
        double presence = 0.0;  // 2.5 - 4 kHz (bands 21 - 23)
        double air = 0.0;       // 8 - 16 kHz (bands 26 - 29)
    };
    ToneAgainstStyle toneAgainstStyle (const Report&);

    // ---------------------------------------------------------------------------------------------

    class Analysis
    {
    public:
        static constexpr double kMomentSeconds = 0.2;
        static constexpr int kMaxMoments = 600;      // 2 minutes
        static constexpr double kEnoughSeconds = 20.0;

        void begin (const Style&);
        /** The music group's fader (dB) while Mixology listened: the instruments' levels include it. */
        void setMusicBus (bool present, double faderDb) { hasMusicBus = present; musicBusDb = present ? faderDb : 0.0; }
        /** How you like your vocal (kept with the project): evaluate() aims for the style's target plus this. */
        void setVocalOffset (double offsetDb, bool fromYou) { style.vocalOffset = offsetDb; style.vocalFromYou = fromYou; }
        void clear();
        void add (Moment);
        double secondsHeard() const { return static_cast<double> (moments.size()) * kMomentSeconds; }
        bool full() const { return static_cast<int> (moments.size()) >= kMaxMoments; }
        bool hasMoments() const { return ! moments.empty(); }
        const std::vector<Moment>& heardMoments() const { return moments; }

        Report evaluate (const std::map<std::uint32_t, TrackSettings>& settings,
                         const std::vector<std::string>& unheard = {}) const;

        /** The style's tonal target at a frequency (same curve as the Tone page; relative dB). */
        static double targetDb (const Style&, double hz);

        /** Kept with the project: the style and every moment heard, compact (levels to 0.01 dB), so the advice
            comes back when the project opens and a role change can still re-evaluate it. */
        std::string save() const;
        /** False when the block isn't a saved analysis: nothing changes then. */
        bool load (const std::string&);

    private:
        Style style;
        std::vector<Moment> moments;
        bool hasMusicBus = false;
        double musicBusDb = 0.0;
    };
}
