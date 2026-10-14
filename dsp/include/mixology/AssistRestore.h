#pragma once

#include "mixology/AssistEq.h"

#include <cstdint>
#include <map>

/** Restore points for the Assist EQ Mixology sets in each track's Mixology Link (Clean Up, Vocal Pocket, Auto Mix).

    Like the fader restore points: the first time Mixology changes a track's Assist EQ, how it was is kept; Restore
    puts it back. A track whose Assist EQ was changed since by someone else (you, in the Link window or Cubase) is
    left as it is. Framework-free and unit-tested; the plug-in applies the decisions through the Link registry. */
namespace mixology
{
    class AssistRestore
    {
    public:
        /** Mixology set `now` on a track whose Assist EQ was `before`. */
        void noteSet (std::uint32_t hash, const AssistSettings& before, const AssistSettings& now);
        /** An Undo put `now` back on the track (gone when that is how it was before Mixology's first change). */
        void noteUndo (std::uint32_t hash, const AssistSettings& now);

        enum class Action { none, restore, keptYours };
        struct Decision
        {
            Action action = Action::none;
            AssistSettings to;   // for restore: what to request
        };
        /** What Restore does for this track, given its Assist EQ now. */
        Decision decide (std::uint32_t hash, const AssistSettings& current) const;

        void forget (std::uint32_t hash) { points.erase (hash); }
        void clear() { points.clear(); }
        bool has (std::uint32_t hash) const { return points.count (hash) != 0; }
        std::size_t size() const { return points.size(); }
        const auto& all() const { return points; }

        /** Equal as far as the Link's parameters can tell (their steps: 1 Hz, 0.1 dB). */
        static bool same (const AssistSettings&, const AssistSettings&);

    private:
        struct Point
        {
            AssistSettings before;   // before Mixology's first change
            AssistSettings set;      // what Mixology set last
        };
        std::map<std::uint32_t, Point> points;
    };
}
