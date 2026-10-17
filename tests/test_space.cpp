#include "mixology/ProcChain.h"
#include "mixology/ProcParams.h"
#include "mixology/SpaceFx.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace mixology;
using namespace mixology::proc;

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kSr = 48000.0;

    Values space (SpaceType type, std::initializer_list<std::pair<Id, float>> changes = {})
    {
        auto v = defaults();
        v[spOn] = 1.0f;
        v[spType] = static_cast<float> (type);
        v[spRoute] = 1.0f;          // FX channel: only the wet sound comes out
        v[spLowCut] = 20.0f;        // the wet EQ wide open
        v[spHighCut] = 20000.0f;
        for (auto [id, value] : changes) v[static_cast<std::size_t> (id)] = value;
        return v;
    }

    struct Run
    {
        std::vector<double> l, r;
    };

    /** Plays `in` (mono, both sides) through a chain with these settings, in 256-sample blocks. */
    Run play (Chain& chain, const std::vector<double>& in, double key = -1.0, double bpm = 120.0)
    {
        Run out { in, in };
        for (std::size_t start = 0; start < in.size(); start += 256)
        {
            const int n = static_cast<int> (std::min<std::size_t> (256, in.size() - start));
            chain.setHost (bpm, key);
            double* ch[2] = { out.l.data() + start, out.r.data() + start };
            chain.process (ch, 2, n);
        }
        return out;
    }

    std::unique_ptr<Chain> makeChain (const Values& v, double bpm = 120.0)
    {
        auto c = std::make_unique<Chain>();
        c->prepare (kSr, 2);
        c->ensureSpace();
        c->setValues (v);
        // Let the 20 ms switch-on fade finish on silence, so the tests hear the effect alone.
        std::vector<double> settleL (4800, 0.0), settleR (4800, 0.0);
        double* ch[2] = { settleL.data(), settleR.data() };
        c->setHost (bpm, -1.0);
        c->process (ch, 2, 4800);
        return c;
    }

    double peak (const std::vector<double>& x, std::size_t from = 0, std::size_t to = 0)
    {
        if (to == 0) to = x.size();
        double p = 0.0;
        for (std::size_t i = from; i < to; ++i) p = std::max (p, std::abs (x[i]));
        return p;
    }

    double rmsDb (const std::vector<double>& x, std::size_t from, std::size_t to)
    {
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i) s += x[i] * x[i];
        return 10.0 * std::log10 (s / static_cast<double> (to - from) + 1.0e-30);
    }

    std::vector<double> impulse (double seconds, double amp = 0.5)
    {
        std::vector<double> x (static_cast<std::size_t> (seconds * kSr));
        x[0] = amp;
        return x;
    }

    /** Magnitude of one frequency (Goertzel, Hann window). */
    double magnitudeAt (const std::vector<double>& x, std::size_t from, std::size_t n, double hz)
    {
        const double w = 2.0 * kPi * hz / kSr, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n - 1));
            const double s0 = x[from + i] * win + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (static_cast<double> (n) / 4.0);
    }
}

