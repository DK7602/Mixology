// AUTO on the processing pages (EQ, Dynamics, Space, Tone, Stereo): the per-track rules Polish and Auto Mix use, for
// one page of one track; the "beat" role; listening to a track Analyze Mix didn't hear.

#include "mixology/BridgeProtocol.h"
#include "mixology/MixAnalysis.h"
#include "mixology/MixPlanner.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

using namespace mixology;
using namespace mixology::mix;

namespace
{
    // ---- spectra (dB per 1/3 octave band) of the kinds of track, typical and with a clear problem ----------------

    double oct (double hz, double ref) { return std::log2 (hz / ref); }

    /** Flat between lo and hi, falling `below` dB per octave under lo and `above` over hi. */
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

    /** A bump of `peak` dB at `hz` added (as energy) to a spectrum: `width` octaves to its -12 dB points. */
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

    std::vector<double> sum (const std::vector<std::vector<double>>& parts)
    {
        std::vector<double> b (static_cast<std::size_t> (kBands), -120.0);
        for (const auto& p : parts)
            for (std::size_t i = 0; i < b.size(); ++i) b[i] = 10.0 * std::log10 (std::pow (10.0, b[i] / 10.0) + std::pow (10.0, p[i] / 10.0));
        return b;
    }

    std::vector<double> vocal (double slope = 6.0, double airExtra = 2.0)
    {
        auto b = plateau (-14.0, 160.0, 630.0, 18.0, slope);
        for (int i = 0; i < kBands; ++i)
            if (bandHz (i) > 7000.0) b[static_cast<std::size_t> (i)] += airExtra;
        return b;
    }
    std::vector<double> kick (double box = 0.0) { return plus (plus (plateau (-60.0, 40.0, 8000.0, 24.0, 12.0), 60.0, -10.0, 1.6), 400.0, -26.0 + box, 0.5); }
    std::vector<double> snare (double crack = 0.0, double box = 0.0)
    {
        return plus (plus (plus (plateau (-40.0, 150.0, 9000.0, 18.0, 12.0), 200.0, -14.0, 0.8), 3500.0, -20.0 + crack, 1.6), 600.0, -30.0 + box, 0.5);
    }
    std::vector<double> hats() { return plus (plateau (-60.0, 3000.0, 16000.0, 24.0, 12.0), 10000.0, -22.0, 1.4); }
    std::vector<double> perc() { return plus (plateau (-50.0, 1000.0, 12000.0, 18.0, 12.0), 5000.0, -24.0, 1.6); }
    std::vector<double> bass808 (double mud = -60.0) { return plus (plus (plateau (-90.0, 30.0, 300.0, 24.0, 24.0), 50.0, -10.0, 0.8), 300.0, mud, 0.6); }
    std::vector<double> keys (double above = 6.0, double mudBump = -80.0) { return plus (plateau (-18.0, 100.0, 800.0, 18.0, above), 315.0, mudBump, 0.6); }
    std::vector<double> synth (double above = 6.0) { return plateau (-20.0, 200.0, 1500.0, 12.0, above); }
    std::vector<double> guitar (double above = 9.0) { return plateau (-18.0, 150.0, 2500.0, 18.0, above); }
    /** A whole beat that sits right on the style's target curve (a beat that fits); `top` / `low` dB more or less
        above 6 kHz / under 160 Hz. */
    std::vector<double> beat (const Style& st, double top = 0.0, double low = 0.0)
    {
        std::vector<double> b (static_cast<std::size_t> (kBands));
        for (int i = 0; i < kBands; ++i)
        {
            const double hz = bandHz (i);
            double v = -18.0 + Analysis::targetDb (st, hz) - 1.5 * std::log2 (hz / 1000.0);
            if (hz < 30.0) v -= 18.0 * oct (30.0, hz);   // nothing under its notes
            if (hz > 6000.0) v += top * std::min (1.0, oct (hz, 6000.0));
            if (hz < 160.0) v += low;
            b[static_cast<std::size_t> (i)] = v;
        }
        return b;
    }
    Style popStyle()
    {
        Style s;
        s.key = "pop.modern";
        s.name = "Pop - Modern";
        s.bass = 3.0; s.scoop = 1.5; s.air = 3.0;
        return s;
    }

    /** A peak `peak` dB over its neighbours at band b. */
    std::vector<double> peaked (std::vector<double> b, int band, double peak)
    {
        b[static_cast<std::size_t> (band)] += peak;
        return b;
    }
    /** Rumble: energy under 100 Hz it doesn't need. */
    std::vector<double> rumbly (std::vector<double> b, double level = -20.0) { return plus (b, 40.0, level, 0.8); }

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
        t.width = 0.15;
        t.lowWidth = 0.02;
        t.correlation = 0.8;
        t.activeRms = -20.0;
        t.bands = std::move (bands);
        return t;
    }

    ChannelNow link (const std::string& name, int channels = 2)
    {
        ChannelNow c;
        c.hash = bridge::nameHash (name);
        c.name = name;
        c.faderKnown = true;
        c.hasLink = true;
        c.channels = channels;
        c.hasProc = true;
        c.proc = proc::defaults();
        c.procAtAnalysis = c.proc;
        c.procAtAnalysisKnown = true;
        c.assistAtAnalysisKnown = true;
        return c;
    }

    Report report (std::vector<TrackReport> tracks, const std::string& style = "pop.modern", const std::string& styleName = "Pop - Modern")
    {
        Report r;
        r.valid = true;
        r.secondsHeard = 60.0;
        r.mixMoments = 300;
        r.style.key = style;
        r.style.name = styleName;
        if (style.rfind ("pop", 0) == 0) { r.style.bass = 3.0; r.style.scoop = 1.5; r.style.air = 3.0; }
        r.tracks = std::move (tracks);
        return r;
    }

    constexpr Page kAllPages[] = { Page::eq, Page::dynamics, Page::space, Page::tone, Page::stereo };

    Plan autoOn (Page page, const std::string& name, const Report& r, const std::vector<ChannelNow>& chans)
    {
        return makeTrackPlan (page, bridge::nameHash (name), r, chans);
    }

    /** The Links after a plan's changes (processing and Assist EQ), Analyze Mix's numbers unchanged. */
    void applyPlan (std::vector<ChannelNow>& chans, const Plan& p)
    {
        for (const auto& c : p.changes)
            for (auto& ch : chans)
            {
                if (ch.hash != c.hash || c.fullMix) continue;
                for (const auto& [id, v] : c.proc) ch.proc[static_cast<std::size_t> (id)] = v;
                if (c.kind == "lowcut") ch.assist.lowCutHz = static_cast<float> (c.to);
                if (c.kind == "mud") { ch.assist.mudDb = static_cast<float> (c.to); ch.assist.mudHz = static_cast<float> (c.hz); }
                if (c.kind == "pocket") { ch.assist.pocketDb = static_cast<float> (c.to); ch.assist.pocketHz = static_cast<float> (c.hz); }
                if (c.setDuck) ch.duck = c.duck;
            }
    }

    float value (const Change& c, int id)
    {
        for (const auto& [i, v] : c.proc)
            if (i == id) return v;
        return -1.0e9f;
    }
    bool sets (const Change& c, int id) { return value (c, id) > -1.0e8f; }

    const Change* change (const Plan& p, const std::string& name, const std::string& kind)
    {
        for (const auto& c : p.changes)
            if (c.name == name && c.kind == kind) return &c;
        return nullptr;
    }

    bool contains (const std::string& s, const std::string& part) { return s.find (part) != std::string::npos; }

    // The controls of each page (what AUTO on that page may touch).
    bool onPage (int id, Page page)
    {
        const std::string key = proc::specs()[static_cast<std::size_t> (id)].id;
        auto starts = [&key] (const char* p) { return key.rfind (p, 0) == 0; };
        switch (page)
        {
            case Page::eq: return starts ("eq");
            case Page::dynamics: return starts ("comp_") || starts ("multi_") || starts ("trans_") || starts ("gate_");
            case Page::space: return starts ("sp_") || starts ("duck_") || starts ("rev_") || starts ("dly_") || starts ("cho_") || starts ("pha_")
                                     || starts ("hall_") || starts ("room_") || starts ("spx_");
            case Page::tone: return id == proc::tbOn || id == proc::tbWarmth || id == proc::tbPresence || id == proc::tbAir || id == proc::tbGlue || id == proc::tbPunch;
            case Page::stereo: return starts ("st_");
        }
        return false;
    }

    double level15 (const std::vector<double>& b, int from, int to)
    {
        double e = 0.0;
        for (int i = from; i <= to; ++i) e += std::pow (10.0, (b[static_cast<std::size_t> (i)] + 1.5 * std::log2 (bandHz (i) / 1000.0)) / 10.0);
        return 10.0 * std::log10 (e / (to - from + 1));
    }
}

