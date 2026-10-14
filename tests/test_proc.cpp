#include "mixology/ProcChain.h"
#include "mixology/ProcDsp.h"
#include "mixology/ProcParams.h"
#include "mixology/TruePeakDetector.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace mixology;
using namespace mixology::proc;

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    struct Stereo
    {
        std::vector<double> l, r;
        explicit Stereo (std::size_t n = 0) : l (n), r (n) {}
        std::size_t size() const { return l.size(); }
    };

    Stereo sine (double hz, double amp, double seconds, double sr, double phase = 0.0)
    {
        Stereo s (static_cast<std::size_t> (seconds * sr));
        for (std::size_t i = 0; i < s.size(); ++i)
            s.l[i] = s.r[i] = amp * std::sin (2.0 * kPi * hz * static_cast<double> (i) / sr + phase);
        return s;
    }

    void run (Chain& chain, Stereo& s, int block = 512)
    {
        for (std::size_t start = 0; start < s.size(); start += static_cast<std::size_t> (block))
        {
            const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), s.size() - start));
            double* ch[2] = { s.l.data() + start, s.r.data() + start };
            chain.process (ch, 2, n);
        }
    }

    double rms (const std::vector<double>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i) sum += x[i] * x[i];
        return std::sqrt (sum / static_cast<double> (to - from));
    }

    double peak (const std::vector<double>& x, std::size_t from = 0, std::size_t to = 0)
    {
        if (to == 0) to = x.size();
        double p = 0.0;
        for (std::size_t i = from; i < to; ++i) p = std::max (p, std::abs (x[i]));
        return p;
    }

    double db (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

    /** Gain (dB) the chain gives a steady sine, measured after it settled. */
    double sineGainDb (const Values& v, double hz, double sr = 48000.0, double amp = 0.1)
    {
        Chain chain;
        chain.prepare (sr, 2);
        chain.setValues (v);
        auto s = sine (hz, amp, 0.6, sr);
        run (chain, s);
        const auto from = s.size() / 2;
        return db (rms (s.l, from, s.size()) / (amp / std::sqrt (2.0)));
    }

    /** Magnitude of one frequency in a signal (Goertzel). */
    double magnitudeAt (const std::vector<double>& x, std::size_t from, std::size_t n, double hz, double sr)
    {
        const double w = 2.0 * kPi * hz / sr, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n - 1));
            const double s0 = x[from + i] * win + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
        return std::sqrt (re * re + im * im) / (static_cast<double> (n) / 4.0);   // ~ amplitude (Hann)
    }

    Values with (std::initializer_list<std::pair<Id, float>> changes)
    {
        auto v = defaults();
        for (auto [id, value] : changes) v[static_cast<std::size_t> (id)] = value;
        return v;
    }
}

TEST_CASE ("Processing controls: one table, sane ids, everything starts off", "[proc]")
{
    const auto& sp = specs();
    std::set<std::string> ids;
    for (int i = 0; i < count; ++i)
    {
        const auto& s = sp[static_cast<std::size_t> (i)];
        INFO (s.id);
        CHECK (ids.insert (s.id).second);
        CHECK (indexOf (s.id) == i);
        CHECK (s.min < s.max);
        CHECK ((s.def >= s.min && s.def <= s.max));
        CHECK (sanitise (i, s.def) == s.def);
        if (s.curve == Curve::log) CHECK (s.min > 0.0f);
    }
    CHECK (indexOf ("no_such_control") == -1);
    CHECK (sp[chainOn].def == 1.0f);
    for (int m = 0; m < kModules; ++m)
    {
        const auto& s = sp[static_cast<std::size_t> (onParam (static_cast<Module> (m)))];
        INFO (s.id);
        CHECK (s.kind == Kind::toggle);
        CHECK (s.def == 0.0f);   // nothing runs on a new Link
        CHECK (! isOn (defaults(), static_cast<Module> (m)));
    }

    CHECK (sanitise (compRatio, 50.0f) == 20.0f);
    CHECK (sanitise (compThr, std::nanf ("")) == sp[compThr].def);
    CHECK (sanitise (compThr, -12.34f) == Catch::Approx (-12.3f));
    CHECK (sanitise (eqMode, 1.7f) == 2.0f);
    CHECK (sanitise (eq1Type, 9.0f) == 4.0f);
}