TEST_CASE ("Space reverbs: the tail fades at the Decay you set (RT60), then to silence", "[space]")
{
    for (auto [type, decayId, decay] : { std::tuple { SpaceType::reverb, revDecay, 1.8f }, std::tuple { SpaceType::hall, hallDecay, 4.0f },
                                         std::tuple { SpaceType::room, roomDecay, 0.8f } })
    {
        INFO ("type " << static_cast<int> (type) << ", decay " << decay);
        auto v = space (type, { { decayId, decay }, { revPre, 0.0f }, { hallPre, 0.0f }, { roomPre, 0.0f } });
        auto chain = makeChain (v);
        const auto out = play (*chain, impulse (decay * 2.2 + 0.5));
        // Level in 50 ms windows; the slope between -5 and -35 dB below the start of the tail gives RT60.
        const std::size_t win = static_cast<std::size_t> (0.05 * kSr);
        std::vector<double> env;
        for (std::size_t i = 0; i + win <= out.l.size(); i += win) env.push_back (rmsDb (out.l, i, i + win));
        const std::size_t peakAt = static_cast<std::size_t> (std::max_element (env.begin(), env.end()) - env.begin());
        const double top = env[peakAt];
        std::size_t a = peakAt, b = peakAt;
        while (a < env.size() && env[a] > top - 5.0) ++a;
        while (b < env.size() && env[b] > top - 35.0) ++b;
        REQUIRE (b < env.size());
        const double rt60 = static_cast<double> (b - a) * 0.05 * 60.0 / 30.0;
        CHECK (rt60 == Catch::Approx (decay).epsilon (0.35));
        CHECK (std::isfinite (top));
    }

    // Long after the input stops it is silent (no hum, no DC, no endless ringing).
    auto chain = makeChain (space (SpaceType::reverb, { { revDecay, 0.5f } }));
    const auto out = play (*chain, impulse (4.0));
    CHECK (rmsDb (out.l, out.l.size() - 4800, out.l.size()) < -110.0);
    CHECK (Chain::tailSecondsFor (space (SpaceType::reverb, { { revDecay, 2.0f }, { revPre, 100.0f } })) == Catch::Approx (0.1 + 2.6));
    CHECK (Chain::tailSecondsFor (defaults()) == 0.0);
}

TEST_CASE ("Space delay: echoes on the beat, fainter by Feedback, ping-pong left / right", "[space]")
{
    // 120 BPM, 1/8 note = 0.25 s; full ping-pong: the first echo on the left, the next on the right.
    auto v = space (SpaceType::delay, { { dlyTime, 3.0f }, { dlyFeedback, 50.0f }, { dlyPingPong, 100.0f }, { dlyTone, 20000.0f } });
    auto chain = makeChain (v);
    const auto out = play (*chain, impulse (1.2), -1.0, 120.0);
    auto peakNear = [&] (const std::vector<double>& x, double t) {
        const auto c = static_cast<std::size_t> (t * kSr);
        double p = 0.0;
        for (std::size_t i = c - 200; i < c + 200; ++i) p = std::max (p, std::abs (x[i]));
        return p;
    };
    const double e1L = peakNear (out.l, 0.25), e1R = peakNear (out.r, 0.25);
    const double e2L = peakNear (out.l, 0.5), e2R = peakNear (out.r, 0.5);
    CHECK (e1L > 0.3);                // the first repeat, left
    CHECK (e1R < 0.02 * e1L);
    CHECK (e2R > 0.1);                // the second, right
    CHECK (e2L < 0.02 * e2R);
    CHECK (e2R / e1L == Catch::Approx (0.5).margin (0.08));   // Feedback 50 %
    CHECK (peakNear (out.l, 0.1) < 1.0e-6);                    // nothing before the beat time

    // A different tempo moves the echoes with it.
    auto fast = makeChain (space (SpaceType::delay, { { dlyTime, 3.0f }, { dlyPingPong, 0.0f } }), 150.0);
    const auto out2 = play (*fast, impulse (1.0), -1.0, 150.0);   // 1/8 at 150 BPM = 0.2 s
    CHECK (peakNear (out2.l, 0.2) > 0.3);
}

TEST_CASE ("Space chorus and phaser: blended in by Mix, nothing at 0 %", "[space]")
{
    std::mt19937 rng (5);
    std::normal_distribution<double> nd (0.0, 0.2);
    std::vector<double> noise (48000);
    for (auto& x : noise) x = nd (rng);
    for (auto [type, mixId] : { std::pair { SpaceType::chorus, choMix }, std::pair { SpaceType::phaser, phaMix } })
    {
        auto dry = space (type, { { mixId, 0.0f } });
        dry[spRoute] = 0.0f;   // on the track
        auto chain = makeChain (dry);
        const auto out = play (*chain, noise);
        bool same = true;
        for (std::size_t i = 4800; i < noise.size(); ++i) same = same && out.l[i] == noise[i];
        CHECK (same);

        auto wet = makeChain (space (type, { { mixId, 100.0f } }));
        const auto w = play (*wet, noise);
        const double level = rmsDb (w.l, 24000, 48000) - rmsDb (noise, 24000, 48000);
        CHECK ((level > -8.0 && level < 3.0));
    }
}