TEST_CASE ("AUTO: print the test spectra's tone measures (manual check)", "[auto][.print]")
{
    const std::vector<std::pair<std::string, std::vector<double>>> all {
        { "vocal", vocal() }, { "kick", kick() }, { "kick boxy", kick (14.0) }, { "snare", snare() }, { "snare dull", snare (-14.0) }, { "snare boxy", snare (0.0, 18.0) },
        { "hats", hats() }, { "perc", perc() }, { "808", bass808() }, { "bass muddy", bass808 (-14.0) }, { "keys", keys() }, { "keys dull", keys (14.0) },
        { "keys muddy", keys (6.0, -10.0) }, { "synth", synth() }, { "synth bright", synth (0.0) }, { "guitar", guitar() }, { "guitar dull", guitar (20.0) },
        { "beat", beat (popStyle()) }, { "beat dull", beat (popStyle(), -10.0) }, { "beat thin", beat (popStyle(), 0.0, -8.0) } };
    for (const auto& [name, b] : all)
    {
        const double mid = level15 (b, 15, 20);
        auto plain = [&b] (int f, int t) { double e = 0.0; for (int i = f; i <= t; ++i) e += std::pow (10.0, b[static_cast<std::size_t> (i)] / 10.0); return 10.0 * std::log10 (e / (t - f + 1)); };
        std::printf ("%-12s top %6.1f low %6.1f mud %6.1f | crack %6.1f snbox %6.1f kbox %6.1f bassmud %6.1f sub %6.1f/%6.1f/%6.1f\n", name.c_str(), level15 (b, 26, 28) - mid,
                     level15 (b, 5, 8) - mid, level15 (b, 11, 14) - mid, level15 (b, 21, 24) - level15 (b, 9, 11),
                     plain (13, 16) - std::max (plain (9, 11), plain (21, 23)), plain (12, 14) - plain (4, 7), plain (11, 14) - plain (4, 8), b[0], b[2], b[4]);
    }
}

namespace
{
    /** One case: a track (and its Link), the page, what AUTO should do there. */
    struct Case
    {
        std::string what;            // for the failure message
        Page page = Page::eq;
        TrackReport t;
        int channels = 2;
        std::string style = "pop.modern", styleName = "Pop - Modern";
        std::string kind;            // the kind of change expected ("" = nothing)
        std::function<bool (const Change&)> check = [] (const Change&) { return true; };
        std::vector<std::string> others;   // other channels in the session (an FX return, the lead vocal...)
        std::function<void (std::vector<ChannelNow>&)> setup = [] (std::vector<ChannelNow>&) {};
    };

    std::vector<ChannelNow> channelsFor (const Case& k)
    {
        std::vector<ChannelNow> chans { link (k.t.name, k.channels) };
        for (const auto& n : k.others) chans.push_back (link (n, 2));
        k.setup (chans);
        return chans;
    }

    TrackReport with (TrackReport t, const std::function<void (TrackReport&)>& f) { f (t); return t; }

    Case want (std::string what, Page page, TrackReport t, std::string kind, std::function<bool (const Change&)> check = {}, int channels = 2)
    {
        Case k;
        k.what = std::move (what);
        k.page = page;
        k.t = std::move (t);
        k.kind = std::move (kind);
        if (check) k.check = std::move (check);
        k.channels = channels;
        return k;
    }

