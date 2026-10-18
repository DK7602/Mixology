#include "mixology/ProcChain.h"
#include "mixology/ProcParams.h"
#include "mixology/SpaceFx.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
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

TEST_CASE ("Space: the tail the host is told is one long, finite time that covers every setting", "[space]")
{
    // Hosts only ask for the tail again when the latency changes, so a tail that followed the settings never reached
    // them when Space went on mid-song (Cubase then cut the reverb off), and Freeze's endless one went to AU hosts
    // as infinity. Now it is one time, longer than any effect rings at its longest.
    CHECK (std::isfinite (Chain::kTailSeconds));
    const std::vector<Values> longest {
        space (SpaceType::reverb, { { revDecay, 10.0f }, { revPre, 200.0f } }),
        space (SpaceType::hall, { { hallDecay, 12.0f }, { hallPre, 200.0f } }),
        space (SpaceType::room, { { roomDecay, 3.0f }, { roomPre, 100.0f } }),
        space (SpaceType::delay, { { dlyFeedback, 95.0f }, { dlyTime, 8.0f } }),
        space (SpaceType::special, { { spxFeedback, 95.0f } }),
        space (SpaceType::special, { { spxMode, 3.0f }, { spxFeedback, 95.0f } }),
        space (SpaceType::chorus),
        space (SpaceType::phaser),
    };
    for (const auto& v : longest)
    {
        INFO ("type " << v[spType] << ", mode " << v[spxMode]);
        CHECK (Chain::tailSecondsFor (v) <= Chain::kTailSeconds);
    }
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

TEST_CASE ("Space: a reset (the host stops or jumps) silences the tails at once", "[space]")
{
    // Both plug-ins now pass the host's reset (AU Reset, VST3 processing off) on to their chain, on the audio
    // thread at the next block: instant now that nothing is cleared.
    std::mt19937 rng (17);
    std::normal_distribution<double> nd (0.0, 0.3);
    std::vector<double> noise (static_cast<std::size_t> (0.5 * kSr));
    for (auto& x : noise) x = nd (rng);
    for (const auto& v : { space (SpaceType::hall, { { hallDecay, 12.0f }, { hallMix, 100.0f } }),
                           space (SpaceType::delay, { { dlyFeedback, 95.0f }, { dlyMix, 100.0f } }),
                           space (SpaceType::special, { { spxFeedback, 95.0f }, { spxMix, 100.0f } }) })
    {
        INFO ("type " << v[spType]);
        auto chain = makeChain (v);
        play (*chain, noise);
        const auto ringing = play (*chain, std::vector<double> (4800, 0.0));
        CHECK (peak (ringing.l) > 1.0e-3);   // the tail rings on ...
        chain->reset();
        const auto after = play (*chain, std::vector<double> (9600, 0.0));
        CHECK (peak (after.l) == 0.0);       // ... until the reset
    }
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

// ---------------------------------------------------------------------------------------------
// Space layers: up to three effects at once on one track.

namespace
{
    /** Space on with exactly these effects switched on (FX channel unless `route` says otherwise, wet EQ wide open). */
    Values layers (std::initializer_list<SpaceType> on, std::initializer_list<std::pair<Id, float>> changes = {}, float route = 1.0f)
    {
        auto v = space (SpaceType::reverb, changes);
        v[spRoute] = route;
        for (auto t : on) v[static_cast<std::size_t> (effectOn (t))] = 1.0f;
        for (auto [id, value] : changes) v[static_cast<std::size_t> (id)] = value;
        return v;
    }

    std::vector<double> noiseOf (double seconds, double sd, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::normal_distribution<double> nd (0.0, sd);
        std::vector<double> x (static_cast<std::size_t> (seconds * kSr));
        for (auto& s : x) s = nd (rng);
        return x;
    }

    double largestDifference (const std::vector<double>& a, const std::vector<double>& b)
    {
        double d = 0.0;
        for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i) d = std::max (d, std::abs (a[i] - b[i]));
        return d;
    }
}

TEST_CASE ("Space layers: two effects at once are both heard, Chorus / Phaser first, the others side by side", "[space][layers]")
{
    const auto noise = noiseOf (1.0, 0.1, 31);

    // Side by side: Delay + Hall on an FX channel is exactly Delay alone plus Hall alone (each at full, same input).
    {
        const auto both = play (*makeChain (layers ({ SpaceType::delay, SpaceType::hall })), noise);
        const auto delay = play (*makeChain (layers ({ SpaceType::delay })), noise);
        const auto hall = play (*makeChain (layers ({ SpaceType::hall })), noise);
        std::vector<double> sum (noise.size());
        for (std::size_t i = 0; i < sum.size(); ++i) sum[i] = delay.l[i] + hall.l[i];
        CHECK (largestDifference (both.l, sum) < 1.0e-9);
        CHECK (rmsDb (delay.l, 24000, 48000) > -40.0);   // both really play
        CHECK (rmsDb (hall.l, 24000, 48000) > -40.0);

        // On a track each is scaled by its own Mix and the dry sound makes room for each, as one effect's MIX always
        // did: dry x (1 - 30 %) x (1 - 20 %) + Delay x 30 % + Hall x 20 %.
        auto track = layers ({ SpaceType::delay, SpaceType::hall }, { { dlyMix, 30.0f }, { hallMix, 20.0f } }, 0.0f);
        const auto mixed = play (*makeChain (track), noise);
        std::vector<double> want (noise.size());
        for (std::size_t i = 0; i < want.size(); ++i) want[i] = 0.7 * 0.8 * noise[i] + 0.3 * delay.l[i] + 0.2 * hall.l[i];
        CHECK (largestDifference (mixed.l, want) < 1.0e-9);
    }

    // The user's Phaser + Hall on one track: the Phaser changes the sound while it plays, the Hall rings on after.
    {
        std::vector<double> burst (static_cast<std::size_t> (1.5 * kSr));
        std::copy (noise.begin(), noise.begin() + 24000, burst.begin());
        const auto onTrack = [] (std::initializer_list<SpaceType> on) { return layers (on, { { phaMix, 50.0f }, { hallMix, 30.0f } }, 0.0f); };
        const auto both = play (*makeChain (onTrack ({ SpaceType::phaser, SpaceType::hall })), burst);
        const auto phaser = play (*makeChain (onTrack ({ SpaceType::phaser })), burst);
        const auto hall = play (*makeChain (onTrack ({ SpaceType::hall })), burst);
        std::vector<double> diff (24000);
        for (std::size_t i = 0; i < diff.size(); ++i) diff[i] = both.l[i] - hall.l[i];
        CHECK (rmsDb (diff, 4800, 24000) > rmsDb (burst, 4800, 24000) - 20.0);   // the Phaser is heard ...
        CHECK (rmsDb (both.l, 36000, 60000) > -70.0);                            // ... and the Hall's tail
        CHECK (rmsDb (phaser.l, 36000, 60000) < -150.0);                         // (a Phaser alone has none)
    }

    // The order: Chorus comes first, so the Delay repeats the chorused sound (its echo arrives the Chorus's 12 ms
    // later than the Delay's own 1/8). With the Delay on, an FX channel plays only the Delay: no Chorus of its own.
    {
        const auto click = impulse (0.5);
        const auto delayOnly = layers ({ SpaceType::delay }, { { dlyFeedback, 0.0f }, { dlyPingPong, 0.0f }, { dlyTone, 20000.0f } });
        auto chorusFirst = delayOnly;
        chorusFirst[static_cast<std::size_t> (effectOn (SpaceType::chorus))] = 1.0f;
        const auto plain = play (*makeChain (delayOnly), click);
        const auto layered = play (*makeChain (chorusFirst), click);
        const auto at = [] (double t) { return static_cast<std::size_t> (t * kSr); };
        CHECK (peak (plain.l, at (0.249), at (0.251)) > 0.3);                  // the Delay alone: right on the 1/8
        CHECK (peak (layered.l, at (0.249), at (0.251)) < 1.0e-6);             // after the Chorus: later
        CHECK (peak (layered.l, at (0.258), at (0.28)) > 0.02);
        CHECK (peak (layered.l, 0, at (0.2)) < 1.0e-9);                        // no Chorus sound of its own, no dry
    }
}

TEST_CASE ("Space layers: at most three effects play, the first three in the chain's order", "[space][layers]")
{
    auto all = layers ({ SpaceType::reverb, SpaceType::delay, SpaceType::chorus, SpaceType::phaser, SpaceType::hall, SpaceType::room, SpaceType::special });
    CHECK (spaceLayers (all) == (bitOf (SpaceType::chorus) | bitOf (SpaceType::phaser) | bitOf (SpaceType::delay)));
    CHECK (effectNames (spaceLayers (all)) == "Chorus + Phaser + Delay");
    auto spaces = layers ({ SpaceType::special, SpaceType::room, SpaceType::hall, SpaceType::reverb });
    CHECK (spaceLayers (spaces) == (bitOf (SpaceType::reverb) | bitOf (SpaceType::hall) | bitOf (SpaceType::room)));
    CHECK (reverbTypeCount (spaceLayers (spaces)) == 3);
    CHECK (effectCount (spaceLayers (spaces)) == 3);

    // The chain plays what spaceLayers() says, nothing more (a damaged project or automation can't run all seven).
    const auto noise = noiseOf (0.5, 0.1, 41);
    const auto seven = play (*makeChain (all), noise);
    const auto three = play (*makeChain (layers ({ SpaceType::chorus, SpaceType::phaser, SpaceType::delay })), noise);
    CHECK (seven.l == three.l);
    CHECK (Chain::tailSecondsFor (all) == Chain::tailSecondsFor (layers ({ SpaceType::chorus, SpaceType::phaser, SpaceType::delay })));

    // Off is off: Space switched off, or all processing.
    auto off = all;
    off[spOn] = 0.0f;
    CHECK (spaceEffects (off) == 0u);
    CHECK (spaceLayers (off) != 0u);   // what it would play
    auto chainOff = all;
    chainOff[chainOn] = 0.0f;
    CHECK (spaceEffects (chainOff) == 0u);
    CHECK (Chain::tailSecondsFor (off) == 0.0);

    // The tail: Chorus and Phaser first (their short tails add), the rest side by side (the longest counts).
    const auto longHall = layers ({ SpaceType::chorus, SpaceType::delay, SpaceType::hall }, { { hallDecay, 10.0f }, { hallPre, 100.0f } });
    CHECK (Chain::tailSecondsFor (longHall) == Catch::Approx (0.1 + 0.1 + 13.0));
    const auto longest = layers ({ SpaceType::chorus, SpaceType::phaser, SpaceType::special }, { { spxMode, 3.0f }, { spxFeedback, 95.0f } });
    CHECK (Chain::tailSecondsFor (longest) <= Chain::kTailSeconds);

    // What plans and presets write to get exactly these effects: every switch, Space on, Space Effect one of them.
    const auto c = spaceControls (bitOf (SpaceType::chorus) | bitOf (SpaceType::hall), SpaceType::reverb);
    auto v = all;
    for (auto [id, value] : c) v[static_cast<std::size_t> (id)] = value;
    CHECK (spaceEffects (v) == (bitOf (SpaceType::chorus) | bitOf (SpaceType::hall)));
    CHECK (v[spType] == static_cast<float> (SpaceType::chorus));
    for (auto [id, value] : spaceControls (bitOf (SpaceType::hall), SpaceType::hall)) v[static_cast<std::size_t> (id)] = value;
    CHECK (spaceEffects (v) == bitOf (SpaceType::hall));
    CHECK (v[spType] == static_cast<float> (SpaceType::hall));
    for (auto [id, value] : spaceControls (0u, SpaceType::hall)) v[static_cast<std::size_t> (id)] = value;
    CHECK (spaceEffects (v) == 0u);
}

TEST_CASE ("Space layers: a project or preset from before (no effect switched on) plays Space Effect's effect alone", "[space][layers]")
{
    // The on switches are new controls at the end of the table: everything before them keeps its place.
    CHECK (indexOf ("sp_level") == spLevel);
    CHECK (spLevel == revOn - 1);
    CHECK (std::string (specs()[static_cast<std::size_t> (revOn)].id) == "rev_on");
    CHECK (std::string (specs()[static_cast<std::size_t> (spxOn)].id) == "spx_on");
    for (int t = 0; t < kSpaceTypes; ++t) CHECK (defaults()[static_cast<std::size_t> (revOn + t)] == 0.0f);

    const auto noise = noiseOf (0.6, 0.15, 51);
    for (int route : { 0, 1 })
        for (int t = 0; t < kSpaceTypes; ++t)
        {
            INFO ("type " << t << ", route " << route);
            auto old = space (static_cast<SpaceType> (t));   // Space on, Space Effect = t, no switch on
            old[spRoute] = static_cast<float> (route);
            CHECK (spaceLayers (old) == (1u << t));
            CHECK (spaceEffects (old) == (1u << t));
            auto now = old;
            now[static_cast<std::size_t> (revOn + t)] = 1.0f;
            const auto a = play (*makeChain (old), noise);
            const auto b = play (*makeChain (now), noise);
            CHECK (a.l == b.l);
            CHECK (a.r == b.r);
            CHECK (Chain::tailSecondsFor (old) == Chain::tailSecondsFor (now));
        }

    // A switch that is on wins over Space Effect (which then only says which effect the page shows).
    auto v = space (SpaceType::hall);
    v[static_cast<std::size_t> (effectOn (SpaceType::delay))] = 1.0f;
    CHECK (spaceLayers (v) == bitOf (SpaceType::delay));
    v[static_cast<std::size_t> (effectOn (SpaceType::delay))] = 0.0f;
    v[spType] = 9.0f;
    CHECK (spaceLayers (v) == bitOf (SpaceType::special));   // out of range: the last one
    v[spType] = std::numeric_limits<float>::quiet_NaN();
    CHECK (spaceLayers (v) == bitOf (SpaceType::reverb));    // a damaged value: the first effect, never nothing
}

TEST_CASE ("Space layers: adding or removing an effect never clicks; one switched off fades out quickly", "[space][layers]")
{
    std::vector<double> tone (static_cast<std::size_t> (4.0 * kSr));
    for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = 0.25 * std::sin (2.0 * kPi * 227.3 * static_cast<double> (i) / kSr);   // never at 0 when a switch comes
    // A track (Mix blends each in) and an FX channel (only the effects come out): every quarter second one effect
    // goes on or off, never more than three on.
    const std::vector<std::vector<SpaceType>> steps {
        { SpaceType::hall }, { SpaceType::hall, SpaceType::chorus }, { SpaceType::hall, SpaceType::chorus, SpaceType::delay },
        { SpaceType::chorus, SpaceType::delay }, { SpaceType::chorus, SpaceType::delay, SpaceType::phaser }, { SpaceType::delay, SpaceType::phaser },
        { SpaceType::delay, SpaceType::phaser, SpaceType::room }, { SpaceType::phaser, SpaceType::room }, { SpaceType::room },
        { SpaceType::room, SpaceType::special }, { SpaceType::room, SpaceType::special, SpaceType::reverb }, { SpaceType::special, SpaceType::reverb },
        { SpaceType::reverb }, { SpaceType::reverb, SpaceType::chorus }, { SpaceType::chorus }, { SpaceType::chorus, SpaceType::hall },
    };
    for (int route : { 0, 1 })
    {
        INFO ("route " << route);
        const auto mixes = { std::pair { revMix, 40.0f }, std::pair { dlyMix, 40.0f }, std::pair { choMix, 50.0f }, std::pair { phaMix, 50.0f },
                             std::pair { hallMix, 40.0f }, std::pair { roomMix, 40.0f }, std::pair { spxMix, 40.0f } };
        Chain chain;
        chain.prepare (kSr, 2);
        chain.ensureSpace();
        Run out { tone, tone };
        std::size_t step = 0;
        for (std::size_t start = 0; start < tone.size(); start += 256)
        {
            if (start % 12000 == 0 && step < steps.size())
            {
                auto v = layers ({}, {}, static_cast<float> (route));
                for (auto [id, value] : mixes) v[static_cast<std::size_t> (id)] = value;
                for (auto t : steps[step]) v[static_cast<std::size_t> (effectOn (t))] = 1.0f;
                ++step;
                chain.setValues (v);
            }
            chain.setHost (120.0, -1.0);
            double* ch[2] = { out.l.data() + start, out.r.data() + start };
            chain.process (ch, 2, static_cast<int> (std::min<std::size_t> (256, tone.size() - start)));
        }
        // A click is a sudden step: it shows in the second difference, which stays small for these smooth sounds
        // (a switch without the fade: about 0.04 on the track, 0.008 on the FX channel).
        double bend = 0.0;
        for (std::size_t i = 4800; i < out.l.size(); ++i)
            for (const auto* x : { &out.l, &out.r })
                bend = std::max (bend, std::abs ((*x)[i] - 2.0 * (*x)[i - 1] + (*x)[i - 2]));
        CHECK (bend < (route == 0 ? 0.01 : 0.002));
    }

    // Switched off, an effect fades out within 60 ms, its tail with it (as a change of effect always did): a 12 s
    // Hall on an FX channel (with a Chorus left on), the sound stops and the Hall goes off at the same moment.
    const auto hallChorus = layers ({ SpaceType::hall, SpaceType::chorus }, { { hallDecay, 12.0f } });
    auto chorusOnly = hallChorus;
    chorusOnly[static_cast<std::size_t> (effectOn (SpaceType::hall))] = 0.0f;
    const auto burst = noiseOf (0.5, 0.3, 61);
    auto kept = makeChain (hallChorus);
    auto dropped = makeChain (hallChorus);
    play (*kept, burst);
    play (*dropped, burst);
    dropped->setValues (chorusOnly);
    const auto ringing = play (*kept, std::vector<double> (9600, 0.0));
    const auto gone = play (*dropped, std::vector<double> (9600, 0.0));
    CHECK (peak (ringing.l, 4800, 9600) > 1.0e-2);   // left on, the Hall rings on
    CHECK (peak (gone.l, 4800, 9600) < 1.0e-4);      // switched off, gone 100 ms later (the Chorus's last ringing)
}

TEST_CASE ("Space layers: an FX channel never plays the dry sound", "[space][layers]")
{
    // An FX channel with a song playing (quiet noise) while its effects change every half second. A click added to
    // that song, during a change (20 ms in, while effects fade) or after it: what the click adds to the output (this
    // run against the run without it; the effects are linear at this level) must stay silent for the first 5 ms
    // (the earliest any effect answers is the Room's 8 ms pre-delay), so no dry sound comes through. (A Phaser whose
    // own sound comes back answers at once, with its sweeping copy: checked below.)
    const std::vector<std::vector<SpaceType>> sets {
        { SpaceType::chorus }, { SpaceType::chorus, SpaceType::hall }, { SpaceType::phaser, SpaceType::hall },
        { SpaceType::phaser, SpaceType::hall, SpaceType::delay }, { SpaceType::chorus, SpaceType::delay, SpaceType::reverb },
        { SpaceType::delay, SpaceType::reverb, SpaceType::room }, { SpaceType::chorus, SpaceType::room, SpaceType::special },
        { SpaceType::chorus, SpaceType::special }, { SpaceType::chorus }, { SpaceType::hall }, { SpaceType::chorus },
        { SpaceType::chorus, SpaceType::phaser },
    };
    const auto valuesFor = [] (const std::vector<SpaceType>& on) {
        auto v = layers ({});
        for (auto t : on) v[static_cast<std::size_t> (effectOn (t))] = 1.0f;
        return v;
    };
    const auto song = noiseOf (static_cast<double> (sets.size()) * 0.5, 0.03, 81);
    const auto run = [&] (std::size_t clickAt) {
        auto in = song;
        if (clickAt < in.size()) in[clickAt] += 0.5;
        auto chain = makeChain (valuesFor (sets[0]));
        Run out { in, in };
        for (std::size_t start = 0; start < in.size(); start += 240)
        {
            if (start % 24000 == 0) chain->setValues (valuesFor (sets[start / 24000]));
            chain->setHost (120.0, -1.0);
            double* ch[2] = { out.l.data() + start, out.r.data() + start };
            chain->process (ch, 2, 240);
        }
        return out;
    };
    const auto without = run (song.size());
    double dry = 0.0, answer = 1.0;
    for (std::size_t k = 0; k < sets.size(); ++k)
        for (std::size_t at : { k * 24000 + 960, k * 24000 + 14400 })
        {
            INFO ("set " << k << ", click at " << at);
            const auto with = run (at);
            double early = 0.0, later = 0.0;
            for (std::size_t i = at; i < std::min (song.size(), at + 19200); ++i)
                (i < at + 240 ? early : later) = std::max (i < at + 240 ? early : later, std::max (std::abs (with.l[i] - without.l[i]), std::abs (with.r[i] - without.r[i])));
            dry = std::max (dry, early);
            answer = std::min (answer, later);
            CHECK (early < 1.0e-9);
        }
    CHECK (dry < 1.0e-9);
    CHECK (answer > 1.0e-3);   // and every click is answered by the effects

    // A Phaser alone there plays its sweeping copy only: what it plays on a track at Mix 100 %, minus the dry half
    // the track already plays (before layers an FX channel's Phaser sent that half back too: the dry sound twice).
    const auto noise = noiseOf (0.5, 0.1, 71);
    auto onTrack = layers ({ SpaceType::phaser }, { { phaMix, 100.0f } }, 0.0f);
    auto onFx = layers ({ SpaceType::phaser });
    const auto t = play (*makeChain (onTrack), noise);
    const auto f = play (*makeChain (onFx), noise);
    dsp::Filter2 lc, hc;   // the wet EQ (wide open, still a filter)
    lc.k = dsp::design (dsp::Shape::highPass, 20.0, 0.0, 0.7071, kSr);
    hc.k = dsp::design (dsp::Shape::lowPass, 20000.0, 0.0, 0.7071, kSr);
    double worst = 0.0;
    for (std::size_t i = 0; i < noise.size(); ++i)
        worst = std::max (worst, std::abs ((t.l[i] - f.l[i]) - 0.5 * hc.process (0, lc.process (0, noise[i]))));
    CHECK (worst < 1.0e-9);
    CHECK (rmsDb (f.l, 4800, 24000) > rmsDb (noise, 4800, 24000) - 10.0);   // and it does play
}

TEST_CASE ("Space layers: every feedback still dies away with three effects at their most", "[space][layers]")
{
    // As the single-effect test above, with three effects at their longest / most feedback together (Chorus and
    // Phaser feed the others, which ring side by side): a second of quiet noise with a DC offset, then silence. The
    // tail fades the whole time (5 s windows), ends at least 40 dB below where it started, stays finite.
    struct Case
    {
        const char* name;
        Values v;
        double seconds;
    };
    const std::vector<Case> cases {
        { "chorus + phaser + delay", layers ({ SpaceType::chorus, SpaceType::phaser, SpaceType::delay },
                                             { { phaFeedback, 90.0f }, { phaDepth, 100.0f }, { dlyFeedback, 95.0f }, { dlyTime, 1.0f }, { dlyTone, 20000.0f } }), 20.0 },
        { "phaser + delay + shimmer", layers ({ SpaceType::phaser, SpaceType::delay, SpaceType::special },
                                              { { phaFeedback, 90.0f }, { dlyFeedback, 95.0f }, { dlyTime, 1.0f }, { dlyTone, 20000.0f }, { spxFeedback, 95.0f }, { spxShift, 12.0f }, { spxTone, 20000.0f } }), 20.0 },
        { "delay + hall + drift", layers ({ SpaceType::delay, SpaceType::hall, SpaceType::special },
                                          { { dlyFeedback, 95.0f }, { dlyTime, 1.0f }, { hallDecay, 12.0f }, { hallSize, 100.0f }, { hallDamp, 20000.0f }, { spxMode, 1.0f }, { spxFeedback, 95.0f } }), 20.0 },
        { "reverb + room + shimmer 0", layers ({ SpaceType::reverb, SpaceType::room, SpaceType::special },
                                               { { revDecay, 10.0f }, { revDamp, 20000.0f }, { roomDecay, 3.0f }, { roomSize, 100.0f }, { spxFeedback, 95.0f }, { spxShift, 0.0f }, { spxMotion, 0.0f } }), 20.0 },
        { "on a track: chorus + phaser + shimmer", layers ({ SpaceType::chorus, SpaceType::phaser, SpaceType::special },
                                                         { { choMix, 100.0f }, { phaMix, 100.0f }, { phaFeedback, 90.0f }, { spxMix, 100.0f }, { spxFeedback, 95.0f }, { spxShift, -12.0f } }, 0.0f), 20.0 },
    };
    for (const auto& c : cases)
    {
        INFO (c.name);
        auto chain = makeChain (c.v);
        std::mt19937 rng (4);
        std::normal_distribution<double> nd (0.0, 0.035);
        constexpr std::size_t kBlock = 1024;
        const auto window = static_cast<std::size_t> (5.0 * kSr);
        const auto inputEnd = static_cast<std::size_t> (kSr);
        const auto total = inputEnd + static_cast<std::size_t> (c.seconds * kSr);
        std::vector<double> l (kBlock), r (kBlock), peaks;
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
        CHECK (peaks.front() < 0.8 * effectCount (spaceLayers (c.v)));   // each under its safety rounding
        bool fading = true;
        for (std::size_t k = 1; k < peaks.size(); ++k) fading = fading && peaks[k] <= peaks[k - 1];
        CHECK (fading);
        CHECK (peaks.back() < 0.01 * peaks.front());
    }
}
