#include "mixology/AutoAnalyze.h"
#include "mixology/TrackAnalyzer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <vector>

using namespace mixology;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double sr = 48000.0;
    constexpr int N = TrackAnalyzer::sizeFor (sr);

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
        const int n = TrackAnalyzer::sizeFor (rate);
        std::vector<float> x (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i) x[static_cast<std::size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * kPi * 2000.0 * i / rate));
        const auto f = a.analyse (x.data(), x.data(), rate);
        CHECK (f.bandDb[20] == Catch::Approx (-6.02).margin (0.6));   // 2 kHz band
    }
}

TEST_CASE ("Analysis: every band hears the same noise the same way at every sample rate", "[analysis]")
{
    // A moment is about 85 ms at every rate, so the bands are as wide in Hz at 96 or 192 kHz as at 48 kHz, and
    // a bin wider than the lowest bands shares its energy with them: no band is ever left empty.
    auto bands = [] (double rate) {
        std::array<double, kBands> sum {};
        TrackAnalyzer a;
        const int n = TrackAnalyzer::sizeFor (rate);
        std::mt19937 rng (11);
        std::normal_distribution<float> noise (0.0f, 0.1f);
        std::vector<float> x (static_cast<std::size_t> (n));
        for (int rep = 0; rep < 64; ++rep)   // averaged over 64 moments: noise is noisy
        {
            for (auto& v : x) v = noise (rng);
            const auto f = a.analyse (x.data(), x.data(), rate);
            for (int b = 0; b < kBands; ++b) sum[static_cast<std::size_t> (b)] += std::pow (10.0, f.bandDb[static_cast<std::size_t> (b)] / 10.0) / 64.0;
        }
        std::array<double, kBands> db {};
        for (int b = 0; b < kBands; ++b) db[static_cast<std::size_t> (b)] = 10.0 * std::log10 (sum[static_cast<std::size_t> (b)]);
        return db;
    };
    const auto at48 = bands (48000.0);
    for (double rate : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const auto other = bands (rate);
        for (int b = 0; b < kBands; ++b)
        {
            // White noise: the same energy per Hz at every rate, so a band reads the same, minus how much more
            // bandwidth the faster rate spreads the same noise over.
            const double expected = at48[static_cast<std::size_t> (b)] - 10.0 * std::log10 (rate / 48000.0);
            if (bandHz (b) * 1.12 >= rate / 2.0) continue;   // above that rate's Nyquist
            INFO ("rate " << rate << " band " << b);
            CHECK (other[static_cast<std::size_t> (b)] > -90.0);
            CHECK (other[static_cast<std::size_t> (b)] == Catch::Approx (expected).margin (1.5));
        }
    }
    for (int b = 0; b < kBands; ++b) CHECK (at48[static_cast<std::size_t> (b)] > -90.0);   // 20 and 31.5 Hz too
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
    // The table both copies of the rules are checked against (the interface's in tools/rolecheck).
    std::ifstream file (MIXOLOGY_TESTS_DIR "/role_names.txt");
    REQUIRE (file.good());
    int checked = 0;
    for (std::string line; std::getline (file, line);)
    {
        const auto bar = line.find (" | ");
        if (line.empty() || line[0] == '#' || bar == std::string::npos) continue;
        const auto name = line.substr (0, bar), want = line.substr (bar + 3);
        const auto got = roleFromName (name);
        INFO ("'" << name << "' reads as '" << nameWords (name) << "'");
        CHECK ((got ? std::string (roleKey (*got)) : std::string ("-")) == want);
        ++checked;
    }
    CHECK (checked > 50);
    CHECK (nameWords ("LeadVox2 (Hook)") == "lead vox 2 hook");
    CHECK (nameWords ("  Hi-Hat__808s ") == "hi hat 808 s");
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
        auto a = listen (beat (-28.0));
        auto r = a.evaluate ({});
        const auto* s = find (r, "vocal-low");
        REQUIRE (s != nullptr);
        CHECK (s->kind == "info");          // the style's opinion: your ears decide
        CHECK (mentions (s, "Lead Vox"));
        CHECK (s->steps.front().find ("Raise 'Lead Vox'") != std::string::npos);
        CHECK (s->steps.back().find ("Sounds right") != std::string::npos);

        // You said how you like it: the advice follows your setting.
        a.setVocalOffset (5.0, true);
        r = a.evaluate ({});
        REQUIRE (find (r, "vocal-low") != nullptr);
        CHECK (find (r, "vocal-low")->kind == "warn");
        CHECK (find (r, "vocal-low")->text.find ("your setting") != std::string::npos);
        CHECK (r.vocalTarget == Catch::Approx (r.styleVocalTarget + 5.0));
        a.setVocalOffset (r.vocalRatio - r.styleVocalTarget, true);   // "sounds right"
        r = a.evaluate ({});
        CHECK (find (r, "vocal-ok") != nullptr);
        CHECK (find (r, "vocal-ok")->title.find ("your setting") != std::string::npos);
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

TEST_CASE ("Analyze Mix: the moments saved with the project give the same advice", "[analysis]")
{
    auto a = listen ([] (int i) {
        std::vector<TrackSample> t { sample ("Kick", kickFrame(), -2.0), sample ("808", bassFrame (5.0), -1.5),
                                     sample ("Hats", panned (hatsFrame(), 4.0), -6.0), sample ("Pad", padFrame (-20.0), -3.0) };
        t.push_back (sample ("Lead Vox", i % 3 == 0 ? silentFrame() : vocalFrame (-22.0 + (i % 7)), 1.0));
        t.push_back (sample ("Adlibs", i % 5 == 0 ? vocalFrame (-26.0) : silentFrame(), -4.0));
        return t; }, 180, -3.5);
    a.setMusicBus (true, -2.0);
    a.setVocalOffset (1.5, true);
    std::map<std::uint32_t, TrackSettings> settings;
    settings[mixology::bridge::nameHash ("Pad")].role = Role::keys;
    const auto before = a.evaluate (settings, { "Strings" });

    Analysis back;
    REQUIRE (back.load (a.save()));
    CHECK (back.secondsHeard() == a.secondsHeard());
    const auto after = back.evaluate (settings, { "Strings" });
    REQUIRE (after.valid);
    CHECK (after.style.key == before.style.key);
    CHECK (after.style.vocalFromYou);
    REQUIRE (after.suggestions.size() == before.suggestions.size());
    for (std::size_t i = 0; i < before.suggestions.size(); ++i)
    {
        CHECK (after.suggestions[i].id == before.suggestions[i].id);
        CHECK (after.suggestions[i].kind == before.suggestions[i].kind);
    }
    REQUIRE (after.tracks.size() == before.tracks.size());
    for (std::size_t i = 0; i < before.tracks.size(); ++i)
    {
        CHECK (after.tracks[i].name == before.tracks[i].name);
        CHECK (after.tracks[i].role == before.tracks[i].role);
        CHECK (after.tracks[i].loudness == Catch::Approx (before.tracks[i].loudness).margin (0.06));
        CHECK (after.tracks[i].width == Catch::Approx (before.tracks[i].width).margin (0.005));
    }
    CHECK (after.vocalRatio == Catch::Approx (before.vocalRatio).margin (0.06));
    CHECK (after.mixLoudness == Catch::Approx (before.mixLoudness).margin (0.06));
    CHECK (after.musicPathDb == Catch::Approx (before.musicPathDb).margin (0.06));
    CHECK (after.unheard == before.unheard);

    // A damaged block changes nothing.
    const auto block = a.save();
    CHECK_FALSE (back.load (block.substr (0, block.size() - 100)));
    CHECK_FALSE (back.load (""));
    CHECK (back.secondsHeard() == a.secondsHeard());
    // And it is small: about 100 bytes per track per moment (fewer when it's silent) before the project compresses it.
    CHECK (block.size() < static_cast<std::size_t> (180 * 7 * 110));
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

// =============================================================================================
// Milestone 4: the plans behind Auto Mix, Balance, Stereo Architect, Clean Up and Vocal Pocket

#include "mixology/MixPlanner.h"

namespace
{
    using mixology::mix::ChannelNow;
    using mixology::mix::Plan;
    using mixology::mix::Tool;
    using mixology::mix::makePlan;

    ChannelNow channel (const std::string& name, double fader = 0.0, double pan = 0.0, bool link = true, bool locked = false)
    {
        ChannelNow c;
        c.hash = mixology::bridge::nameHash (name);
        c.name = name;
        c.faderKnown = true;
        c.faderDb = fader;
        c.hasPan = true;
        c.pan = pan;
        c.hasLink = link;
        c.channels = link ? 1 : 0;   // mono tracks (the ones Stereo Architect may pan)
        c.locked = locked;
        c.assistAtAnalysisKnown = link;   // Mixology heard it with no Assist EQ
        return c;
    }

    std::vector<ChannelNow> channelsOf (const std::vector<TrackSample>& tracks)
    {
        std::vector<ChannelNow> out;
        for (const auto& t : tracks) out.push_back (channel (t.name, t.faderDb));
        return out;
    }

    const mixology::mix::Change* change (const Plan& p, const std::string& name, const std::string& kind)
    {
        for (const auto& c : p.changes)
            if (c.name == name && c.kind == kind) return &c;
        return nullptr;
    }

    bool noted (const Plan& p, const std::string& text)
    {
        return std::any_of (p.notes.begin(), p.notes.end(), [&] (const std::string& n) { return n.find (text) != std::string::npos; });
    }
}

TEST_CASE ("Plans: pairs and doubles are found from the names", "[plan]")
{
    using mixology::mix::pairName;
    CHECK (pairName ("Electric Rhythm (D)").base == "electric rhythm");
    CHECK (pairName ("Electric Rhythm (D)").explicitPair);
    CHECK (pairName ("Electric Rhythm").base == "electric rhythm");
    CHECK_FALSE (pairName ("Electric Rhythm").explicitPair);
    CHECK (pairName ("Gtr L").side == -1);
    CHECK (pairName ("Gtr (R)").side == 1);
    CHECK (pairName ("Gtr R").base == "gtr");
    CHECK (pairName ("Keys 2").order == 1);
    CHECK (pairName ("Keys 2").base == "keys");
    CHECK (pairName ("Lead").base == "lead");          // not "Lea" + a double
    CHECK (pairName ("Gtrl").base == "gtrl");
    CHECK (pairName ("Bass 808").base == "bass 808");
    CHECK (pairName ("Vox Dbl").explicitPair);
}

TEST_CASE ("Plans: nothing without Analyze Mix", "[plan]")
{
    const auto p = makePlan (Tool::autoMix, mixology::mix::Report {}, {});
    CHECK_FALSE (p.valid);
    CHECK (p.summary.find ("Analyze Mix") != std::string::npos);
}

TEST_CASE ("Plans: Balance lifts a buried vocal and makes room for it", "[plan]")
{
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Hats", hatsFrame()), sample ("Lead Vox", vocalFrame (-22.0), -6.0) }; };
    const auto r = listen (tracks).evaluate ({});
    REQUIRE (r.hasVocalRatio);
    REQUIRE (r.vocalTarget - r.vocalRatio > 6.0);
    auto move = [] (const mixology::mix::Change* c) { return c == nullptr ? 0.0 : c->wanted - c->from; };

    SECTION ("split between the vocal and the beat, same overall loudness")
    {
        const auto p = makePlan (Tool::balance, r, channelsOf (tracks (0)));
        REQUIRE (p.valid);
        const auto* v = change (p, "Lead Vox", "level");
        REQUIRE (v != nullptr);
        CHECK (move (v) - move (change (p, "Kick", "level")) == Catch::Approx (r.vocalTarget - r.vocalRatio).margin (0.4));
        CHECK (move (change (p, "Kick", "level")) == Catch::Approx (move (change (p, "808", "level"))).margin (0.01));   // the beat moves together
        CHECK (v->why.find ("sits low") != std::string::npos);
        CHECK (noted (p, "press 'Right' above"));
        CHECK (p.headroomDb == 0.0);   // peaks at -6: no room needed
    }
    SECTION ("a vocal fader near the top: the beat makes the rest of the room")
    {
        auto chans = channelsOf (tracks (0));
        for (auto& c : chans) if (c.name == "Lead Vox") c.faderDb = 5.0;
        const auto p = makePlan (Tool::balance, r, chans);
        CHECK ((change (p, "Lead Vox", "level") == nullptr || change (p, "Lead Vox", "level")->wanted <= 6.0));
        CHECK (move (change (p, "Kick", "level")) < -2.5);
    }
    SECTION ("a protected vocal: the beat makes all the room")
    {
        auto chans = channelsOf (tracks (0));
        for (auto& c : chans) if (c.name == "Lead Vox") c.locked = true;
        const auto p = makePlan (Tool::balance, r, chans);
        CHECK (change (p, "Lead Vox", "level") == nullptr);
        CHECK (move (change (p, "Kick", "level")) == Catch::Approx (-(r.vocalTarget - r.vocalRatio)).margin (0.4));
    }
    SECTION ("a vocal whose name another track has too: never moved, and the plan says why")
    {
        auto chans = channelsOf (tracks (0));
        for (auto& c : chans) if (c.name == "Lead Vox") c.sharedName = true;
        const auto p = makePlan (Tool::autoMix, r, chans);
        CHECK (change (p, "Lead Vox", "level") == nullptr);
        CHECK (change (p, "Lead Vox", "pan") == nullptr);
        CHECK (noted (p, "'Lead Vox': another track has the same name"));
        CHECK_FALSE (noted (p, "'Lead Vox' has no Mixology Link"));
    }
    SECTION ("you like it where it is: nothing moves")
    {
        Analysis a = listen (tracks);
        a.setVocalOffset (r.vocalRatio - r.styleVocalTarget, true);
        const auto p = makePlan (Tool::balance, a.evaluate ({}), channelsOf (tracks (0)));
        CHECK (change (p, "Lead Vox", "level") == nullptr);
        CHECK (change (p, "Kick", "level") == nullptr);
    }
    SECTION ("you find it too loud: it goes down")
    {
        Analysis a = listen (tracks);
        a.setVocalOffset (r.vocalRatio - 2.5 - r.styleVocalTarget, true);
        const auto p = makePlan (Tool::balance, a.evaluate ({}), channelsOf (tracks (0)));
        REQUIRE (change (p, "Lead Vox", "level") != nullptr);
        CHECK (move (change (p, "Lead Vox", "level")) - move (change (p, "Kick", "level")) == Catch::Approx (-2.5).margin (0.3));
        CHECK_FALSE (noted (p, "protected"));
    }
}

TEST_CASE ("Analyze Mix: your ears per vocal track (a verse and a hook on their own tracks)", "[analysis][plan]")
{
    // The verse sings first, the hook after it, each on its own track; the hook sits about 6 dB higher.
    auto tracks = [] (int i) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Hats", hatsFrame()),
        sample ("Verse Vox", i < 75 ? vocalFrame (-20.0) : silentFrame()), sample ("Hook Vox", i >= 75 ? vocalFrame (-14.0) : silentFrame()) }; };
    Analysis a = listen (tracks);
    const auto r0 = a.evaluate ({});
    const auto* verse = track (r0, "Verse Vox");
    const auto* hook = track (r0, "Hook Vox");
    REQUIRE (verse != nullptr);
    REQUIRE (hook != nullptr);
    REQUIRE (verse->hasVocalRatio);
    REQUIRE (hook->hasVocalRatio);
    CHECK (hook->vocalRatio - verse->vocalRatio == Catch::Approx (6.0).margin (0.5));
    CHECK_FALSE (verse->vocalOwn);
    CHECK (verse->vocalTarget == Catch::Approx (r0.vocalTarget));
    CHECK (track (r0, "Kick")->hasVocalRatio == false);

    const auto verseHash = mixology::bridge::nameHash ("Verse Vox");
    const auto hookHash = mixology::bridge::nameHash ("Hook Vox");
    std::map<std::uint32_t, TrackSettings> settings;

    SECTION ("both sound right to you: the advice says so, nothing moves")
    {
        settings[verseHash].vocalOffset = verse->vocalRatio - r0.styleVocalTarget;
        settings[hookHash].vocalOffset = hook->vocalRatio - r0.styleVocalTarget;
        const auto r = a.evaluate (settings);
        const auto* s = find (r, "vocal-ok");
        REQUIRE (s != nullptr);
        CHECK (s->title == "Vocal levels fit your settings");
        CHECK (s->text.find ("'Verse Vox' sits") != std::string::npos);
        CHECK (s->text.find ("this track's setting") != std::string::npos);
        CHECK (mentions (s, "Hook Vox"));
        const auto p = makePlan (Tool::balance, r, channelsOf (tracks (0)));
        CHECK (change (p, "Verse Vox", "level") == nullptr);
        CHECK (change (p, "Hook Vox", "level") == nullptr);
    }
    SECTION ("the verse is too quiet to you, the hook is right: only the verse comes up against the band")
    {
        settings[verseHash].vocalOffset = verse->vocalRatio + 2.5 - r0.styleVocalTarget;
        settings[hookHash].vocalOffset = hook->vocalRatio - r0.styleVocalTarget;
        const auto r = a.evaluate (settings);
        CHECK (track (r, "Verse Vox")->vocalOwn);
        auto move = [] (const mixology::mix::Change* c) { return c == nullptr ? 0.0 : c->wanted - c->from; };
        const auto p = makePlan (Tool::balance, r, channelsOf (tracks (0)));
        const double band = move (change (p, "Kick", "level"));
        CHECK (move (change (p, "Verse Vox", "level")) - band == Catch::Approx (2.5).margin (0.3));
        CHECK (move (change (p, "Hook Vox", "level")) - band == Catch::Approx (0.0).margin (0.3));
        REQUIRE (change (p, "Verse Vox", "level") != nullptr);
        CHECK (change (p, "Verse Vox", "level")->why.find ("'Verse Vox' sits low") != std::string::npos);
        CHECK (change (p, "Verse Vox", "level")->why.find ("for it") != std::string::npos);
    }
    SECTION ("a track far from its own aim gets its own card")
    {
        settings[verseHash].vocalOffset = verse->vocalRatio + 6.0 - r0.styleVocalTarget;
        settings[hookHash].vocalOffset = hook->vocalRatio - r0.styleVocalTarget;   // the hook: right where it is
        const auto r = a.evaluate (settings);
        const auto* s = find (r, "vocal-low");
        REQUIRE (s != nullptr);
        CHECK (s->title == "'Verse Vox' may be buried");
        CHECK (s->kind == "warn");
        CHECK (s->steps.front().find ("Raise 'Verse Vox'") != std::string::npos);
        CHECK (s->steps.front().find ("Hook Vox") == std::string::npos);
        CHECK (mentions (s, "Hook Vox"));   // every vocal is listed: pick one to set it
    }
}