    std::vector<Case> problems()
    {
        const auto P = popStyle();
        auto rumble = [] (TrackReport t) { t.rumble = true; return t; };
        auto eqAt = [] (double lo, double hi, bool cut) {
            return [=] (const Change& c) { return c.hz >= lo && c.hz <= hi && (cut ? c.to < 0.0 : c.to > 0.0); };
        };
        auto lowCut = [] (double hz) {
            return [=] (const Change& c) { return value (c, proc::eqType (0)) == 3.0f && value (c, proc::eqFreq (0)) == static_cast<float> (hz); };
        };
        auto sub = [] (std::vector<double> b) { b[0] = std::max (b[2], b[4]) + 2.0; b[1] = b[0] - 1.0; return b; };
        auto swings = [] (TrackReport t, double range) { t.levelRange = range; t.loudPeak = -4.0; return t; };
        auto crest = [] (TrackReport t, double c) { t.crest = c; return t; };
        auto floor = [] (TrackReport t) { t.floorMoments = 40; t.floorRms = -72.0; t.floorPeak = -63.0; t.floorSpread = 2.0; t.activeRms = -22.0; return t; };
        auto comp = [] (double ratio) { return [=] (const Change& c) { return value (c, proc::compOn) == 1.0f && value (c, proc::compRatio) == static_cast<float> (ratio); }; };
        auto space = [] (proc::SpaceType type) {
            return [=] (const Change& c) { return value (c, proc::spOn) == 1.0f && value (c, proc::effectOn (type)) == 1.0f && value (c, proc::spRoute) == 0.0f; };
        };
        auto tone = [] (int knob, bool up) { return [=] (const Change& c) { return value (c, proc::tbOn) == 1.0f && (up ? value (c, knob) > 0.0f : value (c, knob) < 0.0f); }; };
        auto lows = [] (const Change& c) { return value (c, proc::stLow) == 0.0f; };
        auto width = [] (double lo, double hi) { return [=] (const Change& c) { const auto w = value (c, proc::stWidth); return w >= lo && w <= hi; }; };

        std::vector<Case> v;
        // ---- EQ
        v.push_back (want ("boxy kick", Page::eq, track ("Kick", Role::kick, kick (14.0)), "eq", eqAt (250.0, 520.0, true), 1));
        v.push_back (want ("kick rumble", Page::eq, track ("Kick", Role::kick, sub (kick())), "eq", lowCut (30.0), 1));
        v.push_back (want ("dull snare", Page::eq, track ("Snare", Role::snare, snare (-14.0)), "eq", eqAt (3000.0, 4000.0, false), 1));
        v.push_back (want ("boxy snare", Page::eq, track ("Snare", Role::snare, snare (0.0, 18.0)), "eq", eqAt (380.0, 850.0, true), 1));
        v.push_back (want ("hats with low bleed", Page::eq, track ("Hats", Role::hats, plus (hats(), 100.0, -30.0, 0.6)), "eq", lowCut (250.0)));
        v.push_back (want ("harsh hats", Page::eq, track ("Hats", Role::hats, peaked (hats(), 24, 9.0)), "eq", eqAt (4500.0, 5500.0, true)));
        v.push_back (want ("rumbly perc", Page::eq, rumble (track ("Perc", Role::perc, rumbly (perc(), -30.0))), "eq", lowCut (120.0)));
        v.push_back (want ("harsh perc", Page::eq, track ("Perc", Role::perc, peaked (perc(), 22, 9.0)), "eq", eqAt (3000.0, 3300.0, true)));
        v.push_back (want ("muddy bass", Page::eq, track ("Bass", Role::bass, bass808 (-14.0)), "eq", eqAt (240.0, 520.0, true), 1));
        v.push_back (want ("808 rumble", Page::eq, track ("808", Role::bass, sub (bass808())), "eq", lowCut (25.0), 1));
        v.push_back (want ("muddy keys", Page::eq, track ("Keys", Role::keys, keys (6.0, -10.0)), "eq", eqAt (240.0, 520.0, true)));
        v.push_back (want ("dull keys", Page::eq, track ("Keys", Role::keys, keys (14.0)), "eq", eqAt (9000.0, 11000.0, false)));
        v.push_back (want ("harsh synth", Page::eq, track ("Synth", Role::synth, peaked (synth(), 23, 8.0)), "eq", eqAt (3800.0, 4200.0, true)));
        v.push_back (want ("muddy synth", Page::eq, track ("Pad", Role::synth, plus (synth(), 315.0, -8.0, 0.6)), "eq", eqAt (240.0, 520.0, true)));
        v.push_back (want ("rumbly guitar", Page::eq, rumble (track ("Guitar", Role::guitar, rumbly (guitar(), -25.0))), "eq", lowCut (80.0)));
        v.push_back (want ("dull guitar (EQ alone)", Page::eq, track ("Guitar", Role::guitar, guitar (20.0)), "eq", eqAt (9000.0, 11000.0, false)));
        v.push_back (want ("dull vocal", Page::eq, track ("Lead Vox", Role::vox, vocal (9.0, 0.0)), "eq", eqAt (3000.0, 11000.0, false), 1));
        v.push_back (want ("harsh vocal", Page::eq, track ("Lead Vox", Role::vox, vocal (-2.0)), "eq", eqAt (2400.0, 5200.0, true), 1));
        v.push_back (want ("rumbly vocal", Page::eq, track ("Lead Vox", Role::vox, rumbly (vocal(), -20.0)), "eq", lowCut (90.0), 1));
        v.push_back (want ("rumbly ad-libs", Page::eq, track ("Adlibs", Role::adlibs, rumbly (vocal(), -20.0)), "eq", lowCut (100.0), 1));
        v.push_back (want ("muddy other", Page::eq, track ("Audio 07", Role::other, keys (6.0, -6.0)), "eq", eqAt (240.0, 520.0, true)));
        v.push_back (want ("muddy beat", Page::eq, track ("Beat", Role::beat, plus (beat (P), 315.0, -8.0, 0.6)), "eq", eqAt (240.0, 520.0, true)));
        v.push_back (want ("beat rumble", Page::eq, track ("Beat", Role::beat, sub (beat (P))), "eq", lowCut (25.0)));
        // ---- Dynamics
        v.push_back (want ("swinging vocal", Page::dynamics, swings (track ("Lead Vox", Role::vox, vocal()), 11.0), "comp", comp (3.0), 1));
        v.push_back (want ("swinging ad-libs", Page::dynamics, swings (track ("Adlibs", Role::adlibs, vocal()), 10.0), "comp", comp (3.0), 1));
        v.push_back (want ("swinging bass", Page::dynamics, swings (track ("808", Role::bass, bass808()), 9.0), "comp", comp (4.0), 1));
        v.push_back (want ("bass lows jump", Page::dynamics, with (track ("Bass", Role::bass, bass808()), [] (TrackReport& t) { t.lowRange = 12.0; t.levelRange = 6.0; }), "multi",
                           [] (const Change& c) { return value (c, proc::multiOn) == 1.0f && value (c, proc::multiLow) > 0.0f; }, 1));
        v.push_back (want ("swinging keys", Page::dynamics, swings (track ("Keys", Role::keys, keys()), 10.0), "comp", comp (2.5)));
        v.push_back (want ("swinging guitar", Page::dynamics, swings (track ("Guitar", Role::guitar, guitar()), 10.0), "comp", comp (2.5)));
        v.push_back (want ("swinging synth", Page::dynamics, swings (track ("Synth", Role::synth, synth()), 10.0), "comp", comp (2.5)));
        v.push_back (want ("swinging beat", Page::dynamics, swings (track ("Beat", Role::beat, beat (P)), 11.0), "comp", comp (1.5)));
        v.push_back (want ("squashed kick", Page::dynamics, crest (track ("Kick", Role::kick, kick()), 6.0), "trans", [] (const Change& c) { return value (c, proc::transAttack) > 0.0f; }, 1));
        v.push_back (want ("spiky kick", Page::dynamics, crest (track ("Kick", Role::kick, kick()), 22.0), "trans", [] (const Change& c) { return value (c, proc::transAttack) < 0.0f; }, 1));
        v.push_back (want ("squashed snare", Page::dynamics, crest (track ("Snare", Role::snare, snare()), 6.5), "trans", [] (const Change& c) { return value (c, proc::transAttack) > 0.0f; }, 1));
        v.push_back (want ("vocal hiss", Page::dynamics, floor (track ("Lead Vox", Role::vox, vocal())), "gate",
                           [] (const Change& c) { return value (c, proc::gateMode) == 1.0f && value (c, proc::gateThr) == -57.0f; }, 1));
        v.push_back (want ("snare bleed", Page::dynamics, floor (track ("Snare", Role::snare, snare())), "gate", [] (const Change& c) { return value (c, proc::gateMode) == 0.0f; }, 1));
        // ---- Space (by the style)
        v.push_back (want ("snare in pop", Page::space, track ("Snare", Role::snare, snare()), "space", space (proc::SpaceType::reverb), 1));
        v.push_back (want ("keys in pop", Page::space, track ("Keys", Role::keys, keys()), "space", space (proc::SpaceType::reverb)));
        {
            auto k = want ("guitar in rock", Page::space, track ("Guitar", Role::guitar, guitar()), "space", space (proc::SpaceType::room));
            k.style = "rock.classic"; k.styleName = "Rock - Classic";
            v.push_back (k);
        }
        {
            auto k = want ("synth in worship", Page::space, track ("Pad", Role::synth, synth()), "space", space (proc::SpaceType::hall));
            k.style = "worship.modern"; k.styleName = "Worship - Modern";
            v.push_back (k);
        }
        {
            auto k = want ("perc in rock", Page::space, track ("Perc", Role::perc, perc()), "space", space (proc::SpaceType::room));
            k.style = "rock.classic"; k.styleName = "Rock - Classic";
            v.push_back (k);
        }
        v.push_back (want ("ad-libs in pop", Page::space, track ("Adlibs", Role::adlibs, vocal()), "space", [] (const Change& c) { return value (c, proc::effectOn (proc::SpaceType::delay)) == 1.0f; }, 1));
        v.push_back (want ("lead vocal, no FX channel", Page::space, track ("Lead Vox", Role::vox, vocal()), "space", space (proc::SpaceType::reverb), 1));
        {
            auto k = want ("lead vocal with an FX channel", Page::space, track ("Lead Vox", Role::vox, vocal()), "space",
                           [] (const Change& c) { return c.name == "FX 1-Vox Verb" && value (c, proc::spRoute) == 1.0f && c.setDuck; }, 1);
            k.others = { "FX 1-Vox Verb" };
            v.push_back (k);
        }
        // ---- Tone (Tone Balance only)
        v.push_back (want ("dull guitar", Page::tone, track ("Guitar", Role::guitar, guitar (20.0)), "tone", tone (proc::tbAir, true)));
        v.push_back (want ("bright synth", Page::tone, track ("Synth", Role::synth, synth (0.0)), "tone", tone (proc::tbAir, false)));
        v.push_back (want ("boomy keys", Page::tone, track ("Keys", Role::keys, plus (keys(), 80.0, -6.0, 0.5)), "tone", tone (proc::tbWarmth, false)));
        v.push_back (want ("dull keys (Tone alone)", Page::tone, track ("Keys", Role::keys, keys (14.0)), "tone", tone (proc::tbAir, true)));
        v.push_back (want ("dull hats", Page::tone, track ("Hats", Role::hats, plus (plateau (-30.0, 1500.0, 2500.0, 24.0, 18.0), 2000.0, -24.0, 0.8)), "tone", tone (proc::tbAir, true)));
        v.push_back (want ("dull beat", Page::tone, track ("Beat", Role::beat, beat (P, -10.0)), "tone", tone (proc::tbAir, true)));
        v.push_back (want ("thin beat", Page::tone, track ("Beat", Role::beat, beat (P, 0.0, -8.0)), "tone", tone (proc::tbWarmth, true)));
        v.push_back (want ("dull vocal (Tone alone)", Page::tone, track ("Lead Vox", Role::vox, vocal (9.0, 0.0)), "tone", tone (proc::tbPresence, true), 1));
        v.push_back (want ("dull snare (Tone alone)", Page::tone, track ("Snare", Role::snare, snare (-14.0)), "tone", tone (proc::tbPresence, true), 1));
        v.push_back (want ("dull other", Page::tone, track ("Audio 07", Role::other, keys (14.0)), "tone", tone (proc::tbAir, true)));
        // ---- Stereo (never pans)
        v.push_back (want ("stereo kick lows", Page::stereo, with (track ("Kick", Role::kick, kick()), [] (TrackReport& t) { t.lowWidth = 0.35; }), "width", lows));
        v.push_back (want ("stereo 808 lows", Page::stereo, with (track ("808", Role::bass, bass808()), [] (TrackReport& t) { t.lowWidth = 0.35; }), "width", lows));
        v.push_back (want ("beat lows", Page::stereo, with (track ("Beat", Role::beat, beat (P)), [] (TrackReport& t) { t.lowWidth = 0.3; }), "width",
                           [] (const Change& c) { return value (c, proc::stLow) == 0.0f && value (c, proc::stLowX) == 120.0f; }));
        v.push_back (want ("narrow keys", Page::stereo, with (track ("Keys", Role::keys, keys()), [] (TrackReport& t) { t.width = 0.06; }), "width", width (115.0, 140.0)));
        v.push_back (want ("narrow backing vocals", Page::stereo, with (track ("Adlibs", Role::adlibs, vocal()), [] (TrackReport& t) { t.width = 0.05; }), "width", width (115.0, 140.0)));
        v.push_back (want ("too wide synth", Page::stereo, with (track ("Synth", Role::synth, synth()), [] (TrackReport& t) { t.width = 0.6; t.correlation = 0.05; }), "width", width (80.0, 90.0)));
        return v;
    }