TEST_CASE ("Processing: all modules off passes the audio bit for bit", "[proc]")
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> l (4096), r (4096);
    for (auto& x : l) x = d (rng);
    for (auto& x : r) x = d (rng);
    auto l0 = l, r0 = r;

    Chain chain;
    chain.prepare (44100.0, 2);
    chain.setValues (defaults());
    CHECK (! chain.active());
    CHECK (chain.latencyFor (defaults()) == 0);
    float* ch[2] = { l.data(), r.data() };
    chain.process (ch, 2, 4096);
    CHECK (l == l0);
    CHECK (r == r0);

    // EQ switched on with every band flat: still bit for bit.
    auto v = with ({ { eqOn, 1.0f } });
    chain.setValues (v);
    for (int k = 0; k < 4; ++k) chain.process (ch, 2, 4096);
    l = l0;
    r = r0;
    chain.process (ch, 2, 4096);
    CHECK (l == l0);
    CHECK (r == r0);
}

TEST_CASE ("Processing EQ: each band does what its curve says", "[proc]")
{
    const double sr = 48000.0;
    auto v = with ({ { eqOn, 1.0f }, { eq3Freq, 1000.0f }, { eq3Gain, 6.0f }, { eq3Q, 1.0f } });
    CHECK (sineGainDb (v, 1000.0) == Catch::Approx (6.0).margin (0.1));
    CHECK (std::abs (sineGainDb (v, 60.0)) < 0.1);
    for (double hz : { 200.0, 700.0, 1400.0, 5000.0 })
        CHECK (sineGainDb (v, hz) == Catch::Approx (Chain::eqResponseDb (v, hz, sr)).margin (0.1));

    // Low cut at 100 Hz, 24 dB / octave: about -24 dB an octave below (Butterworth-ish pair).
    auto cut = with ({ { eqOn, 1.0f }, { eq1Type, 3.0f }, { eq1Freq, 100.0f }, { eq1Q, 0.7071f } });
    CHECK (sineGainDb (cut, 50.0) == Catch::Approx (Chain::eqResponseDb (cut, 50.0, sr)).margin (0.2));
    CHECK (Chain::eqResponseDb (cut, 50.0, sr) < -22.0);
    CHECK (std::abs (sineGainDb (cut, 1000.0)) < 0.05);

    // Smooth: a gentler 12 dB / octave cut and wider bells. Natural: small moves broader than Precision.
    auto smooth = cut;
    smooth[eqMode] = 1.0f;
    CHECK (Chain::eqResponseDb (smooth, 50.0, sr) == Catch::Approx (Chain::eqResponseDb (cut, 50.0, sr) / 2.0).margin (0.01));
    auto bell = v;
    bell[eqMode] = 1.0f;
    CHECK (Chain::eqResponseDb (bell, 2000.0, sr) > Chain::eqResponseDb (v, 2000.0, sr) + 0.5);
    auto natural = with ({ { eqOn, 1.0f }, { eqMode, 2.0f }, { eq3Freq, 1000.0f }, { eq3Gain, 2.0f }, { eq3Q, 1.0f } });
    auto precise = natural;
    precise[eqMode] = 0.0f;
    CHECK (Chain::eqResponseDb (natural, 2000.0, sr) > Chain::eqResponseDb (precise, 2000.0, sr));

    // Shelves.
    auto shelves = with ({ { eqOn, 1.0f }, { eq1Gain, 4.0f }, { eq6Gain, -3.0f } });
    CHECK (sineGainDb (shelves, 30.0) == Catch::Approx (4.0).margin (0.3));
    CHECK (sineGainDb (shelves, 19000.0) == Catch::Approx (-3.0).margin (0.4));
}