TEST_CASE ("Plans: Balance brings an outlier back, leaves a fitting beat alone", "[plan]")
{
    auto tracks = [] (double hats) { return [=] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Snare", padFrame (-16.0)), sample ("Hats", hatsFrame(), hats) }; }; };
    const auto loud = listen (tracks (12.0)).evaluate ({});
    const auto p = makePlan (Tool::balance, loud, channelsOf (tracks (12.0) (0)));
    const auto* h = change (p, "Hats", "level");
    REQUIRE (h != nullptr);
    const auto* k = change (p, "Kick", "level");
    const double kickMove = k != nullptr ? k->wanted - k->from : 0.0;
    CHECK (h->wanted - h->from < kickMove - 2.0);
    CHECK (h->why.find ("louder") != std::string::npos);
    if (k != nullptr) CHECK (k->why.find ("as loud as before") != std::string::npos);   // only the loudness top-up

    const auto fits = listen (tracks (0.0)).evaluate ({});
    CHECK (makePlan (Tool::balance, fits, channelsOf (tracks (0.0) (0))).changes.empty());
}

namespace
{
    // Mixology's Full Mix as the hub reports it: on the master channel, nothing switched on (or Final Touch at `out`).
    mixology::mix::MixNow fullMix (double finalOut = 0.0, bool finalOn = false)
    {
        mixology::mix::MixNow m;
        m.known = true;
        m.now = mixology::proc::defaults();
        m.now[mixology::proc::finOn] = finalOn ? 1.0f : 0.0f;
        m.now[mixology::proc::finOut] = static_cast<float> (finalOut);
        m.atAnalysis = m.now;
        m.atAnalysisKnown = true;
        return m;
    }

    double procValue (const mixology::mix::Change& c, int id, double otherwise = -999.0)
    {
        for (const auto& [i, v] : c.proc)
            if (i == id) return v;
        return otherwise;
    }
}

TEST_CASE ("Plans: room for Masterology is one gain on the whole mix (the Full Mix's Final Output)", "[plan]")
{
    using namespace mixology::proc;
    auto tracks = [] (int) { return std::vector<TrackSample> { sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Lead Vox", vocalFrame (-12.0)) }; };
    const auto r = listen (tracks, 150, 7.6).evaluate ({});   // peaks at +7.6 dBTP like the first test session
    auto chans = channelsOf (tracks (0));
    chans[1].locked = true;   // a protected channel changes nothing: no fader moves for room

    SECTION ("Final Touch switches on with only its Output moving; every fader stays")
    {
        const auto p = makePlan (Tool::balance, r, chans, fullMix());
        REQUIRE (p.headroomChange.has_value());
        const auto& c = *p.headroomChange;
        CHECK (c.fullMix);
        CHECK (c.kind == "room");
        CHECK (procValue (c, finOn) == 1.0);
        CHECK (procValue (c, finWidth) == 100.0);   // neutral: nothing else in the sound changes
        CHECK (procValue (c, finAir) == 0.0);
        CHECK (procValue (c, finGlue) == 0.0);
        CHECK (procValue (c, finOut) == Catch::Approx (-13.6).margin (0.25));   // +7.6 down to about -6
        CHECK (p.headroomDb == Catch::Approx (procValue (c, finOut)));
        CHECK (p.headroomWhy.find ("Masterology") != std::string::npos);
        CHECK (p.headroomWhy.find ("one gain on the whole mix") != std::string::npos);
        CHECK (p.headroomWhy.find ("13.6 dB quieter until Masterology brings it back") != std::string::npos);
        CHECK (p.headroomWhy.find ("Auto-Master") != std::string::npos);
        for (const auto& ch : p.changes) CHECK (std::abs (ch.wanted - ch.from) < 6.0);   // the level changes stay small
        for (const auto& ch : p.changes) CHECK (ch.kind != "room");                        // offered on its own, unticked
    }
    SECTION ("Final Output already set counts: never applied twice")
    {
        const auto half = makePlan (Tool::balance, r, chans, fullMix (-8.0, true));
        REQUIRE (half.headroomChange.has_value());
        CHECK (half.headroomChange->proc.size() == 1);   // Final Touch is on: only its Output moves
        CHECK (procValue (*half.headroomChange, finOut) == Catch::Approx (-13.6).margin (0.25));
        CHECK (half.headroomDb == Catch::Approx (-5.6).margin (0.25));
        CHECK (half.headroomWhy.find ("from -8.0 dB") != std::string::npos);
        const auto done = makePlan (Tool::balance, r, chans, fullMix (-13.6, true));
        CHECK (done.headroomDb == 0.0);
        CHECK_FALSE (done.headroomChange.has_value());
        // Final Output set while Final Touch is off doesn't count (it isn't playing).
        CHECK (makePlan (Tool::balance, r, chans, fullMix (-13.6, false)).headroomDb < -12.0);
    }
    SECTION ("room made, then the mix got a little louder: not offered again until it passes -3")
    {
        // Peaks at about -4.4 into Masterology (made room to -6, more mixing since): fine for Masterology, no new offer
        // (it came back after every Auto Mix, and Auto-Master had to run again each time).
        const auto louder = makePlan (Tool::balance, r, chans, fullMix (-12.0, true));
        CHECK (louder.headroomDb == 0.0);
        CHECK_FALSE (louder.headroomChange.has_value());
        // Past -3 (about -2.4): offered again, and it says why.
        const auto over = makePlan (Tool::balance, r, chans, fullMix (-10.0, true));
        REQUIRE (over.headroomChange.has_value());
        CHECK (procValue (*over.headroomChange, finOut) == Catch::Approx (-13.6).margin (0.25));
        CHECK (over.headroomWhy.find ("got louder since you made room") != std::string::npos);
        // Never made room: offered from -5 as before (about -4.4 here).
        auto m = fullMix (0.0, false);
        m.peakKnown = true;
        m.peakBeforeFinalDb = -4.4;
        const auto first = listen (tracks, 150, -4.4).evaluate ({});
        CHECK (makePlan (Tool::balance, first, chans, m).headroomChange.has_value());
    }
    SECTION ("the peak where Final Output sits: Mixology's own output since play when it is higher")
    {
        auto m = fullMix();
        m.peakKnown = true;
        m.peakBeforeFinalDb = 10.2;   // the Full Mix processing adds 2.6 dB on the way
        CHECK (procValue (*makePlan (Tool::balance, r, chans, m).headroomChange, finOut) == Catch::Approx (-16.2).margin (0.25));
        m.peakBeforeFinalDb = 3.0;    // a quieter part played since: what Analyze Mix heard is higher, it counts
        CHECK (procValue (*makePlan (Tool::balance, r, chans, m).headroomChange, finOut) == Catch::Approx (-13.6).margin (0.25));
    }
    SECTION ("hotter than Final Output can take: it stops at -24 and says what's left")
    {
        auto m = fullMix();
        m.peakKnown = true;
        m.peakBeforeFinalDb = 22.0;
        const auto p = makePlan (Tool::balance, r, chans, m);
        REQUIRE (p.headroomChange.has_value());
        CHECK (procValue (*p.headroomChange, finOut) == -24.0);
        CHECK (p.headroomWhy.find ("hotter than Final Output can take") != std::string::npos);
        CHECK (p.headroomWhy.find ("still reach about -2.0 dB") != std::string::npos);
        m.now[finOn] = 1.0f;
        m.now[finOut] = -24.0f;
        const auto again = makePlan (Tool::balance, r, chans, m);
        CHECK_FALSE (again.headroomChange.has_value());
        CHECK (noted (again, "Final Output is at its lowest"));
    }
    SECTION ("no Mixology on the master channel: not offered, and the plan says why")
    {
        auto m = fullMix();
        m.onMaster = false;
        const auto p = makePlan (Tool::balance, r, chans, m);
        CHECK (p.headroomDb == 0.0);
        CHECK_FALSE (p.headroomChange.has_value());
        CHECK (noted (p, "only on the {daw:master} channel"));
        const auto none = makePlan (Tool::balance, r, chans);   // no Full Mix at all
        CHECK_FALSE (none.headroomChange.has_value());
        CHECK (noted (none, "no Mixology is there"));
    }
    SECTION ("the Full Mix processing switched off with other modules on: not offered (they would come on too)")
    {
        auto m = fullMix();
        m.now[chainOn] = 0.0f;
        m.now[compOn] = 1.0f;
        const auto p = makePlan (Tool::balance, r, chans, m);
        CHECK_FALSE (p.headroomChange.has_value());
        CHECK (noted (p, "Full Mix processing is switched off"));
        m.now[compOn] = 0.0f;   // nothing else on: the processing comes on for Final Output alone
        const auto q = makePlan (Tool::balance, r, chans, m);
        REQUIRE (q.headroomChange.has_value());
        CHECK (procValue (*q.headroomChange, chainOn) == 1.0);
    }
}

TEST_CASE ("Plans: room for Masterology reaches tracks that didn't play while Analyze Mix listened", "[plan]")
{
    // The user's song: Analyze Mix heard a verse; the chorus vocals were silent then. Moving every fader that played left
    // them out (they came back 15.7 dB louder than the rest). One gain on the Full Mix takes every channel down.
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Drums", kickFrame()), sample ("808", bassFrame()), sample ("Vox Verse 1", vocalFrame (-10.0)),
        sample ("Vox Chorus 1", silentFrame()), sample ("Vox Chorus 2", silentFrame()), sample ("Rhythm 1 Chorus", silentFrame()) }; };
    const auto r = listen (tracks, 150, 9.7).evaluate ({});
    REQUIRE (track (r, "Vox Chorus 1")->activity == 0.0);
    const auto p = makePlan (Tool::autoMix, r, channelsOf (tracks (0)), fullMix());
    REQUIRE (p.headroomChange.has_value());
    CHECK (p.headroomChange->fullMix);
    CHECK (p.headroomDb == Catch::Approx (-15.7).margin (0.25));
    for (const auto& c : p.changes) CHECK (c.name != "Vox Chorus 1");   // no fader of its own to forget
}

TEST_CASE ("Plans: Balance moves channels without a Link with the rest of the beat", "[plan]")
{
    Analysis a;
    a.begin (Style {});
    for (int i = 0; i < 150; ++i)
    {
        Moment m;
        m.tracks = { sample ("Lead Vox", vocalFrame (-30.0)) };
        auto all = m.tracks;
        all.push_back (sample ("Beat", kickFrame()));
        all.push_back (sample ("Beat 808", bassFrame()));
        m.mix = mixOf (all);
        a.add (m);
    }
    const auto r = a.evaluate ({}, { "Beat", "Beat 808" });
    CHECK (r.unheardLoudness > -20.0);
    std::vector<ChannelNow> chans { channel ("Lead Vox"), channel ("Beat", 0.0, 0.0, false), channel ("Beat 808", -2.0, 0.0, false) };
    chans[0].locked = true;   // the vocal stays: the beat channels (no Link) make the room
    const auto p = makePlan (Tool::balance, r, chans);
    const auto* beat = change (p, "Beat", "level");
    const auto* bass = change (p, "Beat 808", "level");
    REQUIRE (beat != nullptr);
    REQUIRE (bass != nullptr);
    CHECK (beat->to < beat->from);
    CHECK (beat->wanted - beat->from == Catch::Approx (bass->wanted - bass->from));   // they keep their balance
}

TEST_CASE ("Plans: a full drum kit on one track weighs like the drums", "[plan]")
{
    // Rock: guitars and one "Drum Machine" track (perc): no kick / snare / hats tracks.
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Drum Machine 01", shaped ([] (int b) { return std::max (bump (b, 5, 2.0, -10.0), bump (b, 24, 3.0, -18.0)); }, 14.0)),
        sample ("Electric Rhythm", padFrame (-12.0)), sample ("Electric Rhythm (D)", padFrame (-12.0)) }; };
    Analysis a;
    Style rock;
    rock.key = "rock.punk";
    rock.name = "Rock - Punk / Pop Punk";
    a.begin (rock);
    for (int i = 0; i < 150; ++i) { Moment m; m.tracks = tracks (i); m.mix = mixOf (m.tracks); a.add (m); }
    const auto r = a.evaluate ({});
    REQUIRE (track (r, "Drum Machine 01")->role == Role::perc);
    const auto p = makePlan (Tool::balance, r, channelsOf (tracks (0)));
    const auto* d = change (p, "Drum Machine 01", "level");
    CHECK ((d == nullptr || std::abs (d->wanted - d->from) < 4.0));   // not pushed down like a shaker
}

