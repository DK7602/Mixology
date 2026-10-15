#include "mixology/TrackAnalyzer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

TEST_CASE ("Plans: room for Masterology is offered separately", "[plan]")
{
    auto tracks = [] (int) { return std::vector<TrackSample> { sample ("Kick", kickFrame()), sample ("808", bassFrame()), sample ("Lead Vox", vocalFrame (-12.0)) }; };
    const auto r = listen (tracks, 150, 7.6).evaluate ({});   // peaks at +7.6 dBTP like the first test session
    auto chans = channelsOf (tracks (0));
    chans[1].locked = true;
    const auto p = makePlan (Tool::balance, r, chans);
    CHECK (p.headroomDb < -12.0);
    CHECK (p.headroomWhy.find ("Masterology") != std::string::npos);
    CHECK (p.headroomWhy.find ("'808' can't move") != std::string::npos);
    REQUIRE (p.headroom.size() == 2);   // Kick and Lead Vox; the protected 808 stays
    for (const auto& c : p.changes) CHECK (std::abs (c.wanted - c.from) < 6.0);   // the level changes stay small
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
    CHECK (d->hz >= 1200.0);
    CHECK (d->hz <= 5100.0);
    CHECK (change (p, "Lead Vox", "pocket") == nullptr);
    CHECK (change (p, "Kick", "pocket") == nullptr);

    for (auto& c : chans) if (c.name == "Synth Lead") c.locked = true;
    const auto p2 = makePlan (Tool::pocket, r, chans);
    CHECK (change (p2, "Synth Lead", "pocket") == nullptr);
    CHECK (noted (p2, "protected"));
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
    SECTION ("room for Masterology moves the group and the vocals, not the instruments under the group")
    {
        Analysis a = analyse (0.0);
        Moment hot;
        const auto r = [&] {
            auto rr = a.evaluate ({});
            return rr;
        }();
        auto rr = r;
        rr.mixPeak = 6.0;
        std::vector<ChannelNow> chans { channel ("Vocals"), channel ("Electric Rhythm"), channel ("Drum Machine 01"), channel ("Beat", 0.0, 0.0, false) };
        chans[3].musicBus = true;
        const auto p = makePlan (Tool::balance, rr, chans);
        REQUIRE (p.headroomDb < 0.0);
        std::vector<std::string> names;
        for (const auto& h : p.headroom) names.push_back (h.name);
        CHECK (std::find (names.begin(), names.end(), "Beat") != names.end());
        CHECK (std::find (names.begin(), names.end(), "Vocals") != names.end());
        CHECK (std::find (names.begin(), names.end(), "Electric Rhythm") == names.end());
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
    CHECK (noted (p, "mono at the Stereo Out"));
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