TEST_CASE ("Processing compressor: threshold, ratio, makeup", "[proc]")
{
    // Peak -8 dBFS, threshold -20, 4:1, hard knee: 12 dB over -> 9 dB off.
    auto v = with ({ { compOn, 1.0f }, { compThr, -20.0f }, { compRatio, 4.0f }, { compKnee, 2.0f }, { compAtt, 0.1f }, { compRel, 1000.0f } });
    const double amp = std::pow (10.0, -8.0 / 20.0);
    Chain chain;
    chain.prepare (48000.0, 2);
    chain.setValues (v);
    auto s = sine (500.0, amp, 1.0, 48000.0);
    run (chain, s);
    CHECK (db (peak (s.l, s.size() / 2)) == Catch::Approx (-17.0).margin (0.6));
    CHECK (chain.takeMeters().comp == Catch::Approx (9.0).margin (0.6));

    // Below the threshold nothing changes; makeup lifts.
    auto quiet = v;
    quiet[compMakeup] = 3.0f;
    CHECK (sineGainDb (quiet, 500.0, 48000.0, 0.01) == Catch::Approx (3.0).margin (0.05));
}

TEST_CASE ("Processing gate and expander: quiet parts down by the range", "[proc]")
{
    const double sr = 48000.0;
    auto test = [&] (float mode, double expectDb) {
        auto v = with ({ { gateOn, 1.0f }, { gateThr, -40.0f }, { gateRange, 30.0f }, { gateRel, 50.0f }, { gateHold, 10.0f }, { gateMode, mode } });
        Chain chain;
        chain.prepare (sr, 2);
        chain.setValues (v);
        auto loud = sine (300.0, 0.3, 0.5, sr), quiet = sine (300.0, 0.001, 1.0, sr);   // -10 dBFS, then -60 dBFS
        Stereo s;
        s.l = loud.l; s.l.insert (s.l.end(), quiet.l.begin(), quiet.l.end());
        s.r = s.l;
        run (chain, s);
        CHECK (db (rms (s.l, 12000, 24000) / (0.3 / std::sqrt (2.0))) == Catch::Approx (0.0).margin (0.05));
        CHECK (db (rms (s.l, s.size() - 12000, s.size()) / (0.001 / std::sqrt (2.0))) == Catch::Approx (expectDb).margin (0.5));
    };
    test (0.0f, -30.0);   // gate: the full range
    test (1.0f, -20.0);   // expander 2:1: 20 dB under the threshold -> 20 dB down
}

TEST_CASE ("Processing transient shaper: attack lifts the hit, sustain the tail", "[proc]")
{
    const double sr = 48000.0;
    // A drum-like hit every 250 ms: fast attack, 80 ms decay, 200 Hz tone.
    Stereo hits (static_cast<std::size_t> (sr));
    for (std::size_t i = 0; i < hits.size(); ++i)
    {
        const double t = std::fmod (static_cast<double> (i) / sr, 0.25);
        hits.l[i] = hits.r[i] = 0.5 * std::exp (-t / 0.08) * std::sin (2.0 * kPi * 200.0 * t);
    }
    auto shaped = [&] (float att, float sus) {
        Chain chain;
        chain.prepare (sr, 2);
        chain.setValues (with ({ { transOn, 1.0f }, { transAttack, att }, { transSustain, sus } }));
        Stereo s = hits;
        run (chain, s);
        return s;
    };
    const std::size_t hit = static_cast<std::size_t> (0.75 * sr);   // the fourth hit (settled)
    auto front = [&] (const Stereo& s) { return peak (s.l, hit, hit + static_cast<std::size_t> (0.004 * sr)); };
    auto tail = [&] (const Stereo& s) { return rms (s.l, hit + static_cast<std::size_t> (0.12 * sr), hit + static_cast<std::size_t> (0.2 * sr)); };

    const auto flat = shaped (0.0f, 0.0f), attack = shaped (100.0f, 0.0f), sustain = shaped (0.0f, 100.0f);
    CHECK (db (front (attack) / front (flat)) > 3.0);
    CHECK (std::abs (db (tail (attack) / tail (flat))) < 1.5);
    CHECK (db (tail (sustain) / tail (flat)) > 3.0);
    CHECK (std::abs (db (front (flat) / front (hits))) < 0.01);   // at 0 / 0 nothing changes
}

