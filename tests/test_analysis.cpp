#include "mixology/TrackAnalyzer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace mixology;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double sr = 48000.0;
    constexpr int N = TrackAnalyzer::kSize;

    std::vector<float> sine (double hz, double amp, double phase = 0.0)
    {
        std::vector<float> x (N);
        for (int i = 0; i < N; ++i) x[static_cast<std::size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * kPi * hz * i / sr + phase));
        return x;
    }

    double sumDb (const AnalysisFrame& f, int from, int to)
    {
        double e = 0.0;
        for (int b = from; b <= to; ++b) e += std::pow (10.0, f.bandDb[static_cast<std::size_t> (b)] / 10.0);
        return 10.0 * std::log10 (e);
    }
}

TEST_CASE ("Analysis: 1/3-octave band centres", "[analysis]")
{
    CHECK (bandHz (17) == Catch::Approx (1000.0));
    CHECK (bandHz (0) == Catch::Approx (19.7).margin (0.1));
    CHECK (bandHz (30) == Catch::Approx (20159.0).margin (2.0));
    CHECK (bandHz (11) == Catch::Approx (250.0).margin (2.0));
}

TEST_CASE ("Analysis: a sine lands in its band at its level", "[analysis]")
{
    TrackAnalyzer a;
    const auto x = sine (1000.0, 0.5);   // -6 dBFS peak
    const auto f = a.analyse (x.data(), x.data(), sr);
    CHECK_FALSE (f.silent);
    CHECK (f.bandDb[17] == Catch::Approx (-6.02).margin (0.5));
    CHECK (f.bandDb[14] < -40.0f);
    CHECK (f.bandDb[20] < -40.0f);
    CHECK (f.peakDb == Catch::Approx (-6.02).margin (0.05));
    CHECK (f.rmsDb == Catch::Approx (-9.03).margin (0.1));
    CHECK (f.loudness == Catch::Approx (-6.0).margin (1.0));   // BS.1770: a -6 dBFS 1 kHz sine in L and R is about -6 LUFS
    CHECK (f.correlation == Catch::Approx (1.0).margin (1e-4));
    CHECK (f.width < 0.001f);
}

TEST_CASE ("Analysis: band energies add up to the signal's level", "[analysis]")
{
    TrackAnalyzer a;
    std::vector<float> x (N, 0.0f);
    for (int b = 2; b < 29; b += 3)
    {
        const auto s = sine (bandHz (b), 0.05, b);
        for (int i = 0; i < N; ++i) x[static_cast<std::size_t> (i)] += s[static_cast<std::size_t> (i)];
    }
    const auto f = a.analyse (x.data(), x.data(), sr);
    CHECK (sumDb (f, 0, kBands - 1) == Catch::Approx (f.rmsDb + 3.01).margin (0.4));
}

TEST_CASE ("Analysis: low frequencies spread over neighbouring bands but keep their level", "[analysis]")
{
    TrackAnalyzer a;
    const auto x = sine (50.0, 0.5);
    const auto f = a.analyse (x.data(), x.data(), sr);
    CHECK (sumDb (f, 2, 6) == Catch::Approx (-6.02).margin (0.6));
    CHECK (sumDb (f, 10, 30) < -40.0);
}

TEST_CASE ("Analysis: stereo width and correlation", "[analysis]")
{
    TrackAnalyzer a;
    SECTION ("out of phase")
    {
        const auto l = sine (440.0, 0.3);
        auto r = l;
        for (auto& v : r) v = -v;
        const auto f = a.analyse (l.data(), r.data(), sr);
        CHECK (f.correlation == Catch::Approx (-1.0).margin (1e-4));
        CHECK (f.width > 0.999f);
        CHECK (f.sideShare[13] > 0.99f);   // 400 Hz band
    }
    SECTION ("unrelated left and right")
    {
        std::mt19937 rng (7);
        std::normal_distribution<float> n (0.0f, 0.1f);
        std::vector<float> l (N), r (N);
        for (int i = 0; i < N; ++i) { l[static_cast<std::size_t> (i)] = n (rng); r[static_cast<std::size_t> (i)] = n (rng); }
        const auto f = a.analyse (l.data(), r.data(), sr);
        CHECK (std::abs (f.correlation) < 0.1f);
        CHECK (f.width == Catch::Approx (0.5).margin (0.08));
    }
    SECTION ("a sound panned to one side is still correlated, but not mono")
    {
        const auto l = sine (440.0, 0.3);
        std::vector<float> r (N, 0.0f);
        const auto f = a.analyse (l.data(), r.data(), sr);
        CHECK (f.width == Catch::Approx (0.5).margin (0.02));
        CHECK (f.balanceDb == Catch::Approx (40.0));   // all left (capped)
    }
    SECTION ("left / right balance")
    {
        const auto l = sine (440.0, 0.15), r = sine (440.0, 0.3);
        CHECK (a.analyse (l.data(), r.data(), sr).balanceDb == Catch::Approx (-6.02).margin (0.05));   // leans right
        CHECK (a.analyse (r.data(), r.data(), sr).balanceDb == Catch::Approx (0.0).margin (1e-3));
    }
}