    /** Every kind of track as it should be: AUTO finds nothing on any page. Space: a track that already has its own
        Space (or a kind that stays dry). */
    std::vector<Case> fitting()
    {
        const auto P = popStyle();
        std::vector<Case> v;
        auto fit = [&v] (TrackReport t, int channels, bool spaceOn) {
            for (auto page : kAllPages)
            {
                auto k = want (t.name + " fits", page, t, "", {}, channels);
                if (spaceOn) k.setup = [name = t.name] (std::vector<ChannelNow>& chans) {
                    for (auto& c : chans)
                        if (c.name == name) { c.proc[proc::spOn] = 1.0f; c.proc[static_cast<std::size_t> (proc::effectOn (proc::SpaceType::room))] = 1.0f; c.procAtAnalysis = c.proc; }
                };
                v.push_back (k);
            }
        };
        fit (track ("Kick", Role::kick, kick()), 1, false);
        fit (track ("Snare", Role::snare, snare()), 1, true);
        fit (track ("Hats", Role::hats, hats()), 2, false);
        fit (track ("Perc", Role::perc, perc()), 2, true);
        fit (track ("808", Role::bass, bass808()), 1, false);
        fit (track ("Keys", Role::keys, keys()), 2, true);
        fit (track ("Synth", Role::synth, synth()), 2, true);
        fit (track ("Guitar", Role::guitar, guitar()), 2, true);
        fit (track ("Lead Vox", Role::vox, vocal()), 1, true);
        fit (track ("Adlibs", Role::adlibs, vocal()), 1, true);
        fit (track ("Riser", Role::fx, synth()), 2, false);
        fit (track ("Audio 07", Role::other, keys()), 2, false);
        fit (with (track ("Beat", Role::beat, beat (P)), [] (TrackReport& t) { t.width = 0.2; t.lowWidth = 0.05; }), 2, false);
        return v;
    }

    Report reportFor (const Case& k) { return report ({ k.t }, k.style, k.styleName); }
}

TEST_CASE ("AUTO: every kind of track, every page: a clear problem gets the right move, a track that fits gets nothing", "[auto]")
{
    for (const auto& k : problems())
    {
        INFO (k.what << " on the " << pageName (k.page) << " page");
        const auto r = reportFor (k);
        const auto chans = channelsFor (k);
        const auto p = makeTrackPlan (k.page, k.t.hash, r, chans);
        INFO ("line: " << p.line);
        for (const auto& n : p.notes) INFO ("note: " << n);
        REQUIRE (p.valid);
        REQUIRE_FALSE (p.changes.empty());
        bool found = false;
        for (const auto& c : p.changes)
        {
            CHECK (c.page == pageKey (k.page));
            for (const auto& [id, v] : c.proc) CHECK (onPage (id, k.page));   // only this page's controls
            found = found || (c.kind == k.kind && k.check (c));
            CHECK (c.kind != "pan");
        }
        CHECK (found);
        CHECK (p.line.rfind ("AUTO: ", 0) == 0);
        CHECK (contains (p.line, "'" + k.t.name + "'"));
    }
    for (const auto& k : fitting())
    {
        INFO (k.what << " on the " << pageName (k.page) << " page");
        const auto p = makeTrackPlan (k.page, k.t.hash, reportFor (k), channelsFor (k));
        INFO ("line: " << p.line);
        for (const auto& c : p.changes) INFO ("change: " << c.kind << " " << c.toText << " (" << c.why << ")");
        CHECK (p.valid);
        CHECK (p.changes.empty());
        CHECK (contains (p.line, "othing to change"));
    }
}

