// Milestone 5, round 3: Polish (and Auto Mix) propose processing in the tracks' Mixology Links.

#include "mixology/BridgeProtocol.h"
#include "mixology/MixAnalysis.h"
#include "mixology/MixPlanner.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace mixology;
using namespace mixology::mix;

namespace
{
    /** A vocal's long-term spectrum: level from 200 Hz to 630 Hz, falling `slope` dB per octave above, steeply below. */
    std::vector<double> vocalBands (double level, double slope, double airExtra = 0.0)
    {
        std::vector<double> b (static_cast<std::size_t> (kBands));
        for (int i = 0; i < kBands; ++i)
        {
            const double hz = bandHz (i);
            double v = level;
            if (hz > 630.0) v -= slope * std::log2 (hz / 630.0);
            if (hz < 160.0) v -= 18.0 * std::log2 (160.0 / hz);
            if (hz > 7000.0) v += airExtra;
            b[static_cast<std::size_t> (i)] = v;
        }
        return b;
    }

    std::vector<double> kickBands (double boxLift = 0.0)
    {
        std::vector<double> b (static_cast<std::size_t> (kBands));
        for (int i = 0; i < kBands; ++i)
        {
            const double hz = bandHz (i);
            double v = -10.0 - 12.0 * std::pow (std::log2 (hz / 60.0), 2.0) / 4.0;   // weight around 60 Hz
            if (hz >= 300.0 && hz <= 520.0) v = std::max (v, -24.0 + boxLift);
            b[static_cast<std::size_t> (i)] = std::max (v, -80.0);
        }
        return b;
    }

    TrackReport track (const std::string& name, Role role, std::vector<double> bands)
    {
        TrackReport t;
        t.hash = bridge::nameHash (name);
        t.name = name;
        t.role = role;
        t.activity = 0.8;
        t.loudness = -16.0;
        t.loudPeak = -6.0;
        t.levelRange = 4.0;
        t.bands = std::move (bands);
        return t;
    }

    ChannelNow link (const std::string& name, int channels = 1)
    {
        ChannelNow c;
        c.hash = bridge::nameHash (name);
        c.name = name;
        c.faderKnown = true;
        c.hasLink = true;
        c.channels = channels;
        c.hasProc = true;
        c.proc = proc::defaults();
        return c;
    }

    Report report (std::vector<TrackReport> tracks, const std::string& style = "hiphop.modern")
    {
        Report r;
        r.valid = true;
        r.style.key = style;
        r.tracks = std::move (tracks);
        return r;
    }

    const Change* change (const Plan& p, const std::string& name, const std::string& kind)
    {
        for (const auto& c : p.changes)
            if (c.name == name && c.kind == kind) return &c;
        return nullptr;
    }

    float value (const Change& c, int id)
    {
        for (const auto& [i, v] : c.proc)
            if (i == id) return v;
        return -1.0e9f;
    }

    bool noted (const Plan& p, const std::string& text)
    {
        return std::any_of (p.notes.begin(), p.notes.end(), [&] (const std::string& n) { return n.find (text) != std::string::npos; });
    }

    /** What the Link has after applying these changes (and what Analyze Mix would have seen then). */
    void apply (std::vector<ChannelNow>& chans, const Plan& p)
    {
        for (const auto& c : p.changes)
            for (auto& ch : chans)
                if (ch.hash == c.hash)
                    for (const auto& [id, v] : c.proc) ch.proc[static_cast<std::size_t> (id)] = v;
    }
}