TEST_CASE ("Space ducking: the effect gets quieter while the key (vocal) plays", "[space]")
{
    std::mt19937 rng (9);
    std::normal_distribution<double> nd (0.0, 0.2);
    std::vector<double> noise (96000);
    for (auto& x : noise) x = nd (rng);
    auto v = space (SpaceType::reverb, { { duckAmount, 12.0f } });
    auto quiet = makeChain (v);
    auto loud = makeChain (v);
    const auto free = play (*quiet, noise, 0.0);    // the vocal is silent
    const auto ducked = play (*loud, noise, 0.5);   // the vocal sings (-6 dBFS)
    const double diff = rmsDb (ducked.l, 48000, 96000) - rmsDb (free.l, 48000, 96000);
    CHECK (diff == Catch::Approx (-12.0).margin (1.0));
    CHECK (loud->takeMeters().duck == Catch::Approx (12.0).margin (0.5));
    auto none = makeChain (v);
    const auto noKey = play (*none, noise, -1.0);   // no key chosen: never ducks
    CHECK (std::abs (rmsDb (noKey.l, 48000, 96000) - rmsDb (free.l, 48000, 96000)) < 0.1);
}

TEST_CASE ("Space special FX: shimmer adds the octave, swell fades in, freeze stays bounded", "[space]")
{
    // A 440 Hz note into Shimmer +12: its tail holds 880 Hz.
    std::vector<double> note (static_cast<std::size_t> (3.0 * kSr));
    for (std::size_t i = 0; i < static_cast<std::size_t> (1.0 * kSr); ++i) note[i] = 0.3 * std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / kSr);
    auto shimmer = makeChain (space (SpaceType::special, { { spxShift, 12.0f }, { spxFeedback, 70.0f }, { spxMode, 0.0f } }));
    const auto sh = play (*shimmer, note);
    auto plain = makeChain (space (SpaceType::special, { { spxShift, 12.0f }, { spxFeedback, 70.0f }, { spxMode, 1.0f } }));   // Drift: no pitch
    const auto pl = play (*plain, note);
    const std::size_t from = static_cast<std::size_t> (1.5 * kSr), n = 16384;
    const double octaveShimmer = magnitudeAt (sh.l, from, n, 880.0) / magnitudeAt (sh.l, from, n, 440.0);
    const double octavePlain = magnitudeAt (pl.l, from, n, 880.0) / magnitudeAt (pl.l, from, n, 440.0);
    CHECK (octaveShimmer > 5.0 * octavePlain);

    // Swell: the effect is still quiet just after the note starts, full a second later.
    std::vector<double> notes (static_cast<std::size_t> (3.0 * kSr));
    for (std::size_t i = static_cast<std::size_t> (1.0 * kSr); i < notes.size(); ++i) notes[i] = 0.3 * std::sin (2.0 * kPi * 330.0 * static_cast<double> (i) / kSr);
    auto swell = makeChain (space (SpaceType::special, { { spxMode, 2.0f }, { spxMotion, 50.0f } }));
    const auto sw = play (*swell, notes);
    const auto at = [&] (double t) { const auto c = static_cast<std::size_t> (t * kSr); return rmsDb (sw.l, c, c + 2400); };
    CHECK (at (1.12) < at (2.5) - 6.0);

    // Freeze: loud noise for 20 s never runs away.
    std::mt19937 rng (3);
    std::normal_distribution<double> nd (0.0, 0.5);
    std::vector<double> noise (static_cast<std::size_t> (20.0 * kSr));
    for (auto& x : noise) x = nd (rng);
    auto freeze = makeChain (space (SpaceType::special, { { spxMode, 3.0f }, { spxFeedback, 95.0f } }));
    const auto fz = play (*freeze, noise);
    double peak = 0.0;
    for (double x : fz.l) peak = std::max (peak, std::abs (x));
    CHECK (std::isfinite (peak));
    CHECK (peak <= 1.2 + 1.0e-9);
}

