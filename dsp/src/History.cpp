#include "mixology/History.h"

#include "mixology/FaderLaw.h"
#include "mixology/Pack.h"
#include "mixology/ProcParams.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace mixology::history
{
    namespace
    {
        constexpr const char* kMinus = "\xe2\x88\x92";   // the interface's minus sign
        constexpr const char* kArrow = " \xe2\x86\x92 ";

        std::string number (double v, int decimals, bool sign = false)
        {
            char b[48];
            std::snprintf (b, sizeof (b), "%.*f", decimals, std::abs (v));
            const bool zero = std::abs (v) < 0.5 * std::pow (10.0, -decimals);
            return (zero ? "" : v < 0.0 ? kMinus : sign ? "+" : "") + std::string (b);
        }

        std::string faderDb (double db) { return db <= FaderLaw::kMinusInf + 1.0 || db < -144.0 ? std::string (kMinus) + "\xe2\x88\x9e" : number (db, 1); }

        std::string hz (double v)
        {
            if (v >= 1000.0)
            {
                auto s = number (v / 1000.0, v >= 10000.0 ? 1 : 2);
                while (s.find ('.') != std::string::npos && (s.back() == '0' || s.back() == '.')) s.pop_back();
                return s + " kHz";
            }
            return number (v, 0) + " Hz";
        }

        std::string choice (const char* choices, int index)
        {
            std::string all (choices != nullptr ? choices : "");
            for (int i = 0; i < index; ++i)
            {
                const auto bar = all.find ('|');
                if (bar == std::string::npos) return std::to_string (index);
                all.erase (0, bar + 1);
            }
            return all.substr (0, all.find ('|'));
        }

        std::string value (const proc::ParamSpec& s, double v)
        {
            if (s.kind == proc::Kind::toggle) return v > 0.5 ? "on" : "off";
            if (s.kind == proc::Kind::choice) return choice (s.choices, static_cast<int> (std::lround (v)));
            const std::string unit = s.unit != nullptr ? s.unit : "";
            if (unit == "dB") return number (v, 1, true) + " dB";
            if (unit == "Hz") return hz (v);
            if (unit == "ms") return number (v, v < 10.0 ? 1 : 0) + " ms";
            if (unit == "%") return number (v, 0) + " %";
            if (unit == ":1") return number (v, 1) + ":1";
            return number (v, 2) + (unit.empty() ? "" : " " + unit);
        }

        std::string who (const Change& c)
        {
            if (c.fullMix) return "Full Mix";
            return c.track.empty() ? std::string ("A track") : "'" + c.track + "'";
        }

        bool close (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

        /** Adds a change to a list, one per value: a value changed again keeps its first 'before'. */
        void add (std::vector<Change>& into, const Change& c)
        {
            for (auto& x : into)
                if (x.sameTarget (c))
                {
                    x.after = c.after;
                    x.afterPos = c.afterPos;
                    x.listAfter = c.listAfter;
                    if (! c.track.empty()) x.track = c.track;
                    return;
                }
            into.push_back (c);
        }

        void prune (std::vector<Change>& v)
        {
            v.erase (std::remove_if (v.begin(), v.end(), [] (const Change& c) { return c.noChange(); }), v.end());
        }

        std::string plural (std::size_t n, const char* one, const char* many)
        {
            return std::to_string (n) + " " + (n == 1 ? one : many);
        }
    }

    // ---------------------------------------------------------------------------------------------

    bool Change::sameTarget (const Change& o) const
    {
        if (what != o.what || fullMix != o.fullMix || hash != o.hash) return false;
        if (! fullMix && hash == 0 && (slot != o.slot || generation != o.generation)) return false;
        return (what != What::proc && what != What::assist) || index == o.index;
    }

    bool Change::noChange() const
    {
        switch (what)
        {
            case What::duck: return listBefore == listAfter;
            case What::fader:
                if (beforePos >= 0.0 && afterPos >= 0.0) return close (beforePos, afterPos, 1.0e-7);
                return close (before, after, 0.005);
            case What::mute: case What::solo: return (before > 0.5) == (after > 0.5);
            default: return close (before, after, 1.0e-4 * std::max (1.0, std::abs (before)));
        }
    }

    Change Change::reversed() const
    {
        auto r = *this;
        std::swap (r.before, r.after);
        std::swap (r.beforePos, r.afterPos);
        std::swap (r.listBefore, r.listAfter);
        return r;
    }

    // ---------------------------------------------------------------------------------------------

    std::uint64_t History::record (const std::string& what, const std::string& group, std::vector<Change> changes, double now, bool pending)
    {
        const bool anyUndone = std::any_of (list.begin(), list.end(), [] (const Step& s) { return s.undone; });
        if (! anyUndone && ! list.empty() && ! pending)
        {
            auto& last = list.back();
            if (! group.empty() && last.group == group && ! last.pending && now >= last.lastAt && now - last.lastAt <= kMergeSeconds)
            {
                for (const auto& c : changes) add (last.changes, c);
                prune (last.changes);
                last.lastAt = now;
                ++changeCount;
                if (! last.changes.empty()) return last.id;
                list.pop_back();   // dragged away and back: nothing changed after all
                return 0;
            }
        }

        Step s;
        s.what = what;
        s.group = group;
        s.time = now;
        s.lastAt = now;
        s.pending = pending;
        for (const auto& c : changes) add (s.changes, c);
        prune (s.changes);
        if (s.changes.empty() && ! pending) return 0;

        dropUndone();   // a new change: the steps you went back over are gone
        s.id = nextId++;
        list.push_back (std::move (s));
        while (list.size() > kMaxSteps) list.pop_front();
        ++changeCount;
        return list.back().id;
    }

    bool History::amend (std::uint64_t id, std::vector<Change> changes)
    {
        const int at = indexOf (id);
        if (at <= 0) return false;
        auto& s = list[static_cast<std::size_t> (at - 1)];
        for (const auto& c : changes) add (s.changes, c);
        prune (s.changes);
        s.pending = false;
        ++changeCount;
        if (s.changes.empty()) list.erase (list.begin() + (at - 1));
        return true;
    }

    void History::clear()
    {
        list.clear();
        ++changeCount;
    }

    namespace
    {
        constexpr std::uint32_t kSavedMagic = 0x3148584du;   // "MXH1"

        void writeChange (pack::Writer& w, const Change& c)
        {
            w.u8 (static_cast<std::uint8_t> (c.what));
            w.u32 (c.hash);
            w.flag (c.fullMix);
            w.str (c.track);
            w.i32 (c.index);
            w.f64 (c.before);
            w.f64 (c.after);
            w.f64 (c.beforePos);
            w.f64 (c.afterPos);
            for (const auto* list : { &c.listBefore, &c.listAfter })
            {
                w.u32 (static_cast<std::uint32_t> (list->size()));
                for (auto h : *list) w.u32 (h);
            }
        }

        bool readChange (pack::Reader& r, Change& c)
        {
            const auto what = r.u8();
            if (what > static_cast<std::uint8_t> (What::duck)) return false;
            c.what = static_cast<What> (what);
            c.hash = r.u32();
            c.fullMix = r.flag();
            c.track = r.str();
            c.index = r.i32();
            c.before = r.f64();
            c.after = r.f64();
            c.beforePos = r.f64();
            c.afterPos = r.f64();
            for (auto* list : { &c.listBefore, &c.listAfter })
            {
                const auto n = r.count (4);
                list->clear();
                for (std::uint32_t i = 0; i < n; ++i) list->push_back (r.u32());
            }
            // A Link without a name was found by its slot in that session's registry: never here.
            c.slot = -1;
            c.generation = 0;
            return r.ok();
        }
    }

    std::string History::save (std::size_t maxChanges) const
    {
        // The newest steps that fit: the oldest ones go first when there are too many changes.
        std::size_t first = list.size(), total = 0;
        while (first > 0 && total + list[first - 1].changes.size() <= maxChanges)
            total += list[--first].changes.size();

        pack::Writer w;
        w.u32 (kSavedMagic);
        w.u64 (nextId);
        w.u32 (static_cast<std::uint32_t> (list.size() - first));
        for (std::size_t i = first; i < list.size(); ++i)
        {
            const auto& s = list[i];
            w.u64 (s.id);
            w.f64 (s.time);
            w.f64 (s.lastAt);
            w.str (s.what);
            w.str (s.group);
            w.flag (s.undone);
            w.u32 (static_cast<std::uint32_t> (s.changes.size()));
            for (const auto& c : s.changes) writeChange (w, c);
        }
        return w.take();
    }

    bool History::load (const std::string& block)
    {
        pack::Reader r (block);
        if (r.u32() != kSavedMagic) return false;
        const auto savedNext = r.u64();
        const auto n = r.count (16);
        std::deque<Step> steps;
        std::uint64_t lastId = 0;
        bool undoneSeen = false;
        for (std::uint32_t i = 0; i < n && r.ok(); ++i)
        {
            Step s;
            s.id = r.u64();
            s.time = r.f64();
            s.lastAt = r.f64();
            s.what = r.str();
            s.group = r.str();
            s.undone = r.flag();
            const auto changes = r.count (16);
            for (std::uint32_t k = 0; k < changes; ++k)
            {
                Change c;
                if (! readChange (r, c)) return false;
                s.changes.push_back (std::move (c));
            }
            // Ids only grow, and undone steps are always the newest ones: anything else isn't a saved history.
            if (s.id <= lastId || (undoneSeen && ! s.undone)) return false;
            lastId = s.id;
            undoneSeen = undoneSeen || s.undone;
            if (! s.changes.empty()) steps.push_back (std::move (s));
        }
        if (! r.ok()) return false;
        while (steps.size() > kMaxSteps) steps.pop_front();
        list = std::move (steps);
        nextId = std::max (savedNext, lastId + 1);
        ++changeCount;
        return true;
    }

    const Step* History::find (std::uint64_t id) const
    {
        const int at = indexOf (id);
        return at > 0 ? &list[static_cast<std::size_t> (at - 1)] : nullptr;
    }

    std::uint64_t History::current() const
    {
        for (auto it = list.rbegin(); it != list.rend(); ++it)
            if (! it->undone) return it->id;
        return 0;
    }

    bool History::canRedo() const { return ! list.empty() && list.back().undone; }

    bool History::busy() const
    {
        return std::any_of (list.begin(), list.end(), [] (const Step& s) { return s.pending; });
    }

    int History::indexOf (std::uint64_t id) const
    {
        if (id == 0) return 0;
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].id == id) return static_cast<int> (i) + 1;
        return -1;
    }

    void History::dropUndone()
    {
        while (! list.empty() && list.back().undone) list.pop_back();
    }

    std::vector<Change> History::moveTo (std::uint64_t id)
    {
        const int target = indexOf (id);
        if (target < 0 || busy()) return {};
        int now = 0;   // the done steps are always first: the undone ones are a run at the end
        while (now < static_cast<int> (list.size()) && ! list[static_cast<std::size_t> (now)].undone) ++now;

        std::vector<Change> out;
        auto take = [&out] (const Change& c) {
            // One per value: the first one's 'before' (where it is now), the last one's 'after'.
            for (auto& x : out)
                if (x.sameTarget (c))
                {
                    x.after = c.after;
                    x.afterPos = c.afterPos;
                    x.listAfter = c.listAfter;
                    return;
                }
            out.push_back (c);
        };
        if (target < now)
            for (int i = now - 1; i >= target; --i)
            {
                auto& s = list[static_cast<std::size_t> (i)];
                for (auto c = s.changes.rbegin(); c != s.changes.rend(); ++c) take (c->reversed());
                s.undone = true;
            }
        else if (target > now)
            for (int i = now; i < target; ++i)
            {
                auto& s = list[static_cast<std::size_t> (i)];
                for (const auto& c : s.changes) take (c);
                s.undone = false;
            }
        else
            return {};
        ++changeCount;
        prune (out);
        return out;
    }

    std::vector<Change> History::undo()
    {
        const auto cur = current();
        if (cur == 0) return {};
        const int at = indexOf (cur);
        return moveTo (at >= 2 ? list[static_cast<std::size_t> (at - 2)].id : 0);
    }

    std::vector<Change> History::redo()
    {
        if (! canRedo()) return {};
        const int at = indexOf (current());   // 0 when everything is undone
        return moveTo (list[static_cast<std::size_t> (at)].id);
    }

    // ---------------------------------------------------------------------------------------------

    const char* pageOf (int i)
    {
        using namespace proc;
        if (i == chainOn) return "Processing";
        if (i >= eqOn && i <= eq6Q) return "EQ";
        if (i >= compOn && i <= limDetect) return "Dynamics";
        if (i >= tbOn && i <= finOut) return "Tone";
        if (i >= stOn && i <= stHighX) return "Stereo";
        return "Space";
    }

    std::string panText (double pan)
    {
        const int v = static_cast<int> (std::lround ((std::clamp (pan, 0.0, 1.0) - 0.5) * 200.0));
        return v == 0 ? std::string ("C") : (v < 0 ? "L" : "R") + std::to_string (std::abs (v));
    }

    std::string describe (const Change& c)
    {
        const auto t = who (c);
        switch (c.what)
        {
            case What::fader: return t + " fader " + faderDb (c.before) + kArrow + faderDb (c.after) + " dB";
            case What::pan:   return t + " pan " + panText (c.before) + kArrow + panText (c.after);
            case What::mute:  return t + (c.after > 0.5 ? " muted" : " unmuted");
            case What::solo:  return t + (c.after > 0.5 ? " soloed" : " solo off");
            case What::trim:  return t + " Trim " + number (c.before, 1, true) + kArrow + number (c.after, 1, true) + " dB";
            case What::duck:
                return t + (c.listAfter.empty() ? " Space no longer ducks"
                                                : " Space ducks under " + plural (c.listAfter.size(), "track", "tracks"));
            case What::assist:
            {
                static const char* names[] = { "low cut", "mud dip at", "mud dip", "pocket dip at", "pocket dip" };
                const int i = std::clamp (c.index, 0, kAssistValues - 1);
                auto v = [i] (double x) {
                    if (i == assistLowCutHz) return x < 20.0 ? std::string ("off") : hz (x);
                    if (i == assistMudHz || i == assistPocketHz) return hz (x);
                    return number (x, 1, true) + " dB";
                };
                return t + " Assist EQ " + names[i] + " " + v (c.before) + kArrow + v (c.after);
            }
            case What::proc:
            {
                if (c.index < 0 || c.index >= proc::count) return t + " processing";
                const auto& s = proc::specs()[static_cast<std::size_t> (c.index)];
                if (s.kind == proc::Kind::toggle) return t + " " + s.name + (c.after > 0.5 ? " on" : " off");
                return t + " " + s.name + " " + value (s, c.before) + kArrow + value (s, c.after);
            }
        }
        return t;
    }

    std::string summary (const Step& s)
    {
        if (s.changes.empty()) return s.pending ? "Moving faders in Cubase..." : "Nothing changed";
        if (s.changes.size() == 1) return describe (s.changes.front());

        std::size_t faders = 0, pans = 0, switches = 0, trims = 0, ducks = 0;
        std::set<std::pair<std::uint32_t, int>> procTracks, assistTracks;
        bool procMix = false;
        for (const auto& c : s.changes)
            switch (c.what)
            {
                case What::fader: ++faders; break;
                case What::pan: ++pans; break;
                case What::mute: case What::solo: ++switches; break;
                case What::trim: ++trims; break;
                case What::duck: ++ducks; break;
                case What::assist: assistTracks.insert ({ c.hash, c.slot }); break;
                case What::proc:
                    if (c.fullMix) procMix = true;
                    else procTracks.insert ({ c.hash, c.slot });
                    break;
            }
        // One track's processing only: what changed on it ("EQ 2 Gain, EQ 2 Freq" or "6 controls").
        if (faders + pans + switches + trims + ducks + assistTracks.size() == 0 && procTracks.size() + (procMix ? 1 : 0) == 1)
        {
            if (s.changes.size() <= 3)
            {
                std::string out;
                for (const auto& c : s.changes)
                    out += (out.empty() ? "" : "; ") + describe (c);
                return out;
            }
            return who (s.changes.front()) + ": " + plural (s.changes.size(), "control", "controls");
        }

        std::vector<std::string> parts;
        if (faders > 0) parts.push_back (plural (faders, "fader", "faders"));
        if (pans > 0) parts.push_back (plural (pans, "pan", "pans"));
        if (switches > 0) parts.push_back (plural (switches, "mute / solo", "mutes / solos"));
        if (trims > 0) parts.push_back (plural (trims, "Trim", "Trims"));
        if (! procTracks.empty() || procMix)
        {
            std::string p = "processing on ";
            if (! procTracks.empty()) p += plural (procTracks.size(), "track", "tracks");
            if (procMix) p += procTracks.empty() ? "the Full Mix" : " and the Full Mix";
            parts.push_back (p);
        }
        if (! assistTracks.empty()) parts.push_back ("Assist EQ on " + plural (assistTracks.size(), "track", "tracks"));
        if (ducks > 0) parts.push_back ("ducking on " + plural (ducks, "track", "tracks"));
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i)
            out += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
        return out;
    }
}