TEST_CASE ("Plans: Stereo Architect doesn't trust pans the mix contradicts", "[plan]")
{
    // Cubase says L100 for four guitars, but the Stereo Out is centred (stereo tracks, Combined Panner).
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Vocals", vocalFrame (-12.0)),
        sample ("Electric Rhythm", padFrame (-14.0)), sample ("Electric Rhythm (D)", padFrame (-14.0)),
        sample ("Electric Lead", padFrame (-15.0)), sample ("Electric Lead (D)", padFrame (-15.0)) }; };
    const auto r = listen (tracks).evaluate ({});
    REQUIRE (std::abs (r.mixBalance) < 1.0);
    std::vector<ChannelNow> chans;
    for (const auto& t : tracks (0)) chans.push_back (channel (t.name, 0.0, t.name.rfind ("Electric", 0) == 0 ? -100.0 : 0.0));
    const auto p = makePlan (Tool::stereo, r, chans);
    CHECK (p.changes.empty());
    CHECK (noted (p, "Combined Panner"));
}

TEST_CASE ("Plans: Stereo Architect spreads four guitars panned hard left", "[plan]")
{
    // The first test session: four "Electric" tracks all at L100, vocals and drums in the middle.
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Vocals", vocalFrame (-12.0)), sample ("Vocals (D)", vocalFrame (-18.0)),
        sample ("Electric Rhythm", panned (padFrame (-16.0), 40.0)), sample ("Electric Rhythm (D)", panned (padFrame (-16.0), 40.0)),
        sample ("Electric Lead", panned (padFrame (-17.0), 40.0)), sample ("Electric Lead (D)", panned (padFrame (-17.0), 40.0)) }; };
    const auto r = listen (tracks).evaluate ({});
    REQUIRE (r.mixBalance > 3.0);
    std::vector<ChannelNow> chans;
    for (const auto& t : tracks (0)) chans.push_back (channel (t.name, 0.0, t.name.rfind ("Electric", 0) == 0 ? -100.0 : 0.0));
    chans[0].pan = 30.0;   // the kick off to the right
    const auto p = makePlan (Tool::stereo, r, chans);

    const auto* rh = change (p, "Electric Rhythm", "pan");
    const auto* rhD = change (p, "Electric Rhythm (D)", "pan");
    const auto* ld = change (p, "Electric Lead", "pan");
    const auto* ldD = change (p, "Electric Lead (D)", "pan");
    // Each pair: the original stays hard left, its double mirrors it hard right.
    CHECK (rh == nullptr);
    REQUIRE (rhD != nullptr);
    CHECK (rhD->to == Catch::Approx (100.0));
    CHECK (ld == nullptr);
    REQUIRE (ldD != nullptr);
    CHECK (ldD->to == Catch::Approx (100.0));
    CHECK (rhD->why.find ("pairs with 'Electric Rhythm'") != std::string::npos);
    REQUIRE (change (p, "Kick", "pan") != nullptr);
    CHECK (change (p, "Kick", "pan")->to == Catch::Approx (0.0));
    CHECK (change (p, "Vocals", "pan") == nullptr);        // already centred
    CHECK (change (p, "Vocals (D)", "pan") == nullptr);    // a single double stays with the lead
}