TEST_CASE ("Space: every effect's tail dies away, at the most feedback too, and never comes back", "[space]")
{
    // Shimmer fed its pitched tail back with nothing to stop the lows (they keep their pitch and the reverb rings
    // them up): at the default Feedback (55 %) they grew slowly, at 95 % by 10 dB a second until the sound was
    // infinite (NaN). Each effect at its longest / most feedback hears one second of quiet noise with a DC offset
    // (-26 dBFS; the offset feeds exactly the lows that ran away, so a slow runaway shows within seconds instead of
    // minutes), then silence, about six minutes in all: its tail must fade the whole time (no 10 s stretch louder
    // than the one before), end at least 40 dB below where it started, and stay under the wet sound's safety
    // rounding (0.8), so that never has to step in.
    struct Case
    {
        const char* name;
        Values v;
        double seconds;
    };
    const std::vector<Case> cases {
        { "shimmer default", space (SpaceType::special, { { spxMix, 100.0f } }), 60.0 },
        { "shimmer +12", space (SpaceType::special, { { spxFeedback, 95.0f }, { spxShift, 12.0f }, { spxTone, 20000.0f }, { spxMotion, 0.0f }, { spxMix, 100.0f } }), 30.0 },
        { "shimmer 0", space (SpaceType::special, { { spxFeedback, 95.0f }, { spxShift, 0.0f }, { spxTone, 20000.0f }, { spxMotion, 0.0f }, { spxMix, 100.0f } }), 30.0 },
        { "shimmer -12", space (SpaceType::special, { { spxFeedback, 95.0f }, { spxShift, -12.0f }, { spxTone, 20000.0f }, { spxMotion, 0.0f }, { spxMix, 100.0f } }), 30.0 },
        { "drift", space (SpaceType::special, { { spxMode, 1.0f }, { spxFeedback, 95.0f }, { spxTone, 20000.0f }, { spxMix, 100.0f } }), 20.0 },
        { "swell", space (SpaceType::special, { { spxMode, 2.0f }, { spxFeedback, 95.0f }, { spxTone, 20000.0f }, { spxMix, 100.0f } }), 20.0 },
        { "freeze", space (SpaceType::special, { { spxMode, 3.0f }, { spxFeedback, 95.0f }, { spxTone, 20000.0f }, { spxMix, 100.0f } }), 40.0 },
        { "reverb", space (SpaceType::reverb, { { revDecay, 10.0f }, { revDamp, 20000.0f }, { revMix, 100.0f } }), 20.0 },
        { "hall", space (SpaceType::hall, { { hallDecay, 12.0f }, { hallSize, 100.0f }, { hallDamp, 20000.0f }, { hallMix, 100.0f } }), 20.0 },
        { "room", space (SpaceType::room, { { roomDecay, 3.0f }, { roomSize, 100.0f }, { roomMix, 100.0f } }), 20.0 },
        { "delay", space (SpaceType::delay, { { dlyFeedback, 95.0f }, { dlyTime, 3.0f }, { dlyTone, 20000.0f }, { dlyPingPong, 100.0f }, { dlyMix, 100.0f } }), 40.0 },
        { "chorus", space (SpaceType::chorus, { { choMix, 100.0f } }), 20.0 },
        { "phaser", space (SpaceType::phaser, { { phaFeedback, 90.0f }, { phaDepth, 100.0f }, { phaMix, 100.0f } }), 20.0 },
    };
    for (const auto& c : cases)
    {
        INFO (c.name);
        auto chain = makeChain (c.v);
        std::mt19937 rng (4);
        std::normal_distribution<double> nd (0.0, 0.035);
        constexpr std::size_t kBlock = 1024;
        const auto window = static_cast<std::size_t> (10.0 * kSr);
        const auto inputEnd = static_cast<std::size_t> (kSr);
        const auto total = inputEnd + static_cast<std::size_t> (c.seconds * kSr);
        std::vector<double> l (kBlock), r (kBlock), peaks;   // the wet sound's peak in each 10 s after the input
        bool finite = true;
        double windowPeak = 0.0;
        for (std::size_t done = 0; done < total; done += kBlock)
        {
            for (std::size_t i = 0; i < kBlock; ++i)
            {
                const bool playing = done + i < inputEnd;
                l[i] = playing ? 0.035 + nd (rng) : 0.0;
                r[i] = playing ? 0.035 + nd (rng) : 0.0;
            }
            chain->setHost (120.0, -1.0);
            double* ch[2] = { l.data(), r.data() };
            chain->process (ch, 2, static_cast<int> (kBlock));
            for (std::size_t i = 0; i < kBlock; ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                if (done + i < inputEnd) continue;
                windowPeak = std::max ({ windowPeak, std::abs (l[i]), std::abs (r[i]) });
                if ((done + i + 1 - inputEnd) % window == 0)
                {
                    peaks.push_back (windowPeak);
                    windowPeak = 0.0;
                }
            }
        }
        REQUIRE (finite);
        REQUIRE (peaks.size() >= 2);
        CHECK (peaks.front() < 0.8);
        bool fading = true;
        for (std::size_t k = 1; k < peaks.size(); ++k) fading = fading && peaks[k] <= peaks[k - 1];
        CHECK (fading);
        CHECK (peaks.back() < 0.01 * peaks.front());
    }
}

