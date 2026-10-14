#include "mixology/AssistRestore.h"

#include <cmath>

namespace mixology
{
    bool AssistRestore::same (const AssistSettings& a, const AssistSettings& b)
    {
        auto hz = [] (float x, float y) { return std::abs (x - y) <= std::max (1.5f, 0.01f * std::max (x, y)); };
        auto db = [] (float x, float y) { return std::abs (x - y) <= 0.15f; };
        if (a.lowCutOn() != b.lowCutOn()) return false;
        if (a.lowCutOn() && ! hz (a.lowCutHz, b.lowCutHz)) return false;
        const bool mudA = a.mudDb < -0.05f, mudB = b.mudDb < -0.05f;
        if (mudA != mudB || (mudA && (! db (a.mudDb, b.mudDb) || ! hz (a.mudHz, b.mudHz)))) return false;
        const bool pocketA = a.pocketDb < -0.05f, pocketB = b.pocketDb < -0.05f;
        if (pocketA != pocketB || (pocketA && (! db (a.pocketDb, b.pocketDb) || ! hz (a.pocketHz, b.pocketHz)))) return false;
        return true;
    }

    void AssistRestore::noteSet (std::uint32_t hash, const AssistSettings& before, const AssistSettings& now)
    {
        auto it = points.find (hash);
        if (it == points.end()) it = points.emplace (hash, Point { before, now }).first;
        else it->second.set = now;
        if (same (it->second.before, it->second.set)) points.erase (it);   // back where it started: nothing to restore
    }

    void AssistRestore::noteUndo (std::uint32_t hash, const AssistSettings& now)
    {
        auto it = points.find (hash);
        if (it == points.end()) return;
        it->second.set = now;
        if (same (it->second.before, now)) points.erase (it);
    }

    AssistRestore::Decision AssistRestore::decide (std::uint32_t hash, const AssistSettings& current) const
    {
        const auto it = points.find (hash);
        if (it == points.end()) return {};
        if (same (current, it->second.before)) return {};                        // already back
        if (! same (current, it->second.set)) return { Action::keptYours, {} };  // changed since: yours
        return { Action::restore, it->second.before };
    }
}