TEST_CASE ("AUTO: pressing it again, or Auto Mix / Polish after it, finds nothing more on that page (and the other way round)", "[auto]")
{
    MixNow mix;
    mix.known = true;
    mix.now = mix.atAnalysis = proc::defaults();
    mix.atAnalysisKnown = true;
    for (const auto& k : problems())
    {
        INFO (k.what << " on the " << pageName (k.page) << " page");
        const auto r = reportFor (k);
        auto chans = channelsFor (k);
        const auto first = makeTrackPlan (k.page, k.t.hash, r, chans);
        REQUIRE_FALSE (first.changes.empty());
        applyPlan (chans, first);
        const auto again = makeTrackPlan (k.page, k.t.hash, r, chans);
        INFO ("again: " << again.line);
        CHECK (again.changes.empty());
        for (Tool tool : { Tool::polish, Tool::autoMix })
        {
            const auto all = makePlan (tool, r, chans, mix);
            for (const auto& c : all.changes)
            {
                INFO (toolName (tool) << " then: " << c.name << " " << c.kind << " " << c.toText << " (" << c.why << ")");
                CHECK_FALSE ((c.page == pageKey (k.page) && (c.hash == k.t.hash || (k.page == Page::space && c.role == Role::fx))));
            }
        }

        // Auto Mix first (everything it proposes applied), then AUTO on the page: nothing more.
        auto fresh = channelsFor (k);
        applyPlan (fresh, makePlan (Tool::autoMix, r, fresh, mix));
        const auto after = makeTrackPlan (k.page, k.t.hash, r, fresh);
        INFO ("AUTO after Auto Mix: " << after.line);
        CHECK (after.changes.empty());
    }
}

TEST_CASE ("AUTO: the pages count each other: EQ never adds air Tone Balance added, Tone counts the EQ's shelves, nothing is cut twice", "[auto]")
{
    // A dull guitar: Tone first, then EQ; and EQ first, then Tone. A dull vocal's presence, a boomy keys' low end the same.
    struct Pair { TrackReport t; int channels; };
    for (const auto& [t, channels] : { Pair { track ("Guitar", Role::guitar, guitar (20.0)), 2 }, Pair { track ("Keys", Role::keys, keys (14.0)), 2 },
                                       Pair { track ("Lead Vox", Role::vox, vocal (9.0, 0.0)), 1 }, Pair { track ("Keys", Role::keys, plus (keys(), 80.0, -6.0, 0.5)), 2 },
                                       Pair { track ("Snare", Role::snare, snare (-14.0)), 1 } })
    {
        INFO (t.name);
        const auto r = report ({ t });
        for (auto [a, b] : { std::pair { Page::tone, Page::eq }, std::pair { Page::eq, Page::tone } })
        {
            INFO (pageName (a) << " first, then " << pageName (b));
            std::vector<ChannelNow> chans { link (t.name, channels) };
            const auto first = makeTrackPlan (a, t.hash, r, chans);
            REQUIRE_FALSE (first.changes.empty());
            applyPlan (chans, first);
            const auto second = makeTrackPlan (b, t.hash, r, chans);
            INFO ("then: " << second.line);
            CHECK (second.changes.empty());
            CHECK (contains (second.line, "othing"));
        }
    }

    // Clean Up's mud dip (Assist EQ) since Analyze Mix listened: EQ AUTO cuts no mud on top.
    {
        const auto t = track ("Keys", Role::keys, keys (6.0, -10.0));
        std::vector<ChannelNow> chans { link ("Keys") };
        chans[0].assist.mudDb = -3.0f;
        chans[0].assist.mudHz = 315.0f;
        const auto p = makeTrackPlan (Page::eq, t.hash, report ({ t }), chans);
        CHECK (p.changes.empty());
    }
    // ... and EQ AUTO's mud cut first: Clean Up leaves that track's mud alone.
    {
        auto t = track ("Keys", Role::keys, keys (6.0, -10.0));
        auto r = report ({ t });
        r.mixBands.assign (static_cast<std::size_t> (kBands), 0.0);
        r.targetBands.assign (static_cast<std::size_t> (kBands), 0.0);
        for (int b = 10; b <= 14; ++b) r.mixBands[static_cast<std::size_t> (b)] = 4.0;   // the mix is muddy too
        std::vector<ChannelNow> chans { link ("Keys") };
        CHECK (change (makePlan (Tool::cleanUp, r, chans), "Keys", "mud") != nullptr);   // before: Clean Up dips it
        applyPlan (chans, makeTrackPlan (Page::eq, t.hash, r, chans));
        CHECK (change (makePlan (Tool::cleanUp, r, chans), "Keys", "mud") == nullptr);
        // A low cut on the EQ page: Clean Up adds no second one (Assist EQ).
        auto rumbleT = t;
        rumbleT.bands = rumbly (keys(), -25.0);
        rumbleT.rumble = true;
        std::vector<ChannelNow> c2 { link ("Keys") };
        const auto rr = report ({ rumbleT });
        CHECK (change (makePlan (Tool::cleanUp, rr, c2), "Keys", "lowcut") != nullptr);
        applyPlan (c2, makeTrackPlan (Page::eq, rumbleT.hash, rr, c2));
        CHECK (change (makePlan (Tool::cleanUp, rr, c2), "Keys", "lowcut") == nullptr);
    }
    // Vocal Pocket's dip (Assist EQ) where a harsh peak is: EQ AUTO's dip there is smaller (or none).
    {
        const auto t = track ("Synth", Role::synth, peaked (synth(), 23, 8.0));
        std::vector<ChannelNow> plain { link ("Synth") }, pocketed { link ("Synth") };
        pocketed[0].assist.pocketDb = -3.0f;
        pocketed[0].assist.pocketHz = 4000.0f;
        const auto* a = change (makeTrackPlan (Page::eq, t.hash, report ({ t }), plain), "Synth", "eq");
        const auto pb = makeTrackPlan (Page::eq, t.hash, report ({ t }), pocketed);
        REQUIRE (a != nullptr);
        const auto* b = change (pb, "Synth", "eq");
        CHECK ((b == nullptr || b->to > a->to));
    }
}

TEST_CASE ("AUTO: the line says what changed and why, in plain words", "[auto]")
{
    const auto keysT = track ("Keys", Role::keys, plus (keys (14.0), 315.0, -10.0, 0.6));
    const auto p = makeTrackPlan (Page::eq, keysT.hash, report ({ keysT }), { link ("Keys") });
    INFO (p.line);
    REQUIRE (p.changes.size() == 2);
    CHECK (p.line.rfind ("AUTO: cut mud at 315 Hz (-", 0) == 0);
    CHECK (contains (p.line, "opened the top (+"));
    CHECK (contains (p.line, ": 'Keys' sounded muddy and dull for Pop - Modern"));

    // Nothing to change: says so, and why when there is a reason.
    const auto kickT = track ("Kick", Role::kick, kick());
    CHECK (makeTrackPlan (Page::eq, kickT.hash, report ({ kickT }), { link ("Kick", 1) }).line == "'Kick' already fits: nothing to change.");
    const auto space = makeTrackPlan (Page::space, kickT.hash, report ({ kickT }), { link ("Kick", 1) });
    CHECK (contains (space.line, "'Kick': a kick stays dry"));
    CHECK (contains (space.line, "Nothing to change."));
    const auto mono = makeTrackPlan (Page::stereo, kickT.hash, report ({ kickT }), { link ("Kick", 1) });
    CHECK (contains (mono.line, "is mono"));
    CHECK (contains (mono.line, "Stereo Architect"));

    // The lead vocal's reverb on the FX channel: the line names the other channel it set.
    const auto lead = track ("Lead Vox", Role::vox, vocal());
    const auto fx = makeTrackPlan (Page::space, lead.hash, report ({ lead }), { link ("Lead Vox", 1), link ("FX 1-Vox Verb") });
    INFO (fx.line);
    CHECK (contains (fx.line, "on 'FX 1-Vox Verb' (the FX channel)"));
    CHECK (contains (fx.line, "{daw:sendHow}"));
    // ... and AUTO on that FX channel's own Space page does the same (it needn't have been heard: it plays what goes in).
    const auto onFx = makeTrackPlan (Page::space, bridge::nameHash ("FX 1-Vox Verb"), report ({ lead }), { link ("Lead Vox", 1), link ("FX 1-Vox Verb") });
    REQUIRE (onFx.changes.size() == 1);
    CHECK (onFx.changes[0].name == "FX 1-Vox Verb");
    CHECK_FALSE (autoListensFirst (Page::space, bridge::nameHash ("FX 1-Vox Verb"), report ({ lead }), { link ("Lead Vox", 1), link ("FX 1-Vox Verb") }));
    CHECK (autoListensFirst (Page::eq, bridge::nameHash ("FX 1-Vox Verb"), report ({ lead }), { link ("Lead Vox", 1), link ("FX 1-Vox Verb") }));
}