TEST_CASE ("Plans: Stereo Architect evens out a lopsided mix", "[plan]")
{
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Lead Vox", vocalFrame (-12.0)),
        sample ("Piano", panned (padFrame (-14.0), 30.0)), sample ("Strings", panned (padFrame (-15.0), 25.0)) }; };
    const auto r = listen (tracks).evaluate ({});
    std::vector<ChannelNow> chans { channel ("Kick"), channel ("Lead Vox"), channel ("Piano", 0.0, -90.0), channel ("Strings", 0.0, -70.0) };
    const auto p = makePlan (Tool::stereo, r, chans);
    REQUIRE (p.changes.size() >= 1);
    bool movedRight = false;
    for (const auto& c : p.changes) movedRight = movedRight || (c.kind == "pan" && c.to > c.from + 20.0 && c.why.find ("leans left") != std::string::npos);
    CHECK (movedRight);
}

TEST_CASE ("Plans: Stereo Architect centres a mono track that pulls the mix, never mirrors it", "[plan]")
{
    // Mixology Link sits before the panner: a mono track's Link hears no lean at all, the panner puts it left.
    auto tr = [] (const char* n, std::uint32_t h, Role role, double loud) {
        TrackReport t;
        t.name = n; t.hash = h; t.role = role; t.roleFrom = "you"; t.loudness = loud; t.activity = 1.0; t.balance = 0.0;
        t.bands.assign (kBands, -60.0);
        return t;
    };
    auto ch = [] (const char* n, std::uint32_t h, double pan) {
        ChannelNow c;
        c.name = n; c.hash = h; c.faderKnown = true; c.hasPan = true; c.pan = pan; c.channels = 1; c.hasLink = true;
        return c;
    };
    Report r;
    r.valid = true;
    r.style.name = "Test";
    r.tracks = { tr ("Kick", 1, Role::kick, -14.0), tr ("Lead Vox", 2, Role::vox, -12.0), tr ("Synth", 3, Role::synth, -11.1) };
    const double others = std::pow (10.0, -1.4) + std::pow (10.0, -1.2), synth = std::pow (10.0, -1.11);
    r.mixBalance = 10.0 * std::log10 ((others + 2.0 * synth) / others);   // what the Stereo Out hears with Synth at L100
    REQUIRE (r.mixBalance > 3.0);
    const auto p = makePlan (Tool::stereo, r, { ch ("Kick", 1, 0.0), ch ("Lead Vox", 2, 0.0), ch ("Synth", 3, -100.0) });
    const auto* c = change (p, "Synth", "pan");
    REQUIRE (c != nullptr);
    CHECK (c->to > -60.0);   // towards the middle ...
    CHECK (c->to < 30.0);    // ... never mirrored to the other side (that leans the mix right just as much)
}

TEST_CASE ("Plans: Clean Up cuts rumble and dips mud, never the kick", "[plan]")
{
    // A vocal with rumble, and a muddy pad.
    auto rumbleVox = shaped ([] (int b) { return std::max (bump (b, 19, 4.0, -14.0), bump (b, 3, 2.0, -16.0)); }, 10.0, 0.02);
    auto muddyPad = shaped ([] (int b) { return bump (b, 12, 2.5, -10.0); }, 5.0, 0.3);
    auto tracks = [=] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Lead Vox", rumbleVox), sample ("Pad", muddyPad) }; };
    const auto r = listen (tracks).evaluate ({});
    auto chans = channelsOf (tracks (0));
    const auto p = makePlan (Tool::cleanUp, r, chans);
    const auto* lc = change (p, "Lead Vox", "lowcut");
    REQUIRE (lc != nullptr);
    CHECK (lc->to == Catch::Approx (90.0));
    CHECK (lc->from == 0.0);
    const auto* mud = change (p, "Pad", "mud");
    REQUIRE (mud != nullptr);
    CHECK (mud->to <= -1.5);
    CHECK (mud->hz >= 190.0);
    CHECK (mud->hz <= 520.0);
    CHECK (change (p, "Kick", "lowcut") == nullptr);
    CHECK (change (p, "Kick", "mud") == nullptr);

    // Already done: nothing proposed twice. Without a Link: a note instead.
    for (auto& c : chans)
    {
        if (c.name == "Lead Vox") c.assist.lowCutHz = 100.0f;
        if (c.name == "Pad") c.hasLink = false;
    }
    const auto p2 = makePlan (Tool::cleanUp, r, chans);
    CHECK (change (p2, "Lead Vox", "lowcut") == nullptr);
    CHECK (change (p2, "Pad", "mud") == nullptr);
    CHECK (noted (p2, "'Pad' has no Mixology Link"));
}

TEST_CASE ("Plans: Vocal Pocket dips what covers the vocal's words", "[plan]")
{
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Lead Vox", vocalFrame (-16.0)),
        sample ("Synth Lead", shaped ([] (int b) { return bump (b, 21, 3.0, -15.0); }, 6.0, 0.2)), sample ("Hats", hatsFrame()) }; };
    const auto r = listen (tracks).evaluate ({});
    auto chans = channelsOf (tracks (0));
    const auto p = makePlan (Tool::pocket, r, chans);
    const auto* d = change (p, "Synth Lead", "pocket");
    REQUIRE (d != nullptr);
    CHECK (d->to <= -2.0);
    CHECK (d->hz >= 1550.0);   // inside 1.6 - 4 kHz, where the advice measures it
    CHECK (d->hz <= 4100.0);
    CHECK (change (p, "Lead Vox", "pocket") == nullptr);
    CHECK (change (p, "Kick", "pocket") == nullptr);

    for (auto& c : chans) if (c.name == "Synth Lead") c.locked = true;
    const auto p2 = makePlan (Tool::pocket, r, chans);
    CHECK (change (p2, "Synth Lead", "pocket") == nullptr);
    CHECK (noted (p2, "protected"));
}