TEST_CASE ("Space: switching effects and settings never clicks; off is untouched", "[space]")
{
    std::vector<double> tone (static_cast<std::size_t> (4.0 * kSr));
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.25 * std::sin (2.0 * kPi * 220.0 * static_cast<double> (i) / kSr);
    Chain chain;
    chain.prepare (kSr, 2);
    chain.ensureSpace();
    Run out { tone, tone };
    double maxStep = 0.0;
    int typeIndex = 0;
    for (std::size_t start = 0; start < tone.size(); start += 512)
    {
        if (start % 24000 == 0)   // a new effect every half second
        {
            auto v = space (static_cast<SpaceType> (typeIndex++ % 7));
            v[spRoute] = 0.0f;
            chain.setValues (v);
        }
        chain.setHost (120.0, -1.0);
        double* ch[2] = { out.l.data() + start, out.r.data() + start };
        chain.process (ch, 2, static_cast<int> (std::min<std::size_t> (512, tone.size() - start)));
    }
    for (std::size_t i = 1; i < out.l.size(); ++i) maxStep = std::max (maxStep, std::abs (out.l[i] - out.l[i - 1]));
    CHECK (maxStep < 0.12);   // a 220 Hz sine moves at most 0.007 per sample; effects add some, never a jump

    // Space switched on but its engines not made yet (the host makes them a moment later): the sound passes.
    Chain early;
    early.prepare (kSr, 2);
    auto v = space (SpaceType::hall);
    v[spRoute] = 0.0f;
    early.setValues (v);
    CHECK_FALSE (early.spaceReady());
    const auto passed = play (early, tone);
    CHECK (passed.l == tone);
}

TEST_CASE ("Space: switched on again, or back to an effect, it starts from silence (nothing cleared on the audio thread)", "[space]")
{
    // A delay line forgets what it held on reset (instantly), and fills again from there.
    dsp::DelayLine line;
    line.prepare (1000);
    for (int i = 0; i < 3000; ++i) line.push (1.0);
    line.reset();
    bool silent = true;
    for (double d : { 0.0, 0.5, 1.0, 10.0, 999.0, 1000.0 }) silent = silent && line.read (d) == 0.0;
    CHECK (silent);
    line.push (0.5);
    CHECK (line.read (0.0) == 0.5);
    CHECK (line.read (0.5) == 0.25);   // half way to the silence before it
    CHECK (line.read (1.0) == 0.0);

    // A loud Hall tail, then Space off and on again: the new effect starts from silence (no old tail).
    std::mt19937 rng (13);
    std::normal_distribution<double> nd (0.0, 0.5);
    std::vector<double> noise (static_cast<std::size_t> (kSr));
    for (auto& x : noise) x = nd (rng);
    auto hall = space (SpaceType::hall, { { hallDecay, 12.0f }, { hallMix, 100.0f } });
    auto chain = makeChain (hall);
    play (*chain, noise);
    auto off = hall;
    off[spOn] = 0.0f;
    chain->setValues (off);
    play (*chain, std::vector<double> (4800, 0.0));   // faded out
    chain->setValues (hall);
    const auto again = play (*chain, std::vector<double> (9600, 0.0));
    CHECK (peak (again.l) == 0.0);

    // Hall -> Reverb -> Hall: the Reverb's 60 ms crossfade carries the old tail away, then the Hall starts afresh.
    play (*chain, noise);
    auto plate = hall;
    plate[spType] = static_cast<float> (SpaceType::reverb);
    chain->setValues (plate);
    const auto handover = play (*chain, std::vector<double> (9600, 0.0));
    CHECK (peak (handover.l, 0, 480) > 0.05);   // the Hall still fading out
    chain->setValues (hall);
    const auto back = play (*chain, std::vector<double> (9600, 0.0));
    CHECK (peak (back.l) < 1.0e-4);   // only the wet EQ's last ringing; the old 12 s tail would still be loud
}