TEST_CASE ("AUTO: the Full Mix is refused, and a track Analyze Mix didn't hear needs listening", "[auto]")
{
    const auto t = track ("Keys", Role::keys, keys (6.0, -10.0));
    const auto r = report ({ t });
    for (auto page : kAllPages)
    {
        const auto p = makeTrackPlan (page, 0, r, { link ("Keys") });
        CHECK_FALSE (p.valid);
        CHECK (p.changes.empty());
        CHECK (p.line == "Pick a track: AUTO works on one track at a time (Auto Mix on the Overview does the whole mix).");
    }
    CHECK_FALSE (makeTrackPlan (Page::eq, t.hash, Report {}, { link ("Keys") }).valid);   // no Analyze Mix
    auto locked = std::vector<ChannelNow> { link ("Keys") };
    locked[0].locked = true;
    const auto lp = makeTrackPlan (Page::eq, t.hash, r, locked);
    CHECK (lp.changes.empty());
    CHECK (contains (lp.line, "protected"));

    // Heard 2 s only (a chorus while Analyze Mix heard the verse): AUTO listens first.
    auto brief = t;
    brief.activeSeconds = 2.0;
    brief.activity = 0.03;
    CHECK_FALSE (heardEnough (report ({ brief }), t.hash));
    CHECK (autoListensFirst (Page::eq, t.hash, report ({ brief }), { link ("Keys") }));
    CHECK (heardEnough (r, t.hash));
    CHECK_FALSE (autoListensFirst (Page::eq, t.hash, r, { link ("Keys") }));
    CHECK_FALSE (heardEnough (r, bridge::nameHash ("Chorus Keys")));
}

TEST_CASE ("AUTO: Voxology's EQ, compressor, reverb and delay on a vocal are left alone, and the line says why", "[auto][suite]")
{
    auto lead = track ("Lead Vox", Role::vox, vocal (9.0, 0.0));
    lead.levelRange = 11.0;
    lead.floorMoments = 40; lead.floorRms = -72.0; lead.floorPeak = -63.0; lead.floorSpread = 2.0; lead.activeRms = -22.0;
    const auto adlibs = track ("Adlibs", Role::adlibs, vocal());
    std::vector<ChannelNow> chans { link ("Lead Vox", 1), link ("Adlibs", 1), link ("FX 1-Vox Verb") };
    auto& v = chans[0].voxology;
    v.on = v.eq = v.compressor = v.reverb = true;
    v.compressionDb = 4.0;
    chans[1].voxology.on = chans[1].voxology.delay = true;
    const auto r = report ({ lead, adlibs });
    const auto eq = makeTrackPlan (Page::eq, lead.hash, r, chans);
    CHECK (eq.changes.empty());
    CHECK (contains (eq.line, "Voxology's Tone EQ"));
    const auto tone = makeTrackPlan (Page::tone, lead.hash, r, chans);
    CHECK (tone.changes.empty());
    CHECK (contains (tone.line, "Voxology"));
    const auto dyn = makeTrackPlan (Page::dynamics, lead.hash, r, chans);
    CHECK (dyn.changes.empty());   // no compressor, and no expander either (Voxology cleans its vocal)
    CHECK (contains (dyn.line, "Voxology compresses it already"));
    const auto space = makeTrackPlan (Page::space, lead.hash, r, chans);
    CHECK (space.changes.empty());
    CHECK (contains (space.line, "Voxology's reverb"));
    const auto ad = makeTrackPlan (Page::space, adlibs.hash, r, chans);
    CHECK (ad.changes.empty());
    CHECK (contains (ad.line, "Voxology's delay"));
}

TEST_CASE ("AUTO: Tone touches only Tone Balance, Dynamics never the Limiter, Stereo never pans", "[auto]")
{
    // Every change from every page on every case above, plus Polish and Auto Mix on all of them at once.
    std::vector<TrackReport> all;
    std::vector<ChannelNow> chans;
    for (const auto& k : problems())
    {
        const auto p = makeTrackPlan (k.page, k.t.hash, reportFor (k), channelsFor (k));
        for (const auto& c : p.changes)
            for (const auto& [id, value] : c.proc)
            {
                if (k.page == Page::tone) CHECK ((id == proc::tbOn || id == proc::tbWarmth || id == proc::tbPresence || id == proc::tbAir || id == proc::tbGlue || id == proc::tbPunch));
                CHECK_FALSE (proc::specs()[static_cast<std::size_t> (id)].id == std::string ("lim_on"));
                CHECK (std::string (proc::specs()[static_cast<std::size_t> (id)].id).rfind ("lim_", 0) != 0);
            }
        for (const auto& c : p.changes) CHECK (c.kind != "pan");
    }
}

TEST_CASE ("Polish and Auto Mix use the same rules: every kind of track, each fix on the page that suits it", "[auto][polish]")
{
    const auto guitarT = track ("Guitar", Role::guitar, guitar (20.0));
    const auto keysT = track ("Keys", Role::keys, plus (keys (14.0), 315.0, -10.0, 0.6));
    auto bassT = track ("808", Role::bass, bass808());
    bassT.levelRange = 9.0;
    const auto p = makePlan (Tool::polish, report ({ guitarT, keysT, bassT }), { link ("Guitar"), link ("Keys"), link ("808", 1) });
    // The guitar's dull top on Tone Balance (it suits a broad tilt), the keys' on its EQ (with its mud dip), never both.
    REQUIRE (change (p, "Guitar", "tone") != nullptr);
    CHECK (change (p, "Guitar", "eq") == nullptr);
    CHECK (change (p, "Keys", "eq") != nullptr);
    CHECK (change (p, "Keys", "tone") == nullptr);
    CHECK (change (p, "808", "comp") != nullptr);
    CHECK (change (p, "Guitar", "space") != nullptr);   // pop: a short plate on guitars and keys
    CHECK (contains (p.summary, "Tone Balance: 1"));
}

// =================================================================================================
// The beat: a whole instrumental on one track

