// The Sonicology Rack's Coach on one of Mixology's tracks (Beta 28): every page's AUTO rules at once (makeCoachPlan),
// nothing done twice, nothing left after applying it, and the plan as the Rack reads it (valid JSON in the report's
// room, the tone before and after from the plan's own EQ / Tone Balance curves).

#include "mixology/BridgeProtocol.h"
#include "mixology/MixAnalysis.h"
#include "mixology/MixPlanner.h"
#include "mixology/TrackCoach.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace mixology;
using namespace mixology::mix;

namespace
{
    double oct (double hz, double ref) { return std::log2 (hz / ref); }
    std::vector<double> plateau (double level, double lo, double hi, double below, double above)
    {
        std::vector<double> b (static_cast<std::size_t> (kBands));
        for (int i = 0; i < kBands; ++i)
        {
            const double hz = bandHz (i);
            double v = level;
            if (hz < lo) v -= below * oct (lo, hz);
            if (hz > hi) v -= above * oct (hz, hi);
            b[static_cast<std::size_t> (i)] = std::max (v, -110.0);
        }
        return b;
    }
    std::vector<double> plus (std::vector<double> b, double hz, double peak, double width)
    {
        for (int i = 0; i < kBands; ++i)
        {
            const double v = peak - 12.0 * std::pow (oct (bandHz (i), hz) / width, 2.0);
            auto& x = b[static_cast<std::size_t> (i)];
            x = 10.0 * std::log10 (std::pow (10.0, x / 10.0) + std::pow (10.0, v / 10.0));
        }
        return b;
    }
    /** Keys: muddy (a bump at 315 Hz) and dull (falling fast above 800 Hz), as AUTO's own tests make them. */
    std::vector<double> muddyDullKeys() { return plus (plateau (-18.0, 100.0, 800.0, 18.0, 14.0), 315.0, -10.0, 0.6); }

    TrackReport track (const std::string& name, Role role, std::vector<double> bands)
    {
        TrackReport t;
        t.hash = bridge::nameHash (name);
        t.name = name;
        t.role = role;
        t.roleFrom = "you";
        t.activity = 0.8;
        t.activeSeconds = 48.0;
        t.loudness = -16.0;
        t.loudPeak = -6.0;
        t.levelRange = 4.0;
        t.lowRange = 4.0;
        t.crest = 12.0;
        t.width = 0.04;
        t.lowWidth = 0.02;
        t.correlation = 0.8;
        t.activeRms = -20.0;
        t.bands = std::move (bands);
        return t;
    }
    ChannelNow link (const std::string& name)
    {
        ChannelNow c;
        c.hash = bridge::nameHash (name);
        c.name = name;
        c.faderKnown = true;
        c.hasLink = true;
        c.channels = 2;
        c.hasProc = true;
        c.proc = proc::defaults();
        c.procAtAnalysis = c.proc;
        c.procAtAnalysisKnown = true;
        c.assistAtAnalysisKnown = true;
        return c;
    }
    Report report (std::vector<TrackReport> tracks)
    {
        Report r;
        r.valid = true;
        r.secondsHeard = 60.0;
        r.mixMoments = 300;
        r.style.key = "pop.modern";
        r.style.name = "Pop - Modern";
        r.style.bass = 3.0; r.style.scoop = 1.5; r.style.air = 3.0;
        r.tracks = std::move (tracks);
        return r;
    }
    void apply (std::vector<ChannelNow>& chans, const Plan& p)
    {
        for (const auto& c : p.changes)
            for (auto& ch : chans)
            {
                if (ch.hash != c.hash || c.fullMix) continue;
                for (const auto& [id, v] : c.proc) ch.proc[static_cast<std::size_t> (id)] = v;
                if (c.setDuck) ch.duck = c.duck;
            }
    }