TEST_CASE ("Polish: a dull lead vocal gets presence and air; a clear one is left alone", "[polish]")
{
    auto chans = std::vector<ChannelNow> { link ("Lead Vox") };
    const auto dull = makePlan (Tool::polish, report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, 9.0)) }), chans);
    const auto* pres = change (dull, "Lead Vox", "eq");
    REQUIRE (pres != nullptr);
    CHECK (pres->to >= 1.0);
    CHECK (pres->to <= 3.0);
    CHECK (pres->hz == Catch::Approx (3500.0));
    CHECK (value (*pres, proc::eqOn) == 1.0f);
    CHECK (pres->fromText == "Flat");
    CHECK (pres->why.find ("dull") != std::string::npos);
    const auto air = std::count_if (dull.changes.begin(), dull.changes.end(), [] (const Change& c) { return c.kind == "eq" && c.hz == 10000.0; });
    CHECK (air == 1);

    const auto clear = makePlan (Tool::polish, report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, 5.0, 2.0)) }), chans);
    CHECK (change (clear, "Lead Vox", "eq") == nullptr);
    CHECK (noted (clear, "already sounds balanced"));

    const auto harsh = makePlan (Tool::polish, report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, -2.0)) }), chans);
    const auto* dip = change (harsh, "Lead Vox", "eq");
    REQUIRE (dip != nullptr);
    CHECK (dip->to <= -1.0);
    CHECK (dip->why.find ("harsh") != std::string::npos);

    // No Link processing reachable (an older Link): nothing; protected: a note.
    auto old = chans;
    old[0].hasProc = false;
    CHECK (change (makePlan (Tool::polish, report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, 9.0)) }), old), "Lead Vox", "eq") == nullptr);
    auto locked = chans;
    locked[0].locked = true;
    const auto lp = makePlan (Tool::polish, report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, 9.0)) }), locked);
    CHECK (change (lp, "Lead Vox", "eq") == nullptr);
    CHECK (noted (lp, "protected"));
}

TEST_CASE ("Polish: applying and opening it again proposes nothing more", "[polish]")
{
    auto chans = std::vector<ChannelNow> { link ("Lead Vox") };
    const auto r = report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, 9.0)) });
    const auto first = makePlan (Tool::polish, r, chans);
    REQUIRE_FALSE (first.changes.empty());
    // Applied, with the same analysis (the Link's settings at analysis time are known: flat).
    for (auto& c : chans) { c.procAtAnalysis = c.proc; c.procAtAnalysisKnown = true; }
    apply (chans, first);
    const auto again = makePlan (Tool::polish, r, chans);
    CHECK (change (again, "Lead Vox", "eq") == nullptr);

    // A band of yours already doing that job is adjusted, not doubled: +1 dB at 3.4 kHz at analysis -> one band, total.
    auto mine = std::vector<ChannelNow> { link ("Lead Vox") };
    mine[0].proc[proc::eqOn] = 1.0f;
    mine[0].proc[static_cast<std::size_t> (proc::eqFreq (2))] = 3400.0f;
    mine[0].proc[static_cast<std::size_t> (proc::eqGain (2))] = 1.0f;
    mine[0].procAtAnalysis = mine[0].proc;
    mine[0].procAtAnalysisKnown = true;
    const auto adj = makePlan (Tool::polish, r, mine);
    const auto* c = change (adj, "Lead Vox", "eq");
    REQUIRE (c != nullptr);
    CHECK (value (*c, proc::eqGain (2)) > 1.5f);   // its own band, more
    CHECK (value (*c, proc::eqFreq (2)) == Catch::Approx (3400.0f));
    CHECK (c->fromText.find ("+1.0 dB") != std::string::npos);
}