TEST_CASE ("Plans: Vocal Pocket and the dip it heard (the advice and the tool agree)", "[plan]")
{
    // Bright hats over the vocal: their energy rises above 4 kHz, but the dip goes where the words are.
    auto bright = [] (int) { return shaped ([] (int b) { return bump (b, 24, 4.0, -15.0); }, 12.0, 0.2); };
    auto tracks = [&] (int i) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Lead Vox", vocalFrame (-16.0)), sample ("Hats - Verse", bright (i)) }; };
    const auto r = listen (tracks).evaluate ({});
    REQUIRE (find (r, "vocal-pocket") != nullptr);
    REQUIRE (mentions (find (r, "vocal-pocket"), "Hats - Verse"));
    auto chans = channelsOf (tracks (0));
    auto& hats = *std::find_if (chans.begin(), chans.end(), [] (const ChannelNow& c) { return c.name == "Hats - Verse"; });
    const auto p = makePlan (Tool::pocket, r, chans);
    const auto* d = change (p, "Hats - Verse", "pocket");
    REQUIRE (d != nullptr);
    CHECK (d->hz <= 4100.0);
    const double depth = d->to;

    SECTION ("made since Mixology listened: nothing more to do")
    {
        hats.assistAtAnalysisKnown = true;   // heard without a dip
        hats.assist.pocketDb = static_cast<float> (depth);
        hats.assist.pocketHz = static_cast<float> (d->hz);
        const auto again = makePlan (Tool::pocket, r, chans);
        CHECK (change (again, "Hats - Verse", "pocket") == nullptr);
        CHECK (again.changes.empty());
        CHECK (again.summary.find ("the dips are in since Mixology listened") != std::string::npos);
    }
    SECTION ("heard with that dip and still covering: a deeper one")
    {
        hats.assistAtAnalysisKnown = true;
        hats.assistAtAnalysis.pocketDb = -2.0f;
        hats.assistAtAnalysis.pocketHz = static_cast<float> (d->hz);
        hats.assist = hats.assistAtAnalysis;
        const auto again = makePlan (Tool::pocket, r, chans);
        const auto* deeper = change (again, "Hats - Verse", "pocket");
        REQUIRE (deeper != nullptr);
        CHECK (deeper->to < -2.5);
        CHECK (deeper->to >= -6.0);
        CHECK (deeper->why.find ("already has") != std::string::npos);
    }
    SECTION ("heard with a dip above 4 kHz (older Mixology): moved to where the words are")
    {
        hats.assistAtAnalysisKnown = true;
        hats.assistAtAnalysis.pocketDb = -2.0f;
        hats.assistAtAnalysis.pocketHz = 5000.0f;
        hats.assist = hats.assistAtAnalysis;
        const auto again = makePlan (Tool::pocket, r, chans);
        const auto* moved = change (again, "Hats - Verse", "pocket");
        REQUIRE (moved != nullptr);
        CHECK (moved->hz <= 4100.0);
    }
    // A dip already where the words are, but not at the band with the biggest overlap now (the dip itself moved it
    // there): it stays where it is and only gets deeper, never moved or made shallower (it went back and forth between
    // Auto Mix runs).
    const double elsewhere = d->hz < 2500.0 ? 3500.0 : 1600.0;
    SECTION ("heard with a dip elsewhere in the words' range and still covering: deeper where it is")
    {
        hats.assistAtAnalysisKnown = true;
        hats.assistAtAnalysis.pocketDb = -2.0f;
        hats.assistAtAnalysis.pocketHz = static_cast<float> (elsewhere);
        hats.assist = hats.assistAtAnalysis;
        const auto again = makePlan (Tool::pocket, r, chans);
        const auto* deeper = change (again, "Hats - Verse", "pocket");
        REQUIRE (deeper != nullptr);
        CHECK (deeper->hz == Catch::Approx (elsewhere));
        CHECK (deeper->fromHz == Catch::Approx (elsewhere));
        CHECK (deeper->to < -2.5);
    }
    SECTION ("a dip made since elsewhere in the words' range: kept as it is, not moved shallower")
    {
        hats.assistAtAnalysisKnown = true;   // heard without a dip
        hats.assist.pocketDb = -4.0f;
        hats.assist.pocketHz = static_cast<float> (elsewhere);
        const auto again = makePlan (Tool::pocket, r, chans);
        CHECK (change (again, "Hats - Verse", "pocket") == nullptr);
    }
    SECTION ("advice from an older Mixology (the Assist EQ it heard isn't known): the dip there now was heard")
    {
        hats.assistAtAnalysisKnown = false;
        hats.assist.pocketDb = -2.0f;
        hats.assist.pocketHz = static_cast<float> (d->hz);
        const auto again = makePlan (Tool::pocket, r, chans);
        const auto* deeper = change (again, "Hats - Verse", "pocket");
        REQUIRE (deeper != nullptr);
        CHECK (deeper->to < -2.5);
        CHECK (again.summary.find ("dips are in") == std::string::npos);
    }
    SECTION ("... and its dip above 4 kHz doesn't count: moved to where the words are")
    {
        hats.assistAtAnalysisKnown = false;
        hats.assist.pocketDb = -2.0f;
        hats.assist.pocketHz = 5000.0f;
        const auto again = makePlan (Tool::pocket, r, chans);
        const auto* moved = change (again, "Hats - Verse", "pocket");
        REQUIRE (moved != nullptr);
        CHECK (moved->hz <= 4100.0);
    }
    SECTION ("heard with the deepest dip already: a note instead")
    {
        hats.assistAtAnalysisKnown = true;
        hats.assistAtAnalysis.pocketDb = -6.0f;
        hats.assistAtAnalysis.pocketHz = static_cast<float> (d->hz);
        hats.assist = hats.assistAtAnalysis;
        const auto again = makePlan (Tool::pocket, r, chans);
        CHECK (change (again, "Hats - Verse", "pocket") == nullptr);
        CHECK (noted (again, "still covers the vocal"));
    }
}

TEST_CASE ("Plans: Auto Mix puts all four together", "[plan]")
{
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()),
        sample ("Lead Vox", shaped ([] (int b) { return std::max (bump (b, 19, 4.0, -28.0), bump (b, 3, 2.0, -30.0)); }, 10.0, 0.02)),
        sample ("Gtr L", panned (padFrame (-16.0), 40.0)), sample ("Gtr R", panned (padFrame (-16.0), 40.0)) }; };
    const auto r = listen (tracks).evaluate ({});
    std::vector<ChannelNow> chans;
    for (const auto& t : tracks (0)) chans.push_back (channel (t.name, 0.0, t.name.rfind ("Gtr", 0) == 0 ? -100.0 : 0.0));
    const auto p = makePlan (Tool::autoMix, r, chans);
    CHECK (p.title == "Auto Mix");
    CHECK (change (p, "Lead Vox", "level") != nullptr);
    REQUIRE (change (p, "Gtr R", "pan") != nullptr);
    CHECK (change (p, "Gtr R", "pan")->to == Catch::Approx (100.0));   // "Gtr L" stays hard left
    CHECK (change (p, "Lead Vox", "lowcut") != nullptr);
    CHECK (p.summary.find ("levels") != std::string::npos);
}

TEST_CASE ("Reference Match: your mix against your reference song", "[analysis]")
{
    // The reference plays under the song (muted in Cubase: only its Link hears it), with much more low end.
    auto refFrame = shaped ([] (int b) { return std::max (bump (b, 4, 3.0, -6.0), bump (b, 18, 8.0, -14.0)); }, 8.0, 0.3);
    Analysis a;
    a.begin (Style {});
    for (int i = 0; i < 150; ++i)
    {
        Moment m;
        m.tracks = { sample ("Hats", hatsFrame()), sample ("Lead Vox", vocalFrame (-12.0)), sample ("Pad", padFrame (-16.0)), sample ("Ref Song", refFrame) };
        m.mix = mixOf ({ m.tracks[0], m.tracks[1], m.tracks[2] });   // muted: not on the Stereo Out
        a.add (m);
    }
    std::map<std::uint32_t, TrackSettings> settings;
    settings[mixology::bridge::nameHash ("Ref Song")].reference = true;
    const auto r = a.evaluate (settings);
    REQUIRE (r.reference.chosen);
    REQUIRE (r.reference.heard);
    CHECK (r.reference.name == "Ref Song");
    CHECK (track (r, "Ref Song")->excluded);                    // never part of the mix advice
    for (const auto& s : r.suggestions) CHECK_FALSE (mentions (&s, "Ref Song"));
    REQUIRE (r.reference.regions.size() == 5);
    CHECK (r.reference.regions[0].diff < -3.0);                 // your mix has less low end
    CHECK (r.reference.regions[0].text.find ("less low end") != std::string::npos);
    CHECK (r.reference.loudnessDiff > 0.0);

    // Not heard (placed elsewhere on the timeline): said so.
    Analysis b;
    b.begin (Style {});
    for (int i = 0; i < 50; ++i)
    {
        Moment m;
        m.tracks = { sample ("Kick", kickFrame()), sample ("Ref Song", silentFrame()) };
        m.mix = mixOf ({ m.tracks[0] });
        b.add (m);
    }
    const auto r2 = b.evaluate (settings);
    CHECK (r2.reference.chosen);
    CHECK_FALSE (r2.reference.heard);
}

TEST_CASE ("Plans: stereo tracks on Cubase's Combined Panner are never panned", "[plan]")
{
    // Cubase reports "L100" for a full-width stereo track on the Combined Panner (its left edge only).
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Vocals", vocalFrame (-12.0)),
        sample ("Electric Rhythm", padFrame (-14.0)), sample ("Electric Rhythm (D)", padFrame (-14.0)) }; };
    const auto r = listen (tracks).evaluate ({});
    std::vector<ChannelNow> chans;
    for (const auto& t : tracks (0))
    {
        chans.push_back (channel (t.name, 0.0, t.name.rfind ("Electric", 0) == 0 ? -100.0 : 0.0));
        chans.back().panUnknown = t.name.rfind ("Electric", 0) == 0;
    }
    chans[0].pan = 40.0;   // the kick is really off-centre: that one still gets fixed
    const auto p = makePlan (Tool::stereo, r, chans);
    CHECK (change (p, "Electric Rhythm", "pan") == nullptr);
    CHECK (change (p, "Electric Rhythm (D)", "pan") == nullptr);
    REQUIRE (change (p, "Kick", "pan") != nullptr);
    CHECK (noted (p, "Stereo Combined Panner"));
    CHECK_FALSE (noted (p, "probably stereo tracks"));   // the pans agree with the mix once those are centred
}

TEST_CASE ("Plans: applying a plan and opening it again doesn't repeat its moves", "[plan]")
{
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Hats", hatsFrame(), 12.0), sample ("Lead Vox", vocalFrame (-22.0), -6.0) }; };
    const auto r = listen (tracks).evaluate ({});
    auto chans = channelsOf (tracks (0));
    for (auto& c : chans) { c.hasFaderAtAnalysis = true; c.faderAtAnalysis = c.faderDb; }
    const auto first = makePlan (Tool::balance, r, chans);
    REQUIRE (first.changes.size() >= 2);

    // Apply it (the faders move as planned), then plan again from the same Analyze Mix.
    for (auto& c : chans)
        for (const auto& ch : first.changes)
            if (ch.name == c.name && ch.kind == "level") c.faderDb = ch.wanted;   // "Allow moves over 6 dB" was on
    const auto again = makePlan (Tool::balance, r, chans);
    for (const auto& c : again.changes)
        CHECK (std::abs (c.wanted - c.from) < 1.0);   // at most a small leftover, never the same moves again
    CHECK (std::any_of (again.notes.begin(), again.notes.end(), [] (const std::string& n) { return n.find ("moved since Analyze Mix") != std::string::npos; }));
}

TEST_CASE ("Plans: a big channel without a Link: the instruments aren't judged against each other", "[plan]")
{
    Analysis a;
    a.begin (Style {});
    for (int i = 0; i < 150; ++i)
    {
        Moment m;
        m.tracks = { sample ("Lead Vox", vocalFrame (-14.0)), sample ("Gtr", padFrame (-14.0)), sample ("Drum Machine", kickFrame()) };
        auto all = m.tracks;
        all.push_back (sample ("Beat", bassFrame()));
        m.mix = mixOf (all);
        a.add (m);
    }
    const auto r = a.evaluate ({}, { "Beat" });
    const auto p = makePlan (Tool::balance, r, { channel ("Lead Vox"), channel ("Gtr"), channel ("Drum Machine"), channel ("Beat", 0.0, 0.0, false) });
    for (const auto& c : p.changes) CHECK (c.why.find ("against the rest of the band than") == std::string::npos);
    CHECK (noted (p, "'Beat' has no Mixology Link"));
}