namespace
{
    AnalysisFrame frameOf (const std::vector<double>& bands, double crest, double width = 0.1)
    {
        AnalysisFrame f;
        f.silent = false;
        double e = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const auto i = static_cast<std::size_t> (b);
            f.bandDb[i] = static_cast<float> (bands[i]);
            f.sideShare[i] = static_cast<float> (width);
            e += std::pow (10.0, bands[i] / 10.0);
        }
        f.rmsDb = static_cast<float> (10.0 * std::log10 (e) - 3.01);
        f.loudness = static_cast<float> (10.0 * std::log10 (e) - 0.69);
        f.peakDb = static_cast<float> (f.rmsDb + crest);
        f.width = static_cast<float> (width);
        f.correlation = static_cast<float> (1.0 - 2.0 * width);
        return f;
    }

    TrackSample sampleOf (const std::string& name, const AnalysisFrame& f)
    {
        TrackSample t;
        t.name = name;
        t.hash = bridge::nameHash (name);
        t.faderKnown = true;
        t.frame = f;
        return t;
    }

    AnalysisFrame mixOf (const std::vector<TrackSample>& tracks)
    {
        std::vector<double> e (static_cast<std::size_t> (kBands), 0.0);
        bool any = false;
        for (const auto& t : tracks)
        {
            if (t.frame.silent) continue;
            any = true;
            for (int b = 0; b < kBands; ++b) e[static_cast<std::size_t> (b)] += std::pow (10.0, t.frame.bandDb[static_cast<std::size_t> (b)] / 10.0);
        }
        if (! any) return AnalysisFrame {};
        std::vector<double> db (e.size());
        for (std::size_t i = 0; i < e.size(); ++i) db[i] = e[i] > 0.0 ? 10.0 * std::log10 (e[i]) : -120.0;
        auto f = frameOf (db, 10.0, 0.1);
        f.peakDb = -6.0f;
        return f;
    }

    Report listen (const std::function<std::vector<TrackSample> (int)>& at, int moments, const std::map<std::uint32_t, TrackSettings>& settings = {})
    {
        Analysis a;
        a.begin (popStyle());
        for (int i = 0; i < moments; ++i)
        {
            Moment m;
            m.tracks = at (i);
            m.mix = mixOf (m.tracks);
            a.add (m);
        }
        return a.evaluate (settings);
    }
}

TEST_CASE ("Beat: found by its name, not in a beatbox, a beat vocal or Logic's 'Inst 1'", "[auto][beat]")
{
    CHECK (roleFromName ("Beat") == Role::beat);
    CHECK (roleFromName ("Trap Beat") == Role::beat);
    CHECK (roleFromName ("Instrumental") == Role::beat);
    CHECK (roleFromName ("Song Inst") == Role::beat);
    CHECK (roleFromName ("2-Track") == Role::beat);
    CHECK (roleFromName ("2trk") == Role::beat);
    CHECK (roleFromName ("Riddim") == Role::beat);
    CHECK (roleFromName ("Backing Track") == Role::beat);
    CHECK (roleFromName ("Prod. by Metro") == Role::beat);
    CHECK_FALSE (roleFromName ("Beatbox").has_value());
    CHECK_FALSE (roleFromName ("Beat Box").has_value());
    CHECK (roleFromName ("Beat Vox") == Role::vox);
    CHECK_FALSE (roleFromName ("Inst 1").has_value());
    CHECK (roleFromName ("Beat Kick") == Role::kick);
    CHECK (roleFromName ("Drum Beat") == Role::perc);
    CHECK (roleFromName ("Beat Verb") == Role::fx);
    CHECK (roleFromName ("Backing Vocals") == Role::adlibs);
}

TEST_CASE ("Beat: found by its sound (strong lows, mids and highs at once, most of the song), not a drum kit or a pad", "[auto][beat]")
{
    const auto st = popStyle();
    const auto whole = beat (st);
    const auto drums = sum ({ kick(), snare(), hats() });
    const auto r = listen ([&] (int) {
        return std::vector<TrackSample> { sampleOf ("Audio 09", frameOf (whole, 10.0, 0.15)), sampleOf ("Audio 10", frameOf (drums, 16.0, 0.1)),
                                          sampleOf ("Audio 11", frameOf (synth(), 5.0, 0.35)), sampleOf ("Audio 12", frameOf (vocal(), 10.0, 0.02)) };
    }, 150);
    const auto* b = trackIn (r, bridge::nameHash ("Audio 09"));
    REQUIRE (b != nullptr);
    CHECK (b->role == Role::beat);
    CHECK (b->roleFrom == "sound");
    CHECK (trackIn (r, bridge::nameHash ("Audio 10"))->role != Role::beat);
    CHECK (trackIn (r, bridge::nameHash ("Audio 11"))->role != Role::beat);
    CHECK (trackIn (r, bridge::nameHash ("Audio 12"))->role != Role::beat);
    // Its low end is the music: never "rumble" (Clean Up never cuts it), and Vocal Pocket can dip it.
    CHECK_FALSE (b->rumble);
    // A beat that plays only now and then (a few hits) isn't one.
    const auto now = listen ([&] (int i) {
        return std::vector<TrackSample> { sampleOf ("Audio 09", i % 5 == 0 ? frameOf (whole, 10.0, 0.15) : AnalysisFrame {}), sampleOf ("Audio 12", frameOf (vocal(), 10.0, 0.02)) };
    }, 150);
    CHECK (trackIn (now, bridge::nameHash ("Audio 09"))->role != Role::beat);
}

TEST_CASE ("Beat: saved roles keep loading (by key), the new role at the end", "[auto][beat]")
{
    const char* old[] = { "kick", "snare", "hats", "perc", "bass", "keys", "synth", "guitar", "vox", "adlibs", "fx", "other" };
    for (int i = 0; i < 12; ++i)
    {
        CHECK (std::string (roleKey (static_cast<Role> (i))) == old[i]);
        CHECK (roleFromKey (old[i]) == static_cast<Role> (i));
    }
    CHECK (kRoleCount == 13);
    CHECK (std::string (roleKey (Role::beat)) == "beat");
    CHECK (roleFromKey ("beat") == Role::beat);
    CHECK (groupOf (Role::beat) == Group::instruments);
    CHECK (viaMusicBus (Role::beat));
}

TEST_CASE ("Beat: the music against the vocal in Balance, centred and never paired in Stereo Architect", "[auto][beat]")
{
    auto vox = track ("Lead Vox", Role::vox, vocal());
    vox.loudness = -24.0;
    auto beatT = track ("Beat", Role::beat, beat (popStyle()));
    beatT.loudness = -12.0;
    auto extra = track ("808 Extra", Role::bass, bass808());
    extra.loudness = -30.0;   // far under where a bass usually sits against a kick: Balance would raise it, if it judged it
    auto r = report ({ vox, beatT, extra });
    r.hasVocalRatio = true;
    r.vocalRatio = -12.0;
    r.vocalTarget = 1.5;
    r.mixPeak = -12.0;
    std::vector<ChannelNow> chans { link ("Lead Vox", 1), link ("Beat"), link ("808 Extra", 1) };
    const auto p = makePlan (Tool::balance, r, chans);
    for (const auto& c : p.changes) CHECK_FALSE (contains (c.why, "against the rest of the band"));
    CHECK (std::any_of (p.notes.begin(), p.notes.end(), [] (const std::string& n) { return contains (n, "'Beat' is a whole beat on one track"); }));
    const auto* v = change (p, "Lead Vox", "level");
    REQUIRE (v != nullptr);
    CHECK (v->to > v->from);   // the vocal comes up against the music

    // A mono beat panned off-centre goes to the middle; two of them are never spread as a pair.
    auto b1 = track ("Beat 1", Role::beat, beat (popStyle()));
    auto b2 = track ("Beat 2", Role::beat, beat (popStyle()));
    auto sr = report ({ b1, b2 });
    std::vector<ChannelNow> pans { link ("Beat 1", 1), link ("Beat 2", 1) };
    for (auto& c : pans) { c.hasPan = true; }
    pans[0].pan = -40.0;
    pans[1].pan = 0.0;
    const auto st = makePlan (Tool::stereo, sr, pans);
    const auto* moved = change (st, "Beat 1", "pan");
    REQUIRE (moved != nullptr);
    CHECK (moved->to == 0.0);
    CHECK (change (st, "Beat 2", "pan") == nullptr);
}

// =================================================================================================
// Listening to one track: AUTO when Analyze Mix didn't hear it