TEST_CASE ("Polish: a compressor where a vocal or the bass swings, not where it is even or has one", "[polish]")
{
    auto swingy = track ("Lead Vox", Role::vox, vocalBands (-14.0, 5.0, 2.0));
    swingy.levelRange = 11.0;
    swingy.loudPeak = -4.0;
    auto bass = track ("808", Role::bass, kickBands());
    bass.levelRange = 9.0;
    bass.loudPeak = -3.0;
    auto even = track ("Adlibs", Role::adlibs, vocalBands (-20.0, 5.0, 2.0));
    even.levelRange = 5.0;
    auto chans = std::vector<ChannelNow> { link ("Lead Vox"), link ("808"), link ("Adlibs") };
    const auto p = makePlan (Tool::polish, report ({ swingy, bass, even }), chans);
    const auto* vc = change (p, "Lead Vox", "comp");
    REQUIRE (vc != nullptr);
    CHECK (value (*vc, proc::compOn) == 1.0f);
    CHECK (value (*vc, proc::compThr) == Catch::Approx (-10.0f));   // its loud peaks minus 6 dB
    CHECK (value (*vc, proc::compRatio) == Catch::Approx (3.0f));
    CHECK (vc->why.find ("11 dB") != std::string::npos);
    const auto* bc = change (p, "808", "comp");
    REQUIRE (bc != nullptr);
    CHECK (value (*bc, proc::compRatio) == Catch::Approx (4.0f));
    CHECK (change (p, "Adlibs", "comp") == nullptr);

    chans[0].proc[proc::compOn] = 1.0f;   // your own compressor
    const auto p2 = makePlan (Tool::polish, report ({ swingy, bass, even }), chans);
    CHECK (change (p2, "Lead Vox", "comp") == nullptr);
    CHECK (noted (p2, "has a compressor on already"));
}

TEST_CASE ("Polish: the lead vocal's reverb on an FX channel (ducking under it), else a little on the vocal", "[polish]")
{
    const auto lead = track ("Lead Vox", Role::vox, vocalBands (-14.0, 5.0, 2.0));
    const auto dbl = track ("Lead Vox (D)", Role::vox, vocalBands (-20.0, 5.0, 2.0));
    const auto adlibs = track ("Adlibs", Role::adlibs, vocalBands (-20.0, 5.0, 2.0));

    // An FX channel with a Link.
    auto chans = std::vector<ChannelNow> { link ("Lead Vox"), link ("Lead Vox (D)"), link ("Adlibs"), link ("FX 1-Vox Verb", 2) };
    const auto p = makePlan (Tool::polish, report ({ lead, dbl, adlibs }), chans);
    const auto* fx = change (p, "FX 1-Vox Verb", "space");
    REQUIRE (fx != nullptr);
    CHECK (value (*fx, proc::spOn) == 1.0f);
    CHECK (value (*fx, proc::spRoute) == 1.0f);
    CHECK (value (*fx, proc::spType) == static_cast<float> (proc::SpaceType::reverb));
    CHECK (value (*fx, proc::revDecay) == Catch::Approx (1.4f));   // hip hop: a short plate
    CHECK (fx->setDuck);
    REQUIRE (fx->duck.size() == 1);
    CHECK (fx->duck[0] == bridge::nameHash ("Lead Vox"));
    CHECK (noted (p, "Send 'Lead Vox' to 'FX 1-Vox Verb'"));
    CHECK (noted (p, "If another plug-in"));   // nothing tells Mixology what else is on the vocal
    CHECK (change (p, "Lead Vox", "space") == nullptr);
    const auto* ad = change (p, "Adlibs", "space");
    REQUIRE (ad != nullptr);
    CHECK (value (*ad, proc::spType) == static_cast<float> (proc::SpaceType::delay));
    CHECK (value (*ad, proc::dlyMix) == Catch::Approx (15.0f));

    // Without an FX channel: a little plate on the vocal itself. Gospel: a hall.
    chans.pop_back();
    const auto p2 = makePlan (Tool::polish, report ({ lead, dbl, adlibs }, "gospel.contemporary"), chans);
    const auto* on = change (p2, "Lead Vox", "space");
    REQUIRE (on != nullptr);
    CHECK (value (*on, proc::spRoute) == 0.0f);
    CHECK (value (*on, proc::spType) == static_cast<float> (proc::SpaceType::hall));
    CHECK (value (*on, proc::hallMix) == Catch::Approx (10.0f));
    CHECK (change (p2, "Adlibs", "space") == nullptr);   // gospel ad-libs: no ping-pong delay

    // An effect you set up already: left alone.
    chans.push_back (link ("FX 1-Vox Verb", 2));
    chans.back().proc[proc::spOn] = 1.0f;
    const auto p3 = makePlan (Tool::polish, report ({ lead, dbl, adlibs }), chans);
    CHECK (change (p3, "FX 1-Vox Verb", "space") == nullptr);
    CHECK (change (p3, "Lead Vox", "space") == nullptr);
    CHECK (noted (p3, "has a Space effect already"));
}