TEST_CASE ("Processing limiter: never past the ceiling, latency as reported", "[proc]")
{
    const double sr = 44100.0;
    auto v = with ({ { limOn, 1.0f }, { limCeil, -1.0f }, { limIn, 6.0f }, { limDetect, 0.0f }, { limLook, 3.0f } });
    Chain chain;
    chain.prepare (sr, 2);
    const int latency = chain.latencyFor (v);
    CHECK (latency == static_cast<int> (std::ceil (0.005 * sr)) + TruePeakDetector::kLatency);
    chain.setValues (v);

    // Loud music-like material with spikes.
    std::mt19937 rng (3);
    std::normal_distribution<double> nd (0.0, 0.25);
    Stereo s (static_cast<std::size_t> (2.0 * sr));
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        s.l[i] = 0.4 * std::sin (2.0 * kPi * 80.0 * static_cast<double> (i) / sr) + nd (rng);
        s.r[i] = s.l[i] * 0.8 + nd (rng) * 0.3;
        if (i % 5000 == 0) s.l[i] = 1.8;
    }
    run (chain, s, 256);
    const double ceiling = std::pow (10.0, -1.0 / 20.0);
    const std::size_t settled = static_cast<std::size_t> (0.05 * sr);
    CHECK (peak (s.l, settled) <= ceiling * 1.000001);
    CHECK (peak (s.r, settled) <= ceiling * 1.000001);
    CHECK (chain.takeMeters().limit > 3.0f);

    // A quiet click comes out exactly `latency` samples later, untouched.
    Chain quiet;
    quiet.prepare (sr, 2);
    auto q = with ({ { limOn, 1.0f }, { limDetect, 0.0f } });
    quiet.setValues (q);
    Stereo silence (4096);
    run (quiet, silence);   // fully faded in
    Stereo click (4096);
    click.l[1000] = click.r[1000] = 0.1;
    run (quiet, click);
    CHECK (click.l[static_cast<std::size_t> (1000 + latency)] == Catch::Approx (0.1).margin (1.0e-9));
    CHECK (peak (click.l, 0, static_cast<std::size_t> (1000 + latency)) < 1.0e-12);

    // True peak: a sine at a quarter of the sample rate, phase 45 degrees: samples at -3 dB, the wave at 0 dBTP.
    auto tpTest = [&] (float detect) {
        Chain c;
        c.prepare (sr, 2);
        c.setValues (with ({ { limOn, 1.0f }, { limCeil, -1.0f }, { limDetect, detect } }));
        auto x = sine (sr / 4.0, 0.999, 1.0, sr, kPi / 4.0);
        run (c, x);
        TruePeakDetector det;
        double tp = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)   // the meter runs from the start (it rings while it warms up)
        {
            const double p = det.process (x.l[i]);
            if (i > x.size() / 2) tp = std::max (tp, p);
        }
        return db (tp);
    };
    CHECK (tpTest (0.0f) > -0.2);                               // sample peaks never reached -1 dB: untouched
    CHECK (tpTest (1.0f) == Catch::Approx (-1.0).margin (0.2));  // true-peak mode: the wave itself at -1 dBTP
}