TEST_CASE ("AUTO's listening: a track Analyze Mix didn't hear is merged in, everything else stays as it was", "[auto][listen]")
{
    // Analyze Mix heard the verse: 'Keys 2' (the chorus keys) silent the whole time.
    const auto verse = listen ([] (int) {
        return std::vector<TrackSample> { sampleOf ("Lead Vox", frameOf (vocal(), 10.0, 0.02)), sampleOf ("Keys 1", frameOf (keys(), 6.0, 0.15)),
                                          sampleOf ("Keys 2", AnalysisFrame {}), sampleOf ("Vox 1", frameOf (vocal(), 10.0, 0.02)),
                                          sampleOf ("Vox 2", frameOf (vocal(), 10.0, 0.02)) };
    }, 150);
    const auto hash = bridge::nameHash ("Keys 2");
    REQUIRE (verse.valid);
    CHECK_FALSE (heardEnough (verse, hash));
    CHECK (autoListensFirst (Page::eq, hash, verse, { link ("Keys 2") }));

    // AUTO listened while the chorus played (9 s of it, with the vocal): the chorus keys are muddy.
    const auto chorus = listen ([] (int) {
        return std::vector<TrackSample> { sampleOf ("Lead Vox", frameOf (vocal(), 10.0, 0.02)), sampleOf ("Keys 1", frameOf (keys(), 6.0, 0.15)),
                                          sampleOf ("Keys 2", frameOf (keys (6.0, -10.0), 6.0, 0.15)) };
    }, 45);
    REQUIRE (heardEnough (chorus, hash));
    const auto merged = mergeTrack (verse, chorus, hash);
    REQUIRE (heardEnough (merged, hash));
    const auto* got = trackIn (merged, hash);
    const auto* fresh = trackIn (chorus, hash);
    REQUIRE (got != nullptr);
    CHECK (got->loudness == Catch::Approx (fresh->loudness).margin (0.01));
    for (int b = 0; b < kBands; ++b) CHECK (got->bands[static_cast<std::size_t> (b)] == Catch::Approx (fresh->bands[static_cast<std::size_t> (b)]).margin (0.01));
    CHECK (got->role == Role::keys);
    // Everything else as Analyze Mix heard it: the other tracks, the advice, the style, the vocal level.
    CHECK (merged.tracks.size() == verse.tracks.size());
    for (const auto& t : verse.tracks)
    {
        if (t.hash == hash) continue;
        const auto* m = trackIn (merged, t.hash);
        REQUIRE (m != nullptr);
        CHECK (m->loudness == t.loudness);
        CHECK (m->bands == t.bands);
        CHECK (m->maskDb == t.maskDb);
    }
    CHECK (merged.suggestions.size() == verse.suggestions.size());
    CHECK (merged.vocalRatio == verse.vocalRatio);
    CHECK (merged.style.key == verse.style.key);
    // Its name pair comes from the new listen; another pair stays as it was.
    const auto* keysPair = pairCheck (merged, bridge::nameHash ("Keys 1"), hash);
    REQUIRE (keysPair != nullptr);
    CHECK (keysPair->activeB + keysPair->activeA == pairCheck (chorus, bridge::nameHash ("Keys 1"), hash)->activeA + pairCheck (chorus, bridge::nameHash ("Keys 1"), hash)->activeB);
    REQUIRE (pairCheck (merged, bridge::nameHash ("Vox 1"), bridge::nameHash ("Vox 2")) != nullptr);
    CHECK (pairCheck (merged, bridge::nameHash ("Vox 1"), bridge::nameHash ("Vox 2"))->both == pairCheck (verse, bridge::nameHash ("Vox 1"), bridge::nameHash ("Vox 2"))->both);
    // ... and AUTO works from it.
    const auto p = makeTrackPlan (Page::eq, hash, merged, { link ("Keys 2") });
    CHECK (change (p, "Keys 2", "eq") != nullptr);

    // A track the merge doesn't know: nothing changes. The way to the master channel the full listen found (a group
    // fader at -6): the merged track's levels get it too.
    CHECK (mergeTrack (verse, chorus, bridge::nameHash ("Nobody")).tracks.size() == verse.tracks.size());
    auto routed = verse;
    routed.routingFromSound = true;
    routed.musicPathDb = -6.0;
    const auto m2 = mergeTrack (routed, chorus, hash);
    CHECK (trackIn (m2, hash)->loudness == Catch::Approx (fresh->loudness - 6.0).margin (0.01));
}

TEST_CASE ("Analysis: the measures AUTO uses (low-end swing, correlation, a noise floor between phrases)", "[auto][analysis]")
{
    // A vocal with hiss between its phrases (-70 dB rms), and a bass whose lows jump.
    const auto r = listen ([] (int i) {
        const bool sings = (i / 10) % 2 == 0;
        auto hiss = frameOf (plateau (-95.0, 1000.0, 12000.0, 6.0, 6.0), 9.0, 0.0);
        auto bassLow = frameOf (bass808(), 6.0, 0.0), bassHigh = frameOf (plus (bass808(), 50.0, 2.0, 0.8), 6.0, 0.0);
        return std::vector<TrackSample> { sampleOf ("Lead Vox", sings ? frameOf (vocal(), 10.0, 0.02) : hiss), sampleOf ("Bass", (i / 5) % 2 ? bassLow : bassHigh) };
    }, 150);
    const auto* v = trackIn (r, bridge::nameHash ("Lead Vox"));
    REQUIRE (v != nullptr);
    CHECK (v->floorMoments >= 60);
    CHECK (v->floorSpread < 1.0);
    CHECK (v->activeRms - v->floorRms > 30.0);
    CHECK (v->correlation == Catch::Approx (0.96).margin (0.01));
    CHECK (v->activeSeconds == Catch::Approx (16.0).margin (0.1));   // 8 phrases of 2 s
    const auto* b = trackIn (r, bridge::nameHash ("Bass"));
    REQUIRE (b != nullptr);
    CHECK (b->lowRange > 6.0);
    CHECK (b->floorMoments == 0);   // never quiet
}

TEST_CASE ("Beat: a track where Voxology runs in Beat mode is the beat (your own role still wins)", "[auto][beat][suite]")
{
    const auto r0 = listen ([] (int) {
        return std::vector<TrackSample> { sampleOf ("Audio 03", frameOf (synth(), 5.0, 0.3)), sampleOf ("Lead Vox", frameOf (vocal(), 10.0, 0.02)) };
    }, 60);
    const auto hash = bridge::nameHash ("Audio 03");
    std::map<std::uint32_t, TrackSettings> settings;
    settings[hash].fromVoxology = Role::beat;   // what the hub gives a channel whose Voxology runs in Beat mode
    Analysis a;
    a.begin (popStyle());
    for (int i = 0; i < 60; ++i)
    {
        Moment m;
        m.tracks = { sampleOf ("Audio 03", frameOf (synth(), 5.0, 0.3)), sampleOf ("Lead Vox", frameOf (vocal(), 10.0, 0.02)) };
        m.mix = mixOf (m.tracks);
        a.add (m);
    }
    CHECK (trackIn (r0, hash)->role != Role::beat);
    const auto viaVoxology = a.evaluate (settings);
    CHECK (trackIn (viaVoxology, hash)->role == Role::beat);
    CHECK (trackIn (viaVoxology, hash)->roleFrom == "voxology");
    settings[hash].role = Role::keys;   // you set it on the Tracks page
    const auto yours = a.evaluate (settings);
    CHECK (trackIn (yours, hash)->role == Role::keys);
    CHECK (trackIn (yours, hash)->roleFrom == "you");
}