TEST_CASE ("Polish: a boxy kick gets a dip, a stereo 808's lows go to the middle", "[polish]")
{
    auto bass = track ("808", Role::bass, kickBands());
    bass.lowWidth = 0.35;
    auto chans = std::vector<ChannelNow> { link ("Kick"), link ("808", 2) };
    const auto p = makePlan (Tool::polish, report ({ track ("Kick", Role::kick, kickBands (14.0)), bass }), chans);
    const auto* box = change (p, "Kick", "eq");
    REQUIRE (box != nullptr);
    CHECK (box->to <= -1.5);
    CHECK (box->hz >= 250.0);
    CHECK (box->hz <= 520.0);
    const auto* w = change (p, "808", "width");
    REQUIRE (w != nullptr);
    CHECK (value (*w, proc::stLow) == 0.0f);
    CHECK (change (makePlan (Tool::polish, report ({ track ("Kick", Role::kick, kickBands()) }), chans), "Kick", "eq") == nullptr);
}

TEST_CASE ("Polish: the Full Mix's tone against the style", "[polish]")
{
    auto r = report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, 5.0, 2.0)) });
    r.mixBands.assign (static_cast<std::size_t> (kBands), 0.0);
    r.targetBands.assign (static_cast<std::size_t> (kBands), 0.0);
    for (int b = 26; b <= 29; ++b) r.mixBands[static_cast<std::size_t> (b)] = -4.0;   // 4 dB short of air
    MixNow mix;
    mix.known = true;
    mix.now = proc::defaults();
    const auto p = makePlan (Tool::polish, r, { link ("Lead Vox") }, mix);
    const auto* t = change (p, "Full Mix", "tone");
    REQUIRE (t != nullptr);
    CHECK (t->fullMix);
    CHECK (value (*t, proc::tbOn) == 1.0f);
    CHECK (value (*t, proc::tbAir) == Catch::Approx (2.0f));
    CHECK (value (*t, proc::tbWarmth) == Catch::Approx (0.0f));
    CHECK (t->why.find ("short of air") != std::string::npos);

    // Applied (and Analyze Mix's numbers are from before): nothing more.
    mix.atAnalysis = mix.now;
    mix.atAnalysisKnown = true;
    mix.now[proc::tbOn] = 1.0f;
    mix.now[proc::tbAir] = 2.0f;
    CHECK (change (makePlan (Tool::polish, r, { link ("Lead Vox") }, mix), "Full Mix", "tone") == nullptr);
    // Without Mixology's numbers (an older plug-in): no tone change.
    CHECK (change (makePlan (Tool::polish, r, { link ("Lead Vox") }), "Full Mix", "tone") == nullptr);
}

TEST_CASE ("Polish: Auto Mix includes it; the other tools don't", "[polish]")
{
    auto chans = std::vector<ChannelNow> { link ("Lead Vox") };
    const auto r = report ({ track ("Lead Vox", Role::vox, vocalBands (-14.0, 9.0)) });
    CHECK (change (makePlan (Tool::autoMix, r, chans), "Lead Vox", "eq") != nullptr);
    CHECK (change (makePlan (Tool::cleanUp, r, chans), "Lead Vox", "eq") == nullptr);
    CHECK (toolFromKey ("polish") == Tool::polish);
    CHECK (std::string (toolName (Tool::polish)) == "Polish");
}