TEST_CASE ("Processing multiband: at 0 % flat, a band's amount squeezes only that band", "[proc]")
{
    auto v = with ({ { multiOn, 1.0f }, { multiLow, 0.0f }, { multiMid, 0.0f }, { multiHigh, 0.0f } });
    for (double hz : { 40.0, 160.0, 1000.0, 3500.0, 12000.0 })
        CHECK (std::abs (sineGainDb (v, hz)) < 0.05);
    auto two = v;
    two[multiBands] = 1.0f;
    for (double hz : { 40.0, 160.0, 1000.0 })
        CHECK (std::abs (sineGainDb (two, hz)) < 0.05);

    // A pumping bass (loud / quiet every 200 ms) under a steady 5 kHz tone; squeeze the lows only.
    const double sr = 48000.0;
    auto material = [&] {
        Stereo s (static_cast<std::size_t> (3.0 * sr));
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double bassAmp = std::fmod (t, 0.4) < 0.2 ? 0.5 : 0.05;
            s.l[i] = s.r[i] = bassAmp * std::sin (2.0 * kPi * 60.0 * t) + 0.05 * std::sin (2.0 * kPi * 5000.0 * t);
        }
        return s;
    };
    auto squeezed = with ({ { multiOn, 1.0f }, { multiLow, 100.0f }, { multiMid, 0.0f }, { multiHigh, 0.0f } });
    Chain chain;
    chain.prepare (sr, 2);
    chain.setValues (squeezed);
    auto s = material(), dryS = material();
    run (chain, s);
    const auto from = static_cast<std::size_t> (2.0 * sr), n = static_cast<std::size_t> (0.2 * sr);
    const double bassBefore = magnitudeAt (dryS.l, from, n, 60.0, sr), bassAfter = magnitudeAt (s.l, from, n, 60.0, sr);
    const double topBefore = magnitudeAt (dryS.l, from, n, 5000.0, sr), topAfter = magnitudeAt (s.l, from, n, 5000.0, sr);
    CHECK (db (bassAfter / bassBefore) < -2.0);   // the loud bass part is held down
    CHECK (std::abs (db (topAfter / topBefore)) < 0.5);
    CHECK (chain.takeMeters().multiLow > 2.0f);
}

TEST_CASE ("Oversampling: flat in the audio band, images and aliases far down", "[proc]")
{
    dsp::Oversampler os;
    for (int stages = 1; stages <= 3; ++stages)
    {
        INFO ("stages " << stages);
        os.setStages (stages);
        const double sr = 44100.0;
        for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 })
        {
            os.reset();
            auto s = sine (hz, 0.5, 0.3, sr);
            std::vector<double> out (s.size());
            for (std::size_t start = 0; start + 32 <= s.size(); start += 32)
            {
                os.up (0, s.l.data() + start, 32);
                os.down (0, out.data() + start, 32);
            }
            const auto from = s.size() / 2;
            CHECK (std::abs (db (rms (out, from, s.size() - 32) / rms (s.l, from, s.size() - 32))) < 0.05);
        }
    }

    // 2x: the image of a 15 kHz tone (at 29.1 kHz) is gone from the high-rate signal.
    os.setStages (1);
    const double sr = 44100.0;
    auto s = sine (15000.0, 0.5, 0.2, sr);
    std::vector<double> high;
    for (std::size_t start = 0; start + 32 <= s.size(); start += 32)
    {
        const double* h = os.up (0, s.l.data() + start, 32);
        high.insert (high.end(), h, h + 64);
    }
    const double tone = magnitudeAt (high, 4096, 8192, 15000.0, 2.0 * sr);
    const double img = magnitudeAt (high, 4096, 8192, sr - 15000.0, 2.0 * sr);
    CHECK (db (img / tone) < -70.0);
    CHECK (db (tone / 0.5) == Catch::Approx (0.0).margin (0.1));
}

TEST_CASE ("Processing clip control: never past the threshold, oversampling keeps aliases down", "[proc]")
{
    const double sr = 44100.0;
    auto clipped = [&] (float os, float soft) {
        Chain chain;
        chain.prepare (sr, 2);
        chain.setValues (with ({ { clipOn, 1.0f }, { clipThr, -6.0f }, { clipSoft, soft }, { clipOs, os } }));
        auto s = sine (7000.0, 0.9, 1.0, sr);
        run (chain, s);
        return s;
    };
    const auto hard1x = clipped (0.0f, 0.0f);
    CHECK (peak (hard1x.l, hard1x.size() / 2) <= std::pow (10.0, -6.0 / 20.0) + 1.0e-9);

    // Harmonics of 7 kHz above Nyquist fold back to 9.1 kHz (5th) and 4.9 kHz (7th) at 1x.
    const auto hard4x = clipped (2.0f, 0.0f);
    const auto from = hard1x.size() / 2;
    const std::size_t n = 16384;
    const double alias1x = std::max (magnitudeAt (hard1x.l, from, n, 9100.0, sr), magnitudeAt (hard1x.l, from, n, 4900.0, sr));
    const double alias4x = std::max (magnitudeAt (hard4x.l, from, n, 9100.0, sr), magnitudeAt (hard4x.l, from, n, 4900.0, sr));
    CHECK (db (alias4x / alias1x) < -20.0);
}