namespace
{
    // ---- test signals for name pairs ---------------------------------------------------------------------------
    // A voice: syllables (0.10 - 0.30 s, rap) in phrases of 2 - 4 s with breaths between, its level envelope at 1 ms,
    // measured as Mixology Link does: 85 ms of audio every 200 ms, each track on the shared grid give or take 30 ms
    // (the host's buffers, the analysis thread). Own random numbers (mt19937's are the same everywhere; the standard
    // distributions aren't), so every compiler hears the same songs.
    struct VoiceRng
    {
        std::mt19937 g;
        explicit VoiceRng (unsigned seed) : g (seed) {}
        double uniform (double lo, double hi) { return lo + (hi - lo) * (static_cast<double> (g()) / 4294967296.0); }
        double normal (double sigma)
        {
            const double u1 = std::max (1.0e-12, uniform (0.0, 1.0)), u2 = uniform (0.0, 1.0);
            return sigma * std::sqrt (-2.0 * std::log (u1)) * std::cos (2.0 * kPi * u2);
        }
    };
    struct Syllable { double t0, t1, amp; };
    constexpr double kSongSeconds = 40.0;

    std::vector<std::pair<double, double>> phrasesOf (VoiceRng& rng)
    {
        std::vector<std::pair<double, double>> out;
        for (double t = 0.5; t < kSongSeconds - 0.5;)
        {
            const double len = std::min (rng.uniform (2.0, 4.0), kSongSeconds - 0.5 - t);
            out.push_back ({ t, t + len });
            t += len + rng.uniform (0.3, 0.8);
        }
        return out;
    }
    std::vector<Syllable> syllablesIn (VoiceRng& rng, double from, double to)
    {
        std::vector<Syllable> out;
        for (double t = from;;)
        {
            const double d = rng.uniform (0.10, 0.30), gap = rng.uniform (0.02, 0.08);
            if (t + d > to) break;
            out.push_back ({ t, t + d, std::pow (10.0, rng.normal (2.0) / 20.0) });
            t += d + gap;
        }
        return out;
    }
    // Amplitude per ms; a second take: each syllable a little early or late (jitter, s), quieter, its level varied.
    std::vector<double> sing (const std::vector<Syllable>& syl, VoiceRng& rng, double jitter = 0.0, double levelDb = 0.0, double varyDb = 0.0)
    {
        std::vector<double> env (static_cast<std::size_t> (kSongSeconds * 1000.0), 0.0);
        for (auto s : syl)
        {
            if (jitter > 0.0) { s.t0 += rng.normal (jitter); s.t1 += rng.normal (jitter); }
            if (varyDb > 0.0) s.amp *= std::pow (10.0, rng.normal (varyDb) / 20.0);
            const int i0 = std::max (0, static_cast<int> (s.t0 * 1000.0)), i1 = std::min (static_cast<int> (env.size()), static_cast<int> (s.t1 * 1000.0));
            for (int i = i0; i < i1; ++i)
            {
                const double x = static_cast<double> (i - i0) / std::max (1, i1 - i0);
                env[static_cast<std::size_t> (i)] = std::max (env[static_cast<std::size_t> (i)], s.amp * std::pow (10.0, levelDb / 20.0) * std::min (1.0, x * 8.0) * (1.0 - 0.5 * x));
            }
        }
        return env;
    }
    std::vector<double> measured (const std::vector<double>& env, double phase)
    {
        std::vector<double> out;
        for (int k = 0;; ++k)
        {
            const auto i0 = static_cast<std::size_t> ((0.2 * k + phase) * 1000.0), i1 = i0 + 85;
            if (i1 > env.size()) break;
            double e = 0.0;
            for (auto i = i0; i < i1; ++i) e += env[i] * env[i];
            const double rms = std::sqrt (e / 85.0 * 0.5);
            out.push_back (rms > 1.0e-5 ? 20.0 * std::log10 (rms) : -120.0);
        }
        return out;
    }

    enum class Duet { tightDouble, looseDouble, sameTimeOtherLines, callAndResponse, chorusHalves, leadAndAdlibs, notHeard };

    // Two takes' levels per moment, as the Links heard them.
    std::pair<std::vector<double>, std::vector<double>> duet (Duet kind, unsigned seed)
    {
        VoiceRng rng (seed);
        const auto ph = phrasesOf (rng);
        const double phaseA = rng.uniform (0.0, 0.03), phaseB = rng.uniform (0.0, 0.03);
        std::vector<Syllable> a, b;
        auto all = [&] (std::vector<Syllable>& to, auto keep) {
            for (std::size_t i = 0; i < ph.size(); ++i)
                if (keep (i, ph[i])) for (const auto& s : syllablesIn (rng, ph[i].first, ph[i].second)) to.push_back (s);
        };
        std::vector<double> envA, envB;
        switch (kind)
        {
            case Duet::tightDouble:
                all (a, [] (std::size_t, auto) { return true; });
                envA = sing (a, rng);
                envB = sing (a, rng, 0.025, -2.0, 1.5);
                break;
            case Duet::looseDouble:
                all (a, [] (std::size_t, auto) { return true; });
                envA = sing (a, rng);
                envB = sing (a, rng, 0.05, -4.0, 3.0);
                break;
            case Duet::sameTimeOtherLines:
                all (a, [] (std::size_t, auto) { return true; });
                all (b, [] (std::size_t, auto) { return true; });
                envA = sing (a, rng);
                envB = sing (b, rng, 0.0, -2.0);
                break;
            case Duet::callAndResponse:
                all (a, [] (std::size_t i, auto) { return i % 2 == 0; });
                all (b, [] (std::size_t i, auto) { return i % 2 == 1; });
                envA = sing (a, rng);
                envB = sing (b, rng);
                break;
            case Duet::chorusHalves:   // part 1 sings 8 s, then part 2 sings 8 s
                all (a, [] (std::size_t, auto p) { return static_cast<int> (p.first / 8.0) % 2 == 0; });
                all (b, [] (std::size_t, auto p) { return static_cast<int> (p.first / 8.0) % 2 == 1; });
                envA = sing (a, rng);
                envB = sing (b, rng);
                break;
            case Duet::leadAndAdlibs:
                all (a, [] (std::size_t, auto) { return true; });
                for (const auto& p : ph)
                    if (rng.uniform (0.0, 1.0) < 0.6)
                    {
                        const double t = rng.uniform (p.first, p.second);
                        for (const auto& s : syllablesIn (rng, t, std::min (kSongSeconds, t + rng.uniform (0.4, 1.2)))) b.push_back (s);
                    }
                envA = sing (a, rng);
                envB = sing (b, rng, 0.0, -6.0);
                break;
            case Duet::notHeard:   // the second part sings in a section Analyze Mix didn't hear
                all (a, [] (std::size_t, auto) { return true; });
                envA = sing (a, rng);
                envB.assign (envA.size(), 0.0);
                break;
        }
        return { measured (envA, phaseA), measured (envB, phaseB) };
    }

    AnalysisFrame voiceFrame (double db) { return db < -90.0 ? silentFrame() : vocalFrame (db - 6.0); }

    // A short song with a kick, a lead vocal and the two takes on their own tracks (levels before the fader).
    Report duetReport (Duet kind, unsigned seed, const std::string& nameA, const std::string& nameB)
    {
        const auto [la, lb] = duet (kind, seed);
        Analysis a;
        a.begin (Style {});
        for (std::size_t i = 0; i < std::min (la.size(), lb.size()); ++i)
        {
            Moment m;
            m.tracks = { sample ("Kick", kickFrame()), sample ("Lead Vox", vocalFrame (-14.0)), sample (nameA, voiceFrame (la[i])), sample (nameB, voiceFrame (lb[i])) };
            m.mix = mixOf (m.tracks);
            a.add (m);
        }
        return a.evaluate ({});
    }

    PairCheck::Verdict verdictFor (Duet kind, unsigned seed, PairCheck* out = nullptr)
    {
        const auto r = duetReport (kind, seed, "Backing Vox 1", "Backing Vox 2");
        const auto* c = pairCheck (r, mixology::bridge::nameHash ("Backing Vox 1"), mixology::bridge::nameHash ("Backing Vox 2"));
        REQUIRE (c != nullptr);
        if (out != nullptr) *out = *c;
        return c->verdict;
    }
}

TEST_CASE ("Doubles by ear: only a real double plays together and moves together", "[plan][pairs]")
{
    // The user's chorus: 'Vox Chorus 1' and 'Vox Chorus 2' are different parts, not the same lines, yet Stereo
    // Architect panned them left and right because of their names. Now the names only say who MIGHT pair.
    using V = PairCheck::Verdict;
    auto count = [] (Duet kind, V verdict) {
        int n = 0;
        for (unsigned seed = 1; seed <= 12; ++seed) n += verdictFor (kind, seed) == verdict ? 1 : 0;
        return n;
    };
    CHECK (count (Duet::tightDouble, V::together) == 12);       // the same lines, sung twice
    CHECK (count (Duet::looseDouble, V::together) >= 10);       // sloppier doubles (50 ms, 3 dB off): nearly all still count
    CHECK (count (Duet::sameTimeOtherLines, V::together) == 0); // two lines at once: never a double
    CHECK (count (Duet::callAndResponse, V::apart) == 12);
    CHECK (count (Duet::chorusHalves, V::apart) == 12);
    CHECK (count (Duet::leadAndAdlibs, V::apart) == 12);
    CHECK (count (Duet::notHeard, V::notHeard) == 12);

    PairCheck c;
    verdictFor (Duet::tightDouble, 3, &c);
    CHECK (c.overlap > 0.85);
    CHECK (c.alike > 0.5);
    verdictFor (Duet::callAndResponse, 3, &c);
    CHECK (c.overlap < 0.1);
    // Steady levels (a pad and its double, or test tones) are alike; a name with no number, side or double tag isn't a pair.
    const auto r = listen ([] (int) { return std::vector<TrackSample> { sample ("Pad", padFrame (-20.0)), sample ("Pad (D)", padFrame (-22.0)),
                                                                          sample ("Keys", padFrame (-20.0)), sample ("keys ", padFrame (-20.0)) }; }).evaluate ({});
    const auto* pads = pairCheck (r, mixology::bridge::nameHash ("Pad"), mixology::bridge::nameHash ("Pad (D)"));
    REQUIRE (pads != nullptr);
    CHECK (pads->verdict == V::together);
    CHECK (pairCheck (r, mixology::bridge::nameHash ("Keys"), mixology::bridge::nameHash ("keys ")) == nullptr);
}