TEST_CASE ("Analysis: a track's level swing and its loud peaks", "[polish]")
{
    // A vocal with loud and quiet lines (12 dB apart), and an even one.
    Analysis a;
    a.begin (Style {});
    for (int i = 0; i < 120; ++i)
    {
        Moment m;
        auto frame = [] (double rms, double crest) {
            AnalysisFrame f;
            f.silent = false;
            for (auto& b : f.bandDb) b = static_cast<float> (rms - 15.0);
            f.rmsDb = static_cast<float> (rms);
            f.loudness = static_cast<float> (rms + 2.0);
            f.peakDb = static_cast<float> (rms + crest);
            return f;
        };
        TrackSample v;
        v.hash = bridge::nameHash ("Lead Vox");
        v.name = "Lead Vox";
        v.frame = frame ((i / 10) % 2 ? -18.0 : -30.0, 10.0);
        TrackSample e;
        e.hash = bridge::nameHash ("Pad");
        e.name = "Pad";
        e.frame = frame (-24.0, 6.0);
        m.tracks = { v, e };
        m.mix = frame (-14.0, 8.0);
        a.add (m);
    }
    const auto r = a.evaluate ({});
    const TrackReport* vox = nullptr;
    const TrackReport* pad = nullptr;
    for (const auto& t : r.tracks) { if (t.name == "Lead Vox") vox = &t; if (t.name == "Pad") pad = &t; }
    REQUIRE (vox != nullptr);
    REQUIRE (pad != nullptr);
    CHECK (vox->levelRange == Catch::Approx (12.0).margin (1.0));
    CHECK (vox->loudPeak == Catch::Approx (-8.0).margin (0.5));
    CHECK (pad->levelRange == Catch::Approx (0.0).margin (0.1));
}

TEST_CASE ("Polish: the FX channel reverb's level sits under the vocal", "[polish]")
{
    auto lead = track ("Lead Vox", Role::vox, vocalBands (-14.0, 5.0, 2.0));
    lead.loudness = -16.0;

    // Nothing reached the FX channel yet (no send): a send at 0 dB is assumed. Hip hop: 20 dB under the vocal,
    // a plate comes out ~5 dB under what goes in -> level -15 dB.
    auto chans = std::vector<ChannelNow> { link ("Lead Vox"), link ("FX 1-Vox Verb", 2) };
    const auto p = makePlan (Tool::polish, report ({ lead }), chans);
    const auto* fx = change (p, "FX 1-Vox Verb", "space");
    REQUIRE (fx != nullptr);
    CHECK (value (*fx, proc::spLevel) == Catch::Approx (-15.0f));
    CHECK (fx->toText == "Vocal Plate, level -15.0 dB");
    CHECK (noted (p, "at 0 dB"));

    // The send was there while Mixology listened (the FX channel passed the vocal 10 dB down): level -5 dB.
    auto send = track ("FX 1-Vox Verb", Role::fx, vocalBands (-24.0, 5.0, 2.0));
    send.loudness = -26.0;
    const auto p2 = makePlan (Tool::polish, report ({ lead, send }), chans);
    const auto* fx2 = change (p2, "FX 1-Vox Verb", "space");
    REQUIRE (fx2 != nullptr);
    CHECK (value (*fx2, proc::spLevel) == Catch::Approx (-5.0f));
    CHECK_FALSE (noted (p2, "at 0 dB"));

    // A reverb already on the FX channel, coming back only 4 dB under the vocal: turned down to 20 dB under.
    auto loud = send;
    loud.loudness = -20.0;
    auto busy = chans;
    busy[1].proc[proc::spOn] = 1.0f;
    busy[1].proc[proc::spRoute] = 1.0f;
    busy[1].procAtAnalysis = busy[1].proc;
    busy[1].procAtAnalysisKnown = true;
    const auto p3 = makePlan (Tool::polish, report ({ lead, loud }), busy);
    const auto* lv = change (p3, "FX 1-Vox Verb", "space");
    REQUIRE (lv != nullptr);
    REQUIRE (lv->proc.size() == 1);   // only its level: the effect itself stays yours
    CHECK (value (*lv, proc::spLevel) == Catch::Approx (-16.0f));
    CHECK (lv->why.find ("4 dB under") != std::string::npos);
    // Applied: opening it again (same analysis) finds nothing more.
    apply (busy, p3);
    CHECK (change (makePlan (Tool::polish, report ({ lead, loud }), busy), "FX 1-Vox Verb", "space") == nullptr);
    // At a good level: a note, no change.
    auto fine = send;
    fine.loudness = -35.0;
    busy[1].proc[proc::spLevel] = 0.0f;
    const auto p4 = makePlan (Tool::polish, report ({ lead, fine }), busy);
    CHECK (change (p4, "FX 1-Vox Verb", "space") == nullptr);
    CHECK (noted (p4, "at a good level"));
}