TEST_CASE ("Processing stereo: width 0 is mono, 100 changes nothing, low width only narrows the lows", "[proc]")
{
    const double sr = 48000.0;
    auto pair = [&] (double hz) {
        Stereo s (static_cast<std::size_t> (0.5 * sr));
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            s.l[i] = 0.3 * std::sin (2.0 * kPi * hz * t);
            s.r[i] = 0.3 * std::sin (2.0 * kPi * hz * t + 1.2);
        }
        return s;
    };
    auto sideOf = [] (const Stereo& s, std::size_t from) {
        std::vector<double> side (s.size());
        for (std::size_t i = 0; i < s.size(); ++i) side[i] = 0.5 * (s.l[i] - s.r[i]);
        return rms (side, from, s.size());
    };
    auto runWith = [&] (Values v, double hz) {
        Chain chain;
        chain.prepare (sr, 2);
        chain.setValues (v);
        auto s = pair (hz);
        run (chain, s);
        return s;
    };
    const auto from = static_cast<std::size_t> (0.3 * sr);
    const auto ref = pair (1000.0);

    const auto mono = runWith (with ({ { stOn, 1.0f }, { stWidth, 0.0f } }), 1000.0);
    for (std::size_t i = from; i < mono.size(); ++i) REQUIRE (std::abs (mono.l[i] - mono.r[i]) < 1.0e-12);

    // At 100 % the image is the same: left, right and side as loud as before (the bands' crossovers only
    // shift the phase of mid and side alike, so left and right keep their relation).
    const auto same = runWith (with ({ { stOn, 1.0f } }), 1000.0);
    CHECK (db (rms (same.l, from, same.size()) / rms (ref.l, from, ref.size())) == Catch::Approx (0.0).margin (0.01));
    CHECK (db (rms (same.r, from, same.size()) / rms (ref.r, from, ref.size())) == Catch::Approx (0.0).margin (0.01));
    CHECK (db (sideOf (same, from) / sideOf (ref, from)) == Catch::Approx (0.0).margin (0.01));

    const auto wide = runWith (with ({ { stOn, 1.0f }, { stWidth, 200.0f } }), 1000.0);
    CHECK (db (sideOf (wide, from) / sideOf (ref, from)) == Catch::Approx (6.02).margin (0.05));

    auto lowNarrow = with ({ { stOn, 1.0f }, { stLow, 0.0f } });
    CHECK (db (sideOf (runWith (lowNarrow, 50.0), from) / sideOf (pair (50.0), from)) < -12.0);
    CHECK (std::abs (db (sideOf (runWith (lowNarrow, 2000.0), from) / sideOf (pair (2000.0), from))) < 0.3);
}

TEST_CASE ("Processing harmonics and analog: they add harmonics; noise only while the track plays", "[proc]")
{
    const double sr = 48000.0;
    auto harmonics = [&] (float even) {
        Chain chain;
        chain.prepare (sr, 2);
        chain.setValues (with ({ { harmOn, 1.0f }, { harmDrive, 100.0f }, { harmEvenOdd, even }, { harmMix, 100.0f } }));
        auto s = sine (1000.0, 0.5, 0.5, sr);
        run (chain, s);
        const std::size_t from = 12000, n = 8192;
        const double f1 = magnitudeAt (s.l, from, n, 1000.0, sr);
        return std::pair { db (magnitudeAt (s.l, from, n, 2000.0, sr) / f1), db (magnitudeAt (s.l, from, n, 3000.0, sr) / f1) };
    };
    const auto [even2, even3] = harmonics (100.0f);
    const auto [odd2, odd3] = harmonics (0.0f);
    CHECK (even2 > -40.0);   // even: a clear 2nd harmonic
    CHECK (odd2 < -80.0);    // odd: (almost) none
    CHECK (odd3 > -30.0);    // ... but a 3rd
    (void) even3;

    Chain chain;
    chain.prepare (sr, 2);
    chain.setValues (with ({ { anOn, 1.0f }, { anNoise, 100.0f }, { anTape, 100.0f }, { anConsole, 100.0f } }));
    Stereo silence (static_cast<std::size_t> (sr));
    run (chain, silence);
    CHECK (peak (silence.l) < 1.0e-12);
    auto s = sine (200.0, 0.3, 1.0, sr);
    run (chain, s);
    CHECK (std::isfinite (rms (s.l, 0, s.size())));
}

