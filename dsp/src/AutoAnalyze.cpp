#include "mixology/AutoAnalyze.h"

#include <cmath>
#include <utility>

namespace mixology::mix
{
    void AutoAnalyze::start (const Style& s)
    {
        style = s;
        reset();
    }

    AutoAnalyze::Step AutoAnalyze::add (Moment m, std::uint64_t mixKey)
    {
        std::set<std::uint32_t> nowTracks;
        std::map<std::uint32_t, double> nowFaders;
        for (const auto& t : m.tracks)
        {
            nowTracks.insert (t.hash);
            if (t.faderKnown) nowFaders[t.hash] = t.faderDb;
        }

        bool same = ! fresh && mixKey == key && nowTracks == tracks && nowFaders.size() == faders.size();
        if (same)
            for (const auto& [hash, db] : nowFaders)
            {
                const auto it = faders.find (hash);
                if (it == faders.end() || std::abs (it->second - db) > kFaderTolerance) { same = false; break; }
            }

        if (! same || window.full())
        {
            window.begin (style);
            window.setVocalOffset (style.vocalOffset, style.vocalFromYou);
            window.add (std::move (m));
            fresh = false;
            key = mixKey;
            tracks = std::move (nowTracks);
            faders = std::move (nowFaders);
            checkedAt = 0.0;
            return Step::restarted;
        }

        window.add (std::move (m));
        const auto s = window.secondsHeard();
        if (s + 1e-9 >= kEnoughSeconds && s - checkedAt + 1e-9 >= (checkedAt == 0.0 ? 0.0 : kRefreshSeconds))
        {
            checkedAt = s;
            return Step::check;
        }
        return Step::added;
    }

    bool adviceChanged (const Report& shown, const Report& fresh)
    {
        if (! fresh.valid) return false;
        if (! shown.valid) return true;

        auto points = [] (const Report& r) {
            std::set<std::pair<std::string, std::string>> s;
            for (const auto& sg : r.suggestions) s.insert ({ sg.id, sg.kind });
            return s;
        };
        if (points (shown) != points (fresh)) return true;

        auto names = [] (const Report& r) {
            std::set<std::uint32_t> s;
            for (const auto& t : r.tracks) s.insert (t.hash);
            return s;
        };
        if (names (shown) != names (fresh)) return true;

        if (shown.hasVocalRatio != fresh.hasVocalRatio) return true;
        if (shown.hasVocalRatio && std::abs (shown.vocalRatio - fresh.vocalRatio) >= 1.0) return true;
        if (std::abs (shown.mixLoudness - fresh.mixLoudness) >= 1.0) return true;
        if (shown.reference.heard != fresh.reference.heard) return true;
        return false;
    }
}
