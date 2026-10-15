#pragma once

#include "mixology/MixAnalysis.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/** Setup check: problems in how the Cubase project is set up that no mixing move can fix, found while the song plays
    and told in plain words with the fix (the Mix Assistant, the Tracks page, the Stereo page).

    - A mono group or output on the way: the tracks play in stereo (their Mixology Links hear width, or they are
      panned), yet left and right are the same at the Stereo Out. Named with the channel it most likely is.
    - Vocals with Voxology but without Mixology Link (Mixology can't hear them on their own).
    - Mixology itself on a mono channel.

    Framework-free and unit-tested; the hub feeds it five times a second. */
namespace mixology::setup
{
    /** One track as its Mixology Link hears it right now. */
    struct TrackNow
    {
        std::string name;
        bool audible = true;         // not muted / soloed out
        float loudness = kSilentDb;  // its Link's frame, before the fader (LUFS-like)
        float width = 0.0f;          // its Link's frame: side share
        double faderDb = 0.0;        // its Cubase fader (when known)
        bool panKnown = false;
        double pan = 0.0;            // -100 = L100 .. +100 = R100 (Cubase's pan for a mono track)
        bool panWorks = true;        // false: Cubase doesn't pan this channel ("No pan")
    };

    /** The mix as Mixology on the Stereo Out hears it right now. */
    struct MixNow
    {
        bool playing = false;
        float loudness = kSilentDb;
        float width = 0.0f;          // side share
        float correlation = 1.0f;
    };

    /** Watches whether stereo tracks arrive in mono at the Stereo Out (a mono group or output on the way). */
    class MonoWatch
    {
    public:
        static constexpr double kExpectedWidth = 0.03;    // the tracks' own width that should show in the mix
        static constexpr double kMonoWidth = 0.002;       // ... and what counts as none at all
        static constexpr int kWindow = 150;               // moments kept (30 s at five a second)
        static constexpr int kEnough = 30;                // moments with stereo tracks before it judges (6 s)

        void add (const MixNow&, const std::vector<TrackNow>&);
        void reset();

        /** The tracks are stereo but the mix arrives in mono (nearly every recent moment). */
        bool monoFold() const;
        /** The tracks that should have made the mix stereo, the widest first (up to 3). */
        const std::vector<std::string>& stereoTracks() const { return evidenceNames; }
        /** What the tracks should have given the mix (side share) and what it had, on the last judged moment. */
        double expectedWidth() const { return lastExpected; }

    private:
        std::deque<bool> moments;   // per moment with stereo tracks: true = the mix was mono anyway
        std::vector<std::string> evidenceNames;
        double lastExpected = 0.0;
    };

    /** The side share a mono sound gets from a pan position (-100 .. +100), constant-power pan law. */
    double panWidth (double pan);

    /** What the hub knows when it asks for the issues. */
    struct Context
    {
        int mixChannels = 2;                         // Mixology's own channels (1 = it sits on a mono channel)
        bool monoFold = false;
        std::vector<std::string> stereoTracks;       // the evidence (MonoWatch)
        std::vector<std::string> busCandidates;      // channels that are most likely the mono group, best first
        std::vector<std::string> vocalsWithoutLink;  // Voxology on them, no Mixology Link
    };

    /** The issues, worst first, as Mix Assistant suggestions (id "setup-..."). */
    std::vector<mix::Suggestion> issues (const Context&);
}