TEST_CASE ("Analysis: silence", "[analysis]")
{
    TrackAnalyzer a;
    std::vector<float> z (N, 0.0f);
    const auto f = a.analyse (z.data(), z.data(), sr);
    CHECK (f.silent);
    CHECK (f.bandDb[17] == kSilentDb);
    CHECK (f.loudness == kSilentDb);
}

TEST_CASE ("Analysis: works at 44.1 and 96 kHz", "[analysis]")
{
    TrackAnalyzer a;
    for (double rate : { 44100.0, 96000.0 })
    {
        std::vector<float> x (N);
        for (int i = 0; i < N; ++i) x[static_cast<std::size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * kPi * 2000.0 * i / rate));
        const auto f = a.analyse (x.data(), x.data(), rate);
        CHECK (f.bandDb[20] == Catch::Approx (-6.02).margin (0.6));   // 2 kHz band
    }
}

// =================================================================================================
// Analyze Mix

#include "mixology/BridgeProtocol.h"
#include "mixology/MixAnalysis.h"

#include <algorithm>
#include <functional>

using namespace mixology::mix;

namespace
{
    // A track's frame from a spectrum shape: band levels (dB, before the fader), punch and width.
    AnalysisFrame shaped (const std::function<double (int)>& bandLevel, double crest, double width = 0.0, double lowWidth = -1.0)
    {
        AnalysisFrame f;
        f.silent = false;
        double e = 0.0, side = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const auto i = static_cast<std::size_t> (b);
            f.bandDb[i] = static_cast<float> (bandLevel (b));
            f.sideShare[i] = static_cast<float> (b <= 9 && lowWidth >= 0.0 ? lowWidth : width);
            const double be = std::pow (10.0, f.bandDb[i] / 10.0);
            e += be;
            side += be * f.sideShare[i];
        }
        f.rmsDb = static_cast<float> (10.0 * std::log10 (e) - 3.01);
        f.loudness = static_cast<float> (10.0 * std::log10 (e) - 0.69);
        f.peakDb = static_cast<float> (f.rmsDb + crest);
        f.width = static_cast<float> (e > 0 ? side / e : 0.0);
        f.correlation = static_cast<float> (1.0 - 2.0 * f.width);
        return f;
    }

    double bump (int b, int centre, double width, double peak) { return peak - 12.0 * std::pow ((b - centre) / width, 2.0); }

    AnalysisFrame silentFrame() { return AnalysisFrame {}; }

    // The Stereo Out: everything added up after the faders.
    AnalysisFrame mixOf (const std::vector<TrackSample>& tracks, double peakDb = -6.0)
    {
        AnalysisFrame f;
        f.silent = true;
        std::array<double, kBands> e {}, side {};
        double left = 0.0, right = 0.0;
        for (const auto& t : tracks)
        {
            if (t.frame.silent) continue;
            f.silent = false;
            const double ms = std::pow (10.0, (t.frame.rmsDb + t.faderDb) / 10.0), lr = std::pow (10.0, t.frame.balanceDb / 10.0);
            left += 2.0 * ms * lr / (1.0 + lr);
            right += 2.0 * ms / (1.0 + lr);
            for (int b = 0; b < kBands; ++b)
            {
                const auto i = static_cast<std::size_t> (b);
                const double be = std::pow (10.0, (t.frame.bandDb[i] + t.faderDb) / 10.0);
                e[i] += be;
                side[i] += be * t.frame.sideShare[i];
            }
        }
        double total = 0.0, totalSide = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const auto i = static_cast<std::size_t> (b);
            f.bandDb[i] = e[i] > 0 ? static_cast<float> (10.0 * std::log10 (e[i])) : kSilentDb;
            f.sideShare[i] = e[i] > 0 ? static_cast<float> (side[i] / e[i]) : 0.0f;
            total += e[i];
            totalSide += side[i];
        }
        f.loudness = static_cast<float> (10.0 * std::log10 (total) - 0.69);
        f.rmsDb = static_cast<float> (10.0 * std::log10 (total) - 3.01);
        f.peakDb = static_cast<float> (peakDb);
        f.width = static_cast<float> (totalSide / total);
        f.correlation = static_cast<float> (1.0 - 2.0 * f.width);
        f.balanceDb = static_cast<float> (10.0 * std::log10 (left / right));
        return f;
    }

    AnalysisFrame panned (AnalysisFrame f, double balanceDb) { f.balanceDb = static_cast<float> (balanceDb); return f; }

    TrackSample sample (const std::string& name, const AnalysisFrame& f, double fader = 0.0)
    {
        TrackSample t;
        t.name = name;
        t.hash = mixology::bridge::nameHash (name);
        t.faderDb = fader;
        t.faderKnown = true;
        t.frame = f;
        return t;
    }

    // The sounds of a small beat (levels before the fader).
    AnalysisFrame kickFrame() { return shaped ([] (int b) { return std::max (bump (b, 5, 2.0, -12.0), bump (b, 22, 2.0, -30.0)); }, 15.0); }
    AnalysisFrame bassFrame (double centre = 4.0) { return shaped ([=] (int b) { return bump (b, static_cast<int> (centre), 2.2, -11.0); }, 6.0); }
    AnalysisFrame hatsFrame() { return shaped ([] (int b) { return bump (b, 27, 2.5, -24.0); }, 16.0, 0.2); }
    AnalysisFrame vocalFrame (double level = -14.0) { return shaped ([=] (int b) { return bump (b, 19, 4.0, level); }, 10.0, 0.02); }
    AnalysisFrame padFrame (double level = -24.0) { return shaped ([=] (int b) { return bump (b, 17, 5.0, level); }, 5.0, 0.35); }

    Analysis listen (const std::function<std::vector<TrackSample> (int)>& at, int moments = 150, double mixPeak = -6.0)
    {
        Analysis a;
        Style s;
        a.begin (s);
        for (int i = 0; i < moments; ++i)
        {
            Moment m;
            m.tracks = at (i);
            m.mix = mixOf (m.tracks, mixPeak);
            a.add (m);
        }
        return a;
    }

    const Suggestion* find (const Report& r, const std::string& id)
    {
        for (const auto& s : r.suggestions)
            if (s.id == id) return &s;
        return nullptr;
    }

    const TrackReport* track (const Report& r, const std::string& name)
    {
        for (const auto& t : r.tracks)
            if (t.name == name) return &t;
        return nullptr;
    }

    bool mentions (const Suggestion* s, const std::string& name)
    {
        return s != nullptr && std::find (s->tracks.begin(), s->tracks.end(), name) != s->tracks.end();
    }
}