TEST_CASE ("Space: any settings, any audio: finite and bounded", "[space]")
{
    std::mt19937 rng (21);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::normal_distribution<double> nd (0.0, 0.4);
    Chain chain;
    chain.prepare (44100.0, 2);
    chain.ensureSpace();
    std::vector<double> l (999), r (999);
    for (int block = 0; block < 300; ++block)
    {
        if (block % 9 == 0)
        {
            Values v;
            for (int i = 0; i < count; ++i)
            {
                const auto& s = specs()[static_cast<std::size_t> (i)];
                v[static_cast<std::size_t> (i)] = s.min + u (rng) * (s.max - s.min);
            }
            v[chainOn] = 1.0f;
            v[spOn] = 1.0f;
            chain.setValues (v);
        }
        for (auto& x : l) x = nd (rng);
        for (auto& x : r) x = nd (rng);
        chain.setHost (60.0 + u (rng) * 140.0, u (rng) > 0.5f ? static_cast<double> (u (rng)) : -1.0);
        double* ch[2] = { l.data(), r.data() };
        chain.process (ch, 2, 999);
        for (double x : l) REQUIRE ((std::isfinite (x) && std::abs (x) < 100.0));
    }
}

TEST_CASE ("Space: its engine is made once, at the right rate, even when two threads ask at the same time", "[space]")
{
    // Some hosts prepare a plug-in off the message thread while its timer makes the Space engine: that could make
    // two engines (one freed while the audio thread used it) or one at the old sample rate.
    for (int round = 0; round < 20; ++round)
    {
        INFO ("round " << round);
        auto chain = std::make_unique<Chain>();
        chain->prepare (48000.0, 2);
        std::thread timer ([&] { chain->ensureSpace(); });
        std::thread host ([&] { chain->prepare (96000.0, 2); chain->ensureSpace(); });
        timer.join();
        host.join();
        REQUIRE (chain->spaceReady());

        // A 1/8 note at 120 BPM: the echo 0.25 s after the click, counted at 96 kHz.
        chain->setValues (space (SpaceType::delay, { { dlyTime, 3.0f }, { dlyPingPong, 0.0f }, { dlyTone, 20000.0f } }));
        play (*chain, std::vector<double> (9600, 0.0));   // faded in
        std::vector<double> click (static_cast<std::size_t> (0.4 * 96000.0));
        click[0] = 0.5;
        const auto out = play (*chain, click);
        const auto echo = static_cast<std::size_t> (std::max_element (out.l.begin(), out.l.end(), [] (double a, double b) { return std::abs (a) < std::abs (b); }) - out.l.begin());
        CHECK (std::abs (static_cast<double> (echo) - 24000.0) <= 2.0);
    }
}

TEST_CASE ("Space before its engine is made: a track passes the sound, an FX channel stays silent", "[space]")
{
    std::vector<double> tone (9600);
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.3 * std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / kSr);
    for (int route : { 0, 1 })
    {
        INFO ("route " << route);
        auto v = space (SpaceType::reverb);
        v[spRoute] = static_cast<float> (route);
        Chain chain;
        chain.prepare (kSr, 2);
        chain.setValues (v);   // no ensureSpace()
        CHECK_FALSE (chain.spaceReady());
        const auto out = play (chain, tone);
        const double level = rmsDb (out.l, 4800, 9600);
        if (route == 0) CHECK (level == Catch::Approx (rmsDb (tone, 4800, 9600)).margin (0.01));
        else CHECK (level < -200.0);
    }
}