    /** A small JSON checker: one value, nothing after it. */
    struct Checker
    {
        const std::string& s;
        std::size_t i = 0;
        void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) ++i; }
        bool lit (const char* w) { const std::string t (w); if (s.compare (i, t.size(), t) != 0) return false; i += t.size(); return true; }
        bool str()
        {
            if (i >= s.size() || s[i] != '"') return false;
            for (++i; i < s.size(); ++i)
            {
                if (s[i] == '\\') { ++i; continue; }
                if (static_cast<unsigned char> (s[i]) < 0x20) return false;
                if (s[i] == '"') { ++i; return true; }
            }
            return false;
        }
        bool number()
        {
            const std::size_t start = i;
            if (i < s.size() && s[i] == '-') ++i;
            while (i < s.size() && (std::isdigit (static_cast<unsigned char> (s[i])) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
            return i > start;
        }
        bool value()
        {
            ws();
            if (i >= s.size()) return false;
            bool ok = false;
            if (s[i] == '{')
            {
                ++i; ws();
                if (i < s.size() && s[i] == '}') { ++i; ok = true; }
                else
                    for (;;)
                    {
                        ws();
                        if (! str()) return false;
                        ws();
                        if (i >= s.size() || s[i++] != ':' || ! value()) return false;
                        ws();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == '}') { ++i; ok = true; }
                        break;
                    }
            }
            else if (s[i] == '[')
            {
                ++i; ws();
                if (i < s.size() && s[i] == ']') { ++i; ok = true; }
                else
                    for (;;)
                    {
                        if (! value()) return false;
                        ws();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == ']') { ++i; ok = true; }
                        break;
                    }
            }
            else if (s[i] == '"') ok = str();
            else if (s[i] == 't') ok = lit ("true");
            else if (s[i] == 'f') ok = lit ("false");
            else if (s[i] == 'n') ok = lit ("null");
            else ok = number();
            ws();
            return ok;
        }
    };
    bool validJson (const std::string& s) { Checker c { s }; return c.value() && c.i == s.size(); }
    bool has (const std::string& s, const std::string& part) { return s.find (part) != std::string::npos; }
}

TEST_CASE ("Coach (a Mixology track): every page at once, nothing done twice, nothing left after it", "[coach]")
{
    const auto keys = track ("Keys", Role::keys, muddyDullKeys());
    const auto r = report ({ keys });
    std::vector<ChannelNow> chans { link ("Keys") };

    const auto plan = makeCoachPlan (keys.hash, r, chans);
    INFO (plan.line);
    REQUIRE (plan.valid);
    REQUIRE_FALSE (plan.changes.empty());
    CHECK (plan.title == "Coach");
    CHECK (plan.line.rfind ("AUTO", 0) != 0);      // the Coach's own words, not AUTO's
    // More than one page: the mud on the EQ, and more (the top, dynamics, width...)
    std::vector<std::string> kinds;
    for (const auto& c : plan.changes)
        if (std::find (kinds.begin(), kinds.end(), c.kind) == kinds.end()) kinds.push_back (c.kind);
    CHECK (std::find (kinds.begin(), kinds.end(), "eq") != kinds.end());
    CHECK (kinds.size() >= 2);
    // the dull top fixed once (on the EQ or on Tone Balance, never both)
    CHECK (std::count_if (plan.changes.begin(), plan.changes.end(), [] (const Change& c) { return c.heard == "dull"; }) <= 1);
    for (const auto& c : plan.changes) CHECK (c.hash == keys.hash);

    // Applied: the Coach (and AUTO on every page) finds nothing more.
    apply (chans, plan);
    const auto again = makeCoachPlan (keys.hash, r, chans);
    INFO (again.line);
    CHECK (again.valid);
    CHECK (again.changes.empty());
    for (auto page : { Page::eq, Page::dynamics, Page::space, Page::tone, Page::stereo })
        CHECK (makeTrackPlan (page, keys.hash, r, chans).changes.empty());
}