TEST_CASE ("Doubles by ear: print the measurements (manual check)", "[.pairsprint]")
{
    for (auto kind : { Duet::tightDouble, Duet::looseDouble, Duet::sameTimeOtherLines, Duet::callAndResponse, Duet::chorusHalves, Duet::leadAndAdlibs, Duet::notHeard })
    {
        std::vector<double> ov, al;
        int verdicts[4] = {};
        for (unsigned seed = 1; seed <= 40; ++seed)
        {
            PairCheck c;
            ++verdicts[static_cast<int> (verdictFor (kind, seed, &c))];
            ov.push_back (c.overlap);
            al.push_back (c.alike);
        }
        std::sort (ov.begin(), ov.end());
        std::sort (al.begin(), al.end());
        printf ("kind %d  overlap %.2f %.2f %.2f   alike %.2f %.2f %.2f   (min / median / max)   not heard %d, apart %d, unlike %d, together %d of 40\n",
                static_cast<int> (kind), ov.front(), ov[20], ov.back(), al.front(), al[20], al.back(), verdicts[0], verdicts[1], verdicts[2], verdicts[3]);
    }
}

TEST_CASE ("Plans: Stereo Architect spreads a double heard as one, and leaves other name pairs alone", "[plan][pairs]")
{
    auto chans = [] (double pan2) {
        std::vector<ChannelNow> c { channel ("Kick"), channel ("Lead Vox"), channel ("Backing Vox 1"), channel ("Backing Vox 2", 0.0, pan2) };
        return c;
    };
    SECTION ("a real double: one left, one right")
    {
        const auto r = duetReport (Duet::tightDouble, 5, "Backing Vox 1", "Backing Vox 2");
        const auto p = makePlan (Tool::stereo, r, chans (0.0));
        const auto* a = change (p, "Backing Vox 1", "pan");
        const auto* b = change (p, "Backing Vox 2", "pan");
        REQUIRE (a != nullptr);
        REQUIRE (b != nullptr);
        CHECK (a->to * b->to < 0.0);
        CHECK (a->why.find ("a double: Mixology heard them play together") != std::string::npos);
    }
    SECTION ("two parts of the chorus (they take turns): the pans stay, and a note says why")
    {
        const auto r = duetReport (Duet::chorusHalves, 5, "Backing Vox 1", "Backing Vox 2");
        const auto p = makePlan (Tool::stereo, r, chans (0.0));
        CHECK (change (p, "Backing Vox 1", "pan") == nullptr);
        CHECK (change (p, "Backing Vox 2", "pan") == nullptr);
        CHECK (noted (p, "'Backing Vox 1' and 'Backing Vox 2' look like a pair by name, but they sing at different times (different parts), so their pans stay."));
    }
    SECTION ("two lines sung at the same time: the pans stay")
    {
        const auto r = duetReport (Duet::sameTimeOtherLines, 5, "Backing Vox 1", "Backing Vox 2");
        const auto p = makePlan (Tool::autoMix, r, chans (0.0));
        CHECK (change (p, "Backing Vox 1", "pan") == nullptr);
        CHECK (change (p, "Backing Vox 2", "pan") == nullptr);
        CHECK (noted (p, "not the same line"));
    }
    SECTION ("the second part wasn't heard (it sings in another section): its pan stays, play that part")
    {
        const auto r = duetReport (Duet::notHeard, 5, "Backing Vox 1", "Backing Vox 2");
        const auto p = makePlan (Tool::stereo, r, chans (0.0));
        CHECK (change (p, "Backing Vox 1", "pan") == nullptr);
        CHECK (noted (p, "didn't hear them sing together: play the part where both sing and Analyze Mix again"));
    }
    SECTION ("left and right typed in the names count by name")
    {
        const auto r = duetReport (Duet::chorusHalves, 5, "Gtr L", "Gtr R");
        std::vector<ChannelNow> c { channel ("Kick"), channel ("Lead Vox"), channel ("Gtr L"), channel ("Gtr R") };
        const auto p = makePlan (Tool::stereo, r, c);
        REQUIRE (change (p, "Gtr L", "pan") != nullptr);
        CHECK (change (p, "Gtr L", "pan")->to < 0.0);
        CHECK (change (p, "Gtr R", "pan")->to > 0.0);
        CHECK (change (p, "Gtr L", "pan")->why.find ("left and right in their names") != std::string::npos);
    }
    SECTION ("the user's chorus: 'Vox Chorus 2' panned by an older Mixology comes back to the middle")
    {
        // Two parts of the chorus taking turns are lead vocal parts: the one panned R35 goes back to the centre.
        const auto r = duetReport (Duet::chorusHalves, 7, "Vox Chorus 1", "Vox Chorus 2");
        REQUIRE (track (r, "Vox Chorus 2")->role == Role::vox);
        std::vector<ChannelNow> c { channel ("Kick"), channel ("Lead Vox"), channel ("Vox Chorus 1", 0.0, -35.0), channel ("Vox Chorus 2", 0.0, 35.0) };
        const auto p = makePlan (Tool::stereo, r, c);
        const auto* one = change (p, "Vox Chorus 1", "pan");
        const auto* two = change (p, "Vox Chorus 2", "pan");
        REQUIRE (one != nullptr);
        REQUIRE (two != nullptr);
        CHECK (one->to == Catch::Approx (0.0));
        CHECK (two->to == Catch::Approx (0.0));
        CHECK (two->why.find ("a lead vocal part") != std::string::npos);
        // A real double of the chorus: the original stays in the middle, a single double stays with it (as before).
        const auto d = duetReport (Duet::tightDouble, 7, "Vox Chorus 1", "Vox Chorus 2");
        const auto q = makePlan (Tool::stereo, d, { channel ("Kick"), channel ("Lead Vox"), channel ("Vox Chorus 1"), channel ("Vox Chorus 2") });
        CHECK (change (q, "Vox Chorus 2", "pan") == nullptr);
    }
}

TEST_CASE ("Plans: Stereo Architect only pans mono tracks", "[plan]")
{
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Gtr L", padFrame (-16.0)), sample ("Gtr R", padFrame (-16.0)) }; };
    const auto r = listen (tracks).evaluate ({});
    auto chans = channelsOf (tracks (0));
    for (auto& c : chans) if (c.name.rfind ("Gtr", 0) == 0) c.channels = 2;   // stereo tracks
    chans[0].pan = 40.0;
    const auto p = makePlan (Tool::stereo, r, chans);
    CHECK (change (p, "Gtr L", "pan") == nullptr);
    CHECK (change (p, "Gtr R", "pan") == nullptr);
    CHECK (change (p, "Kick", "pan") != nullptr);   // mono: still centred
    CHECK (noted (p, "only pans mono tracks"));
}

TEST_CASE ("Music group: the instruments play through one group fader (Voxology's 'Beat')", "[plan]")
{
    // Vocals on their own; guitars and drums play through the "Beat" group (no Link on it).
    auto tracks = [] (int) { return std::vector<TrackSample> {
        sample ("Vocals", vocalFrame (-26.0)), sample ("Electric Rhythm", padFrame (-14.0)), sample ("Drum Machine 01", kickFrame()) }; };
    auto analyse = [&] (double busDb) {
        Analysis a;
        a.begin (Style {});
        a.setMusicBus (true, busDb);
        for (int i = 0; i < 150; ++i)
        {
            Moment m;
            m.tracks = tracks (i);
            auto heard = m.tracks;   // what reaches the Stereo Out: the instruments through the group fader
            for (auto& t : heard) if (t.name != "Vocals") t.faderDb += busDb;
            m.mix = mixOf (heard);
            a.add (m);
        }
        return a;
    };

    SECTION ("its fader counts in the instruments' levels")
    {
        const auto at0 = analyse (0.0).evaluate ({});
        const auto down6 = analyse (-6.0).evaluate ({});
        CHECK (track (down6, "Electric Rhythm")->loudness == Catch::Approx (track (at0, "Electric Rhythm")->loudness - 6.0).margin (0.05));
        CHECK (track (down6, "Vocals")->loudness == Catch::Approx (track (at0, "Vocals")->loudness).margin (0.05));
        CHECK (down6.vocalRatio > at0.vocalRatio + 4.0);
    }
    SECTION ("Balance moves the group fader, not each instrument; room for Masterology too")
    {
        const auto r = analyse (0.0).evaluate ({});
        std::vector<ChannelNow> chans { channel ("Vocals"), channel ("Electric Rhythm"), channel ("Drum Machine 01"), channel ("Beat", 0.0, 0.0, false) };
        chans[3].musicBus = true;
        for (auto& c : chans) { c.hasFaderAtAnalysis = true; c.faderAtAnalysis = c.faderDb; }
        const auto p = makePlan (Tool::balance, r, chans);
        const auto* beat = change (p, "Beat", "level");
        REQUIRE (beat != nullptr);
        CHECK (beat->to < beat->from);
        CHECK (beat->why.find ("music group") != std::string::npos);
        // The instruments only move for their balance against each other, never again for the vocal (group + track).
        for (const char* n : { "Electric Rhythm", "Drum Machine 01" })
            if (const auto* c = change (p, n, "level"))
            {
                CHECK (c->why.find ("makes room for the vocal") == std::string::npos);
                CHECK (c->why.find ("against the rest of the band") != std::string::npos);
            }
        REQUIRE (change (p, "Vocals", "level") != nullptr);

        // Applied: the group fader moved. Opening Balance again finds nothing more.
        for (auto& c : chans)
            for (const auto& ch : p.changes)
                if (ch.name == c.name) c.faderDb = ch.wanted;
        const auto again = makePlan (Tool::balance, r, chans);
        for (const auto& c : again.changes) CHECK (std::abs (c.wanted - c.from) < 1.0);
    }
    SECTION ("room for Masterology: one gain on the Full Mix, the music group and the vocals alike")
    {
        auto rr = analyse (0.0).evaluate ({});
        rr.mixPeak = 6.0;
        std::vector<ChannelNow> chans { channel ("Vocals"), channel ("Electric Rhythm"), channel ("Drum Machine 01"), channel ("Beat", 0.0, 0.0, false) };
        chans[3].musicBus = true;
        const auto p = makePlan (Tool::balance, rr, chans, fullMix());
        REQUIRE (p.headroomDb < 0.0);
        REQUIRE (p.headroomChange.has_value());
        CHECK (p.headroomChange->fullMix);
        CHECK (p.headroomChange->kind == "room");
    }
}