TEST_CASE ("Polish: Voxology on a vocal: no second compressor, reverb or delay", "[polish][suite]")
{
    auto lead = track ("Lead Vox", Role::vox, vocalBands (-14.0, 5.0, 2.0));
    lead.levelRange = 11.0;
    lead.loudPeak = -4.0;
    auto adlibs = track ("Adlibs", Role::adlibs, vocalBands (-20.0, 5.0, 2.0));
    adlibs.levelRange = 10.0;
    adlibs.loudPeak = -8.0;
    auto chans = std::vector<ChannelNow> { link ("Lead Vox"), link ("Adlibs"), link ("FX 1-Vox Verb", 2) };

    // Voxology compresses the lead and adds its reverb; on the ad-libs it adds its delay.
    chans[0].voxology.on = chans[0].voxology.compressor = chans[0].voxology.reverb = true;
    chans[0].voxology.compressionDb = 4.4;
    chans[1].voxology.on = chans[1].voxology.delay = true;
    const auto p = makePlan (Tool::polish, report ({ lead, adlibs }), chans);
    CHECK (change (p, "Lead Vox", "comp") == nullptr);
    CHECK (noted (p, "'Lead Vox': Voxology compresses it already (about 4 dB lately)"));
    CHECK (change (p, "FX 1-Vox Verb", "space") == nullptr);
    CHECK (change (p, "Lead Vox", "space") == nullptr);
    CHECK (noted (p, "has Voxology's reverb"));
    CHECK_FALSE (noted (p, "If another plug-in"));
    CHECK (change (p, "Adlibs", "space") == nullptr);
    CHECK (noted (p, "'Adlibs' has Voxology's delay"));
    CHECK (change (p, "Adlibs", "comp") != nullptr);   // Voxology doesn't compress the ad-libs here: Mixology may

    // Voxology's reverb and an FX channel reverb as well: a warning, nothing proposed.
    chans[2].proc[proc::spOn] = 1.0f;
    const auto both = makePlan (Tool::polish, report ({ lead, adlibs }), chans);
    CHECK (noted (both, "gets reverb from Voxology and from the FX channel"));

    // Voxology bypassed (or gone): Mixology proposes as usual.
    chans[2].proc[proc::spOn] = 0.0f;
    chans[0].voxology = {};
    chans[1].voxology = {};
    const auto off = makePlan (Tool::polish, report ({ lead, adlibs }), chans);
    CHECK (change (off, "Lead Vox", "comp") != nullptr);
    CHECK (change (off, "FX 1-Vox Verb", "space") != nullptr);
    CHECK (change (off, "Adlibs", "space") != nullptr);
}

TEST_CASE ("Analysis: the mix's tone against the style, for Polish and the hand-off to Masterology", "[polish][suite]")
{
    Report tone;
    CHECK_FALSE (toneAgainstStyle (tone).valid);   // no Analyze Mix yet
    tone.valid = true;
    tone.mixBands.assign (static_cast<std::size_t> (kBands), 0.0);
    tone.targetBands.assign (static_cast<std::size_t> (kBands), 0.0);
    for (int b = 26; b <= 29; ++b) tone.mixBands[static_cast<std::size_t> (b)] = -3.0;
    for (int b = 3; b <= 8; ++b) tone.mixBands[static_cast<std::size_t> (b)] = 2.0;
    const auto t = toneAgainstStyle (tone);
    REQUIRE (t.valid);
    CHECK (t.air == Catch::Approx (-3.0));
    CHECK (t.low == Catch::Approx (2.0));
    CHECK (t.presence == Catch::Approx (0.0));
}