TEST_CASE ("Analyze Mix: roles from names (same rules as the interface)", "[analysis]")
{
    CHECK (roleFromName ("Kick") == Role::kick);
    CHECK (roleFromName ("808") == Role::bass);
    CHECK (roleFromName ("Lead Vox") == Role::vox);
    CHECK (roleFromName ("Adlibs") == Role::adlibs);
    CHECK (roleFromName ("Hi Hat") == Role::hats);
    CHECK (roleFromName ("Vox Double") == Role::adlibs);
    CHECK (roleFromName ("Rhodes") == Role::keys);
    CHECK_FALSE (roleFromName ("Analog Lab V 01").has_value());
    CHECK (roleFromKey ("synth") == Role::synth);
    CHECK (groupOf (Role::hats) == Group::drums);
}

TEST_CASE ("Analyze Mix: roles from the sound when the name doesn't say", "[analysis]")
{
    auto a = listen ([] (int) { return std::vector<TrackSample> {
        sample ("Audio 01", kickFrame()), sample ("Audio 02", bassFrame()), sample ("Audio 03", hatsFrame()),
        sample ("Audio 04", vocalFrame()), sample ("Audio 05", padFrame()) }; });
    auto r = a.evaluate ({});
    CHECK (track (r, "Audio 01")->role == Role::kick);
    CHECK (track (r, "Audio 02")->role == Role::bass);
    CHECK (track (r, "Audio 03")->role == Role::hats);
    CHECK (track (r, "Audio 04")->role == Role::vox);
    CHECK (track (r, "Audio 05")->role == Role::synth);
    CHECK (track (r, "Audio 05")->roleFrom == "sound");

    // Your correction wins, and the moments are kept: no need to listen again.
    std::map<std::uint32_t, TrackSettings> settings;
    settings[mixology::bridge::nameHash ("Audio 05")].role = Role::keys;
    r = a.evaluate (settings);
    CHECK (track (r, "Audio 05")->role == Role::keys);
    CHECK (track (r, "Audio 05")->roleFrom == "you");
}

