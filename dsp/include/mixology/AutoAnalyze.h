#pragma once

#include "mixology/MixAnalysis.h"

#include <cstdint>
#include <map>
#include <set>

/** Auto Analyze: after Analyze Mix, Mixology keeps listening whenever the song plays and refreshes the advice
    every 15 seconds of new playback, only when the advice changed.

    It listens to one version of the mix at a time: when a fader, a track's processing or the tracks that play
    change, it starts again from that moment (so the advice is about the mix as it is now, never half old,
    half new). It never changes the mix. Framework-free and unit-tested (tests/test_analysis.cpp). */
namespace mixology::mix
{
    class AutoAnalyze
    {
    public:
        static constexpr double kEnoughSeconds = 20.0;    // the first check after a change
        static constexpr double kRefreshSeconds = 15.0;   // then every 15 s of playback
        static constexpr double kFaderTolerance = 0.25;   // dB: a fader that moved less is the same mix

        enum class Step
        {
            added,      // one more moment of the same mix
            restarted,  // the mix changed (or this is the first moment): listening again from here
            check,      // added, and it's time to check the advice (evaluate, then adviceChanged)
        };

        /** Begin (or begin again) with the style the advice is for. */
        void start (const Style&);
        /** Stop; the next add() starts a new window. */
        void reset() { window.clear(); fresh = true; }

        /** A moment of the song playing. `mixKey` changes whenever Mixology or you changed the processing
            (the hub hashes every Link's settings and its own history); faders and tracks are read from the moment. */
        Step add (Moment, std::uint64_t mixKey);

        double secondsHeard() const { return window.secondsHeard(); }
        const Analysis& analysis() const { return window; }
        Analysis& analysis() { return window; }

    private:
        Style style;
        Analysis window;
        bool fresh = true;
        std::uint64_t key = 0;
        std::set<std::uint32_t> tracks;
        std::map<std::uint32_t, double> faders;
        double checkedAt = 0.0;   // secondsHeard() at the last check
    };

    /** Whether a new report says something different enough to replace the one on screen: a point appeared,
        went away or changed colour, the vocal moved by 1 dB or more against the beat, the mix by 1 LU or more,
        or other tracks played. Small number changes alone keep the cards still. */
    bool adviceChanged (const Report& shown, const Report& fresh);
}