TEST_CASE ("Coach (a Mixology track): the same refusals as AUTO, in its own words", "[coach]")
{
    const auto keys = track ("Keys", Role::keys, muddyDullKeys());
    auto r = report ({ keys });
    std::vector<ChannelNow> chans { link ("Keys") };
    CHECK_FALSE (makeCoachPlan (0, r, chans).valid);
    auto locked = chans;
    locked[0].locked = true;
    const auto l = makeCoachPlan (keys.hash, r, locked);
    CHECK_FALSE (l.valid);
    CHECK (has (l.line, "padlock"));
    auto off = chans;
    off[0].proc[static_cast<std::size_t> (proc::chainOn)] = 0.0f;
    CHECK (has (makeCoachPlan (keys.hash, r, off).line, "switched off"));
    auto unheard = r;
    unheard.valid = false;
    CHECK (has (makeCoachPlan (keys.hash, unheard, chans).line, "ANALYZE & BUILD"));
    const auto other = makeCoachPlan (bridge::nameHash ("Strings"), r, chans);
    CHECK_FALSE (other.valid);
}

TEST_CASE ("Coach (a Mixology track): the plan as the Rack reads it, the tone before and after from the plan's curves", "[coach]")
{
    const auto keys = track ("Keys", Role::keys, muddyDullKeys());
    const auto r = report ({ keys });
    std::vector<ChannelNow> chans { link ("Keys") };
    const auto plan = makeCoachPlan (keys.hash, r, chans);
    REQUIRE (plan.valid);

    const auto json = coach::planJson (plan, r, keys.hash, chans, 7000);
    INFO (json);
    CHECK (validJson (json));
    CHECK (json.size() <= 7000);
    CHECK (has (json, "\"ok\":true"));
    CHECK (has (json, "\"kind\":\"mix\""));
    CHECK (has (json, "\"key\":\"mix.eq\""));
    CHECK (has (json, "\"before\":["));
    CHECK (has (json, "\"after\":["));
    CHECK (has (json, "\"name\":\"Mud\""));
    CHECK (has (json, "\"role\":\"Keys\""));

    // The mud region: the plan's EQ takes it down, measured on the curves (exact filter responses).
    const auto& g = coach::regions()[1];
    REQUIRE (std::string (g.key) == "mud");
    auto after = keys.bands;
    auto then = chans[0].proc;
    for (const auto& c : plan.changes)
        for (const auto& [id, v] : c.proc) then[static_cast<std::size_t> (id)] = v;
    for (int i = 0; i < kBands; ++i) after[static_cast<std::size_t> (i)] += coach::toneResponseDb (then, bandHz (i));
    CHECK (coach::regionDb (after, g.from, g.to) < coach::regionDb (keys.bands, g.from, g.to) - 1.0);
    CHECK (coach::toneResponseDb (proc::defaults(), 1000.0) == Catch::Approx (0.0));

    // Squeezed: still valid, shorter, then smaller.
    for (const std::size_t room : { std::size_t { 3000 }, std::size_t { 1500 }, std::size_t { 600 } })
    {
        INFO (room);
        const auto small = coach::planJson (plan, r, keys.hash, chans, room);
        CHECK (validJson (small));
        CHECK (small.size() <= room);
    }

    // A refusal: why, as the Rack shows it.
    auto locked = chans;
    locked[0].locked = true;
    const auto refused = coach::planJson (makeCoachPlan (keys.hash, r, locked), r, keys.hash, locked, 7000);
    CHECK (validJson (refused));
    CHECK (has (refused, "\"ok\":false"));
    CHECK (has (refused, "padlock"));
}

TEST_CASE ("Coach (a Mixology track): print a plan (manual check)", "[coach][.print]")
{
    const auto keys = track ("Keys", Role::keys, muddyDullKeys());
    const auto r = report ({ keys });
    std::vector<ChannelNow> chans { link ("Keys") };
    const auto plan = makeCoachPlan (keys.hash, r, chans);
    std::printf ("%s\n\n%s\n", plan.line.c_str(), coach::planJson (plan, r, keys.hash, chans, 7000).c_str());
}