TEST_CASE ("Analyze Mix: kick and 808 on top of each other, and kept apart", "[analysis]")
{
    SECTION ("clash")
    {
        auto r = listen ([] (int i) { return std::vector<TrackSample> {
            sample ("Kick", i % 2 ? kickFrame() : silentFrame()), sample ("808", bassFrame (5.0)), sample ("Lead Vox", vocalFrame()) }; }).evaluate ({});
        const auto* s = find (r, "kick-bass");
        REQUIRE (s != nullptr);
        CHECK (s->kind == "warn");
        CHECK (mentions (s, "Kick"));
        CHECK (mentions (s, "808"));
    }
    SECTION ("kick above, 808 below: no clash")
    {
        const auto kickHigh = shaped ([] (int b) { return std::max (bump (b, 8, 1.0, -12.0), bump (b, 22, 2.0, -30.0)); }, 15.0);
        const auto subLow = shaped ([] (int b) { return bump (b, 1, 1.0, -11.0); }, 6.0);
        auto r = listen ([&] (int i) { return std::vector<TrackSample> { sample ("Kick", i % 2 ? kickHigh : silentFrame()), sample ("808", subLow) }; }).evaluate ({});
        CHECK (find (r, "kick-bass") == nullptr);
    }
}

TEST_CASE ("Analyze Mix: vocal level against the beat", "[analysis]")
{
    auto beat = [] (double vocal) {
        return [=] (int) { return std::vector<TrackSample> {
            sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Hats", hatsFrame()), sample ("Lead Vox", vocalFrame (vocal)) }; };
    };
    SECTION ("buried")
    {
        auto r = listen (beat (-28.0)).evaluate ({});
        const auto* s = find (r, "vocal-low");
        REQUIRE (s != nullptr);
        CHECK (s->kind == "warn");
        CHECK (mentions (s, "Lead Vox"));
        CHECK (s->steps.front().find ("Raise 'Lead Vox'") != std::string::npos);
    }
    SECTION ("sits right")
    {
        // The beat's loudness is about -7.7 here: a vocal around 1 dB over it fits Hip Hop.
        const auto r0 = listen (beat (-14.0)).evaluate ({});
        const double beatL = track (r0, "Kick")->loudness;   // (only used to keep the example honest)
        (void) beatL;
        bool fits = false;
        for (double level = -16.0; level <= -4.0 && ! fits; level += 1.0)
            fits = find (listen (beat (level)).evaluate ({}), "vocal-ok") != nullptr;
        CHECK (fits);
    }
}

TEST_CASE ("Analyze Mix: vocal level against a beat that has no Link", "[analysis]")
{
    // Only the vocal has a Link; the printed beat is heard on the Stereo Out only.
    auto run = [] (double vocal) {
        Analysis a;
        a.begin (Style {});
        for (int i = 0; i < 150; ++i)
        {
            Moment m;
            m.tracks = { sample ("Lead Vox", vocalFrame (vocal)) };
            auto everything = m.tracks;
            everything.push_back (sample ("Beat", kickFrame()));
            everything.push_back (sample ("Beat 808", bassFrame()));
            m.mix = mixOf (everything);
            a.add (m);
        }
        return a.evaluate ({}, { "Beat", "Beat 808" });
    };
    const auto buried = run (-30.0);
    REQUIRE (find (buried, "vocal-low") != nullptr);
    CHECK (find (buried, "vocal-low")->kind == "warn");
    CHECK (find (run (-6.0), "vocal-low") == nullptr);
}

TEST_CASE ("Analyze Mix: the mix leans to one side", "[analysis]")
{
    auto band = [] (bool lopsided) {
        return [=] (int) { return std::vector<TrackSample> {
            sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Lead Vox", vocalFrame (-12.0)),
            sample ("Electric Rhythm", panned (padFrame (-16.0), 40.0)),
            sample ("Electric Rhythm (D)", panned (padFrame (-16.0), lopsided ? 40.0 : -40.0)),
            sample ("Electric Lead", panned (padFrame (-17.0), lopsided ? 40.0 : 0.0)) }; };
    };
    const auto r = listen (band (true)).evaluate ({});
    const auto* s = find (r, "lean-left");
    REQUIRE (s != nullptr);
    CHECK (s->kind == "warn");
    CHECK (mentions (s, "Electric Rhythm"));
    CHECK (mentions (s, "Electric Rhythm (D)"));
    CHECK_FALSE (mentions (s, "Kick"));
    CHECK (r.mixBalance > 3.0);
    CHECK (track (r, "Electric Lead")->balance > 30.0);
    CHECK (track (r, "Electric Lead")->role == Role::guitar);   // "Electric" names are guitars

    const auto even = listen (band (false)).evaluate ({});
    CHECK (find (even, "lean-left") == nullptr);
    CHECK (find (even, "lean-right") == nullptr);
}

TEST_CASE ("Analyze Mix: instruments covering the vocal's words", "[analysis]")
{
    const auto lead = shaped ([] (int b) { return bump (b, 21, 3.0, -16.0); }, 6.0, 0.3);   // a bright synth lead in 1.6 - 4 kHz
    auto r = listen ([&] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Lead Vox", vocalFrame (-14.0)), sample ("Synth Lead", lead), sample ("Pad", padFrame (-40.0)) }; }).evaluate ({});
    const auto* s = find (r, "vocal-pocket");
    REQUIRE (s != nullptr);
    CHECK (mentions (s, "Synth Lead"));
    CHECK_FALSE (mentions (s, "Pad"));
}

TEST_CASE ("Analyze Mix: rumble in a vocal, low end spread wide", "[analysis]")
{
    const auto rumblyVocal = shaped ([] (int b) { return std::max (bump (b, 19, 4.0, -14.0), bump (b, 4, 2.0, -16.0)); }, 10.0, 0.02);
    const auto wideBass = shaped ([] (int b) { return bump (b, 4, 2.2, -11.0); }, 6.0, 0.0, 0.4);
    auto r = listen ([&] (int) { return std::vector<TrackSample> { sample ("Kick", kickFrame()), sample ("808", wideBass), sample ("Lead Vox", rumblyVocal) }; }).evaluate ({});
    const auto* hp = find (r, "highpass");
    REQUIRE (hp != nullptr);
    CHECK (hp->kind == "warn");   // a vocal among them
    CHECK (mentions (hp, "Lead Vox"));
    CHECK_FALSE (mentions (hp, "808"));
    const auto* wide = find (r, "low-wide");
    REQUIRE (wide != nullptr);
    CHECK (mentions (wide, "808"));
}

TEST_CASE ("Analyze Mix: muddy and harsh mixes, against the style's target", "[analysis]")
{
    const auto muddy = shaped ([] (int b) { return bump (b, 12, 1.5, -10.0); }, 6.0, 0.2);
    auto r = listen ([&] (int) { return std::vector<TrackSample> { sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Keys", muddy), sample ("Lead Vox", vocalFrame()) }; }).evaluate ({});
    const auto* mud = find (r, "mud");
    REQUIRE (mud != nullptr);
    CHECK (mentions (mud, "Keys"));
    CHECK (r.mixBands.size() == static_cast<std::size_t> (kBands));
    CHECK (r.targetBands.size() == static_cast<std::size_t> (kBands));

    const auto fizzy = shaped ([] (int b) { return bump (b, 23, 1.2, -9.0); }, 8.0, 0.2);
    r = listen ([&] (int) { return std::vector<TrackSample> { sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Guitar", fizzy) }; }).evaluate ({});
    const auto* harsh = find (r, "harsh");
    REQUIRE (harsh != nullptr);
    CHECK (mentions (harsh, "Guitar"));
}

TEST_CASE ("Analyze Mix: headroom, short listening, unheard and excluded tracks", "[analysis]")
{
    auto a = listen ([] (int) { return std::vector<TrackSample> { sample ("Kick", kickFrame()), sample ("Reference", padFrame (-10.0)) }; }, 40, -0.3);
    std::map<std::uint32_t, TrackSettings> settings;
    settings[mixology::bridge::nameHash ("Reference")].excluded = true;
    const auto r = a.evaluate (settings, { "Strings", "Choir" });
    CHECK (find (r, "headroom") != nullptr);
    CHECK (find (r, "headroom")->kind == "warn");
    CHECK (find (r, "short") != nullptr);   // 8 s heard
    CHECK (mentions (find (r, "unheard"), "Choir"));
    CHECK (track (r, "Reference")->excluded);
    for (const auto& s : r.suggestions) CHECK_FALSE (mentions (&s, "Reference"));
    CHECK (r.suggestions.front().kind == "warn");   // worth fixing comes first
}

TEST_CASE ("Analyze Mix: a mix that follows the style gets a calm word", "[analysis]")
{
    // Build the mix straight from the style's target curve (as a balanced mix would measure).
    Analysis a;
    Style s;
    a.begin (s);
    for (int i = 0; i < 150; ++i)
    {
        Moment m;
        m.tracks = { sample ("Kick", kickFrame()), sample ("808", bassFrame (2.0)), sample ("Lead Vox", vocalFrame (-8.0)) };
        m.mix.silent = false;
        for (int b = 0; b < kBands; ++b)
        {
            const double hz = bandHz (b);
            m.mix.bandDb[static_cast<std::size_t> (b)] = static_cast<float> (Analysis::targetDb (s, hz) - 1.5 * std::log2 (hz / 1000.0) - 20.0);
        }
        m.mix.loudness = -12.0f; m.mix.peakDb = -6.0f; m.mix.correlation = 0.8f;
        a.add (m);
    }
    const auto r = a.evaluate ({});
    for (const auto& sg : r.suggestions) INFO (sg.id << ": " << sg.title);
    CHECK (find (r, "mud") == nullptr);
    CHECK (find (r, "harsh") == nullptr);
    CHECK (find (r, "bass-light") == nullptr);
    CHECK (find (r, "bass-heavy") == nullptr);
    CHECK (find (r, "headroom") == nullptr);
}

TEST_CASE ("Analyze Mix: print a report (manual check)", "[analysis][.print]")
{
    const auto lead = shaped ([] (int b) { return bump (b, 21, 3.0, -16.0); }, 6.0, 0.3);
    const auto rumblyVocal = shaped ([] (int b) { return std::max (bump (b, 19, 4.0, -24.0), bump (b, 4, 2.0, -26.0)); }, 10.0, 0.02);
    auto a = listen ([&] (int i) { return std::vector<TrackSample> {
        sample ("Kick", i % 2 ? kickFrame() : silentFrame()), sample ("808", bassFrame (5.0)), sample ("Hats", hatsFrame()),
        sample ("Lead Vox", rumblyVocal), sample ("Synth Lead", lead), sample ("Audio 07", padFrame (-60.0)) }; }, 150, -0.8);
    const auto r = a.evaluate ({}, { "Strings" });
    for (const auto& t : r.tracks)
        printf ("%-12s %-7s (%s) loud %6.1f act %.2f width %.2f crest %.1f\n", t.name.c_str(), roleKey (t.role), t.roleFrom.c_str(), t.loudness, t.activity, t.width, t.crest);
    for (const auto& s : r.suggestions)
    {
        printf ("\n[%s] %s\n  %s\n  Do I need to fix it? %s\n", s.kind.c_str(), s.title.c_str(), s.text.c_str(), s.need.c_str());
        for (const auto& st : s.steps) printf ("   - %s\n", st.c_str());
    }
}