TEST_CASE ("Music group: a channel without a Link that adds no sound of its own is recognised", "[analysis]")
{
    // "Instrumental Bus" (any name): the guitars and drums play through it, so the Links explain the whole mix.
    Analysis a;
    a.begin (Style {});
    for (int i = 0; i < 150; ++i)
    {
        Moment m;
        m.tracks = { sample ("Vocals", vocalFrame (-14.0)), sample ("Gtr", padFrame (-14.0)), sample ("Drums", kickFrame()) };
        m.mix = mixOf (m.tracks);
        a.add (m);
    }
    const auto r = a.evaluate ({}, { "Instrumental Bus" });
    REQUIRE (r.groupHints.size() == 1);
    CHECK (r.groupHints.front() == "Instrumental Bus");
    CHECK (r.unheardLoudness < -100.0);                 // not counted as extra music
    CHECK (find (r, "group-hint") != nullptr);
    CHECK (find (r, "unheard") == nullptr);
    const auto p = makePlan (Tool::balance, r, { channel ("Vocals"), channel ("Gtr"), channel ("Drums"), channel ("Instrumental Bus", 0.0, 0.0, false) });
    CHECK (p.groupHints.size() == 1);
    CHECK (change (p, "Instrumental Bus", "level") == nullptr);

    // A printed beat really adds music: no hint, counted as before.
    Analysis b;
    b.begin (Style {});
    for (int i = 0; i < 150; ++i)
    {
        Moment m;
        m.tracks = { sample ("Vocals", vocalFrame (-14.0)) };
        auto all = m.tracks;
        all.push_back (sample ("Beat", kickFrame()));
        m.mix = mixOf (all);
        b.add (m);
    }
    const auto r2 = b.evaluate ({}, { "Beat" });
    CHECK (r2.groupHints.empty());
    CHECK (r2.unheardLoudness > -100.0);
}

TEST_CASE ("Routing by ear: groups and pan law found from the sound, never counted twice", "[analysis]")
{
    // What reaches the Stereo Out: the vocals through vocalPath, the instruments through musicPath (a group fader,
    // group processing...), plus an optional printed beat nobody has a Link on.
    auto run = [] (double vocalPath, double musicPath, bool printedBeat, std::vector<std::string> unheard) {
        Analysis a;
        a.begin (Style {});
        for (int i = 0; i < 150; ++i)
        {
            Moment m;
            const bool sings = (i / 10) % 2 == 0;   // verses with and without vocal
            m.tracks = { sample ("Vocals", sings ? vocalFrame (-16.0) : silentFrame()), sample ("Gtr", padFrame (-14.0)), sample ("Drums", kickFrame()) };
            auto heard = m.tracks;
            for (auto& t : heard) t.faderDb += t.name == "Vocals" ? vocalPath : musicPath;
            if (printedBeat) heard.push_back (sample ("Beat", hatsFrame(), 10.0));
            m.mix = mixOf (heard);
            a.add (m);
        }
        return a.evaluate ({}, unheard);
    };
    const auto plain = run (0.0, 0.0, false, {});
    REQUIRE (plain.routingFromSound);
    CHECK (plain.vocalPathDb == Catch::Approx (0.0).margin (0.3));
    CHECK (plain.musicPathDb == Catch::Approx (0.0).margin (0.3));

    SECTION ("a group fader at -6, any name")
    {
        const auto r = run (0.0, -6.0, false, { "Bus 1" });
        REQUIRE (r.routingFromSound);
        CHECK (r.musicPathDb == Catch::Approx (-6.0).margin (0.3));
        CHECK (r.vocalPathDb == Catch::Approx (0.0).margin (0.3));
        CHECK (r.vocalRatio == Catch::Approx (plain.vocalRatio + 6.0).margin (0.5));   // the vocal really is 6 dB further up
        CHECK (r.groupHints == std::vector<std::string> { "Bus 1" });
        CHECK (r.unheardLoudness < -100.0);
        CHECK (track (r, "Gtr")->loudness == Catch::Approx (track (plain, "Gtr")->loudness - 6.0).margin (0.3));
    }
    SECTION ("a group whose compressor adds 3 dB")
    {
        const auto r = run (0.0, 3.0, false, { "Instruments" });
        REQUIRE (r.routingFromSound);
        CHECK (r.musicPathDb == Catch::Approx (3.0).margin (0.3));
        CHECK (r.groupHints.size() == 1);   // still no extra music
    }
    SECTION ("a mono vocal through Cubase's pan law (-3 dB in the middle)")
    {
        const auto r = run (-3.0, 0.0, false, {});
        REQUIRE (r.routingFromSound);
        CHECK (r.vocalPathDb == Catch::Approx (-3.0).margin (0.3));
    }
    SECTION ("a printed beat is real extra music")
    {
        const auto r = run (0.0, 0.0, true, { "Beat" });
        CHECK_FALSE (r.routingFromSound);
        CHECK (r.groupHints.empty());
        CHECK (r.unheardLoudness > -100.0);
    }
}

TEST_CASE ("A mono mix: said so, and Stereo Architect leaves the pans alone", "[analysis]")
{
    // Everything reaches the Stereo Out through a mono group: left and right identical, a stereo drum track included.
    Analysis a;
    a.begin (Style {});
    for (int i = 0; i < 150; ++i)
    {
        Moment m;
        m.tracks = { sample ("Vocals", vocalFrame (-14.0)), sample ("Electric Lead", padFrame (-14.0)), sample ("Electric Lead (D)", padFrame (-14.0)),
                     sample ("Drum Machine 01", shaped ([] (int b) { return bump (b, 6, 2.0, -12.0); }, 12.0, 0.3)) };
        auto mono = m.tracks;
        for (auto& t : mono) for (auto& sh : t.frame.sideShare) sh = 0.0f;
        m.mix = mixOf (mono);
        a.add (m);
    }
    const auto r = a.evaluate ({});
    CHECK (r.monoMix);
    const auto* s = find (r, "mono-mix");
    REQUIRE (s != nullptr);
    CHECK (mentions (s, "Drum Machine 01"));
    std::vector<ChannelNow> chans { channel ("Vocals"), channel ("Electric Lead", 0.0, -100.0), channel ("Electric Lead (D)", 0.0, -100.0), channel ("Drum Machine 01") };
    const auto p = makePlan (Tool::stereo, r, chans);
    CHECK (p.changes.empty());
    CHECK (noted (p, "mono at the {daw:master} channel"));
}

TEST_CASE ("Analysis: Voxology on a track makes it a vocal; your choice still wins", "[analysis][suite]")
{
    const auto a = listen ([] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("Audio 07", padFrame (-16.0)), sample ("Audio 08", padFrame (-18.0)) }; });
    const auto before = a.evaluate ({});
    for (const auto& t : before.tracks)
        if (t.name != "Kick") CHECK (t.role != Role::vox);   // by its sound alone it isn't a vocal

    std::map<std::uint32_t, TrackSettings> settings;
    settings[mixology::bridge::nameHash ("Audio 07")].fromVoxology = Role::vox;
    settings[mixology::bridge::nameHash ("Audio 08")].fromVoxology = Role::vox;
    settings[mixology::bridge::nameHash ("Audio 08")].role = Role::adlibs;   // yours
    const auto r = a.evaluate (settings);
    int checked = 0;
    for (const auto& t : r.tracks)
    {
        if (t.name == "Audio 07") { CHECK (t.role == Role::vox); CHECK (t.roleFrom == "voxology"); ++checked; }
        if (t.name == "Audio 08") { CHECK (t.role == Role::adlibs); CHECK (t.roleFrom == "you"); ++checked; }
    }
    CHECK (checked == 2);
}

TEST_CASE ("Auto Analyze: checks after 20 s, then every 15 s, and starts again when the mix changes", "[analysis]")
{
    AutoAnalyze live;
    live.start (Style {});
    auto moment = [] (double vocalFader, bool withHats = false) {
        Moment m;
        m.tracks = { sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Lead Vox", vocalFrame(), vocalFader) };
        if (withHats) m.tracks.push_back (sample ("Hats", hatsFrame()));
        m.mix = mixOf (m.tracks);
        return m;
    };
    auto run = [&] (int moments, double fader, std::uint64_t key, bool hats = false) {
        std::vector<double> checks;
        for (int i = 0; i < moments; ++i)
        {
            const auto step = live.add (moment (fader, hats), key);
            if (step == AutoAnalyze::Step::check) checks.push_back (live.secondsHeard());
            if (step == AutoAnalyze::Step::restarted) checks.push_back (-live.secondsHeard());
        }
        return checks;
    };

    // 60 s of the same mix: restarted at the first moment, checks at 20, 35, 50 s.
    auto c = run (300, 0.0, 1);
    REQUIRE (c.size() == 4);
    CHECK (c[0] == Catch::Approx (-0.2));
    CHECK (c[1] == Catch::Approx (20.0));
    CHECK (c[2] == Catch::Approx (35.0));
    CHECK (c[3] == Catch::Approx (50.0));

    // A fader nudge smaller than 0.25 dB is the same mix; a real move starts again.
    CHECK (run (1, 0.2, 1).empty());
    c = run (1, -3.0, 1);
    REQUIRE (c.size() == 1);
    CHECK (c[0] < 0.0);
    CHECK (live.secondsHeard() == Catch::Approx (0.2));

    // Processing changed (the key), or another track started playing: again from that moment.
    run (50, -3.0, 1);
    CHECK (run (1, -3.0, 2).size() == 1);
    run (50, -3.0, 2);
    CHECK (run (1, -3.0, 2, true).size() == 1);
    CHECK (live.secondsHeard() == Catch::Approx (0.2));

    // reset(): the next moment starts a new window even when nothing changed.
    live.reset();
    CHECK (run (1, -3.0, 2, true).size() == 1);
}

TEST_CASE ("Auto Analyze: only a real change in the advice replaces the report", "[analysis]")
{
    auto beat = [] (double vocalFader) {
        return [=] (int) { return std::vector<TrackSample> {
            sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Hats", hatsFrame()), sample ("Lead Vox", vocalFrame(), vocalFader) }; };
    };
    const auto shown = listen (beat (0.0)).evaluate ({});
    REQUIRE (shown.valid);
    CHECK_FALSE (adviceChanged (shown, listen (beat (0.0)).evaluate ({})));
    CHECK_FALSE (adviceChanged (shown, listen (beat (-0.4)).evaluate ({})));   // a hair: cards stay still
    CHECK (adviceChanged (shown, listen (beat (-8.0)).evaluate ({})));         // the vocal dropped: new advice
    CHECK_FALSE (adviceChanged (shown, Report {}));                           // nothing new heard: keep it
    CHECK (adviceChanged (Report {}, shown));
    auto otherStyle = shown;
    otherStyle.style.key = "pop.modern";   // you picked another style: new advice even if the points are the same
    CHECK (adviceChanged (shown, otherStyle));

    // Another track joined the mix.
    const auto more = listen ([] (int) { return std::vector<TrackSample> {
        sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Hats", hatsFrame()), sample ("Lead Vox", vocalFrame()), sample ("Pad", padFrame()) }; }).evaluate ({});
    CHECK (adviceChanged (shown, more));
}