TEST_CASE ("Processing: switching a module on or off never clicks", "[proc]")
{
    const double sr = 48000.0;
    Chain chain;
    chain.prepare (sr, 2);
    auto off = defaults();
    auto on = with ({ { compOn, 1.0f }, { compThr, -40.0f }, { compRatio, 20.0f }, { compAtt, 0.1f }, { compKnee, 2.0f },
                      { eqOn, 1.0f }, { eq3Gain, 12.0f }, { eq3Freq, 100.0f } });
    auto s = sine (100.0, 0.5, 2.0, sr);
    double maxStep = 0.0, prev = 0.0;
    const double natural = 2.0 * kPi * 100.0 / sr * 0.5 * 4.0;   // the sine's own steepest step, x4 for the EQ's boost
    for (std::size_t start = 0; start < s.size(); start += 480)
    {
        chain.setValues ((start / 24000) % 2 == 1 ? on : off);
        double* ch[2] = { s.l.data() + start, s.r.data() + start };
        chain.process (ch, 2, 480);
        for (std::size_t i = start; i < start + 480; ++i)
        {
            maxStep = std::max (maxStep, std::abs (s.l[i] - prev));
            prev = s.l[i];
        }
    }
    CHECK (maxStep < natural);
}

TEST_CASE ("Processing: any settings, any audio: always finite, never blows up", "[proc]")
{
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::normal_distribution<double> nd (0.0, 0.3);
    for (int channels : { 1, 2 })
    {
        Chain chain;
        chain.prepare (44100.0, channels);
        chain.ensureSpace();
        std::vector<double> l (777), r (777);
        Values last {};
        for (int block = 0; block < 400; ++block)
        {
            if (block % 7 == 0)
            {
                Values v;
                for (int i = 0; i < count; ++i)
                {
                    const auto& s = specs()[static_cast<std::size_t> (i)];
                    v[static_cast<std::size_t> (i)] = s.min + u (rng) * (s.max - s.min);
                }
                v[chainOn] = 1.0f;
                chain.setValues (v);
                last = v;
            }
            chain.setHost (60.0 + 120.0 * u (rng), u (rng) < 0.5 ? -1.0 : 2.0 * u (rng));
            for (auto& x : l) x = nd (rng) * (block % 13 == 0 ? 8.0 : 1.0);
            for (auto& x : r) x = nd (rng);
            double* ch[2] = { l.data(), r.data() };
            chain.process (ch, channels, 777);
            // Random settings can stack six EQ boosts of +12 dB on one frequency (+72 dB) on a burst 17 dB over full
            // scale: loud, but finite and bounded; runaway is what the silence check below catches.
            for (int c = 0; c < channels; ++c)
                for (double x : (c == 0 ? l : r)) REQUIRE ((std::isfinite (x) && std::abs (x) < 1.0e5));
        }
        // And it all goes quiet again once the input stops (nothing keeps ringing or feeding itself).
        last[spOn] = 0.0f;
        chain.setValues (last);
        double peak = 0.0;
        for (int block = 0; block < 120; ++block)
        {
            std::fill (l.begin(), l.end(), 0.0);
            std::fill (r.begin(), r.end(), 0.0);
            double* ch[2] = { l.data(), r.data() };
            chain.process (ch, channels, 777);
            if (block >= 100)
                for (int c = 0; c < channels; ++c)
                    for (double x : (c == 0 ? l : r)) peak = std::max (peak, std::abs (x));
        }
        CHECK (peak < 1.0e-2);
    }
}
