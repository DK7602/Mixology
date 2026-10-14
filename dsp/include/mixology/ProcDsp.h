#pragma once

#include "mixology/TruePeakDetector.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/** Building blocks for the Milestone 5 processing (ProcChain): filters, envelope followers, compressors,
    gate, transient shaper, oversampling and a lookahead limiter. All double precision, up to two channels,
    framework-free. Nothing here allocates after prepare(). */
namespace mixology::dsp
{
    constexpr double kPi = 3.14159265358979323846;

    inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }
    inline double gainToDb (double g) { return g > 1.0e-10 ? 20.0 * std::log10 (g) : -200.0; }
    /** One-pole smoothing: how much of the old value is kept per sample for a time constant in ms. */
    inline double timeCoeff (double ms, double sampleRate) { return ms <= 0.0 ? 0.0 : std::exp (-1.0 / (ms * 0.001 * sampleRate)); }

    // ---------------------------------------------------------------------------------------------
    // Filters (RBJ cookbook; the interface draws its curves with the same formulas)

    enum class Shape { bell, lowShelf, highShelf, lowPass, highPass, allPass };
    struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    Coeffs design (Shape, double hz, double db, double q, double sampleRate);
    double magnitudeDb (const Coeffs&, double hz, double sampleRate);

    /** One biquad for up to two channels (transposed direct form II). */
    struct Filter2
    {
        Coeffs k;
        std::array<std::array<double, 2>, 2> z {};
        void reset() { z = {}; }
        double process (int ch, double x)
        {
            auto& s = z[static_cast<std::size_t> (ch)];
            const double y = k.b0 * x + s[0];
            s[0] = k.b1 * x - k.a1 * y + s[1];
            s[1] = k.b2 * x - k.a2 * y;
            return y;
        }
    };

    /** Linkwitz-Riley 24 dB / octave low or high half of a crossover. LP + HP at the same frequency add up
        to a flat, all-pass response. */
    struct LR4
    {
        Filter2 a, b;
        void set (bool high, double hz, double sampleRate)
        {
            a.k = b.k = design (high ? Shape::highPass : Shape::lowPass, hz, 0.0, 0.70710678118654752, sampleRate);
        }
        void reset() { a.reset(); b.reset(); }
        double process (int ch, double x) { return b.process (ch, a.process (ch, x)); }
    };

    // ---------------------------------------------------------------------------------------------
    // Dynamics

    /** A value in dB that moves fast one way (attack) and slowly back (release). */
    struct Ballistics
    {
        double att = 0.0, rel = 0.0, v = 0.0;
        void set (double attMs, double relMs, double sampleRate) { att = timeCoeff (attMs, sampleRate); rel = timeCoeff (relMs, sampleRate); }
        double step (double target) { const double c = target > v ? att : rel; v = target + (v - target) * c; return v; }
    };

    /** Classic feed-forward compressor (threshold, ratio, knee, attack, release, makeup), linked channels. */
    class Compressor
    {
    public:
        void prepare (double sampleRate) { rate = sampleRate; reset(); }
        void reset() { gr.v = 0.0; }
        void set (double thrDb, double ratio, double attMs, double relMs, double kneeDb, double makeupDb);
        /** The level driving it (linear peak of the linked channels) in, the gain to apply out. */
        double gain (double level);
        double reductionDb() const { return gr.v; }
        static double staticReductionDb (double inDb, double thrDb, double ratio, double kneeDb);

    private:
        double rate = 48000.0, thr = 0.0, ratio = 1.0, knee = 0.0, makeup = 0.0;
        Ballistics gr;
    };

    /** A one-knob compressor that sets its own threshold from the material's average level, so "amount"
        means the same on a quiet and on a loud track. Makes up about 60 % of what it takes off. Used by the
        multiband bands, Glue and Density. Amount 0 = no change. */
    class AutoComp
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void configure (double attMs, double relMs, double ratioSpan, double depthDb);
        double gain (double level, double amount);
        double reductionDb() const { return gr.v; }

    private:
        double rate = 48000.0, rmsC = 0.0, avgC = 0.0, slowC = 0.0, span = 3.0, depth = 10.0;
        double ms = 0.0, avg = 0.0, slowGr = 0.0;
        bool primed = false;
        Ballistics gr;
    };

    /** Attack / sustain shaping from the difference of fast and slow envelopes (level-independent). */
    class TransientShaper
    {
    public:
        void prepare (double sampleRate);
        void reset();
        /** attack, sustain: -1..+1. */
        void set (double attack, double sustain, double speedMs);
        double gain (double level);
        double gainDb() const { return smooth; }

    private:
        struct Env
        {
            double att = 0.0, rel = 0.0, v = 0.0;
            double step (double x) { const double c = x > v ? att : rel; v = x + (v - x) * c; return v; }
        };
        Env aFast, aSlow, sFast, sSlow;
        double rate = 48000.0, attack = 0.0, sustain = 0.0, speed = -1.0, smooth = 0.0, smoothC = 0.0;
    };

    /** Gate (closes by Range below the threshold, 3 dB hysteresis) or 2:1 downward expander (at most Range). */
    class Gate
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void set (double thrDb, double rangeDb, double attMs, double holdMs, double relMs, bool expander);
        double gain (double level);
        double attenuationDb() const { return atten; }

    private:
        double rate = 48000.0, key = 0.0, keyRel = 0.0, thr = -50.0, range = 0.0, attC = 0.0, relC = 0.0, atten = 0.0;
        int holdSamples = 0, hold = 0;
        bool open = true, expander = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Oversampling: polyphase IIR half-band filters (two all-pass chains, designed like Laurent de Soras'
    // HIIR). No latency to report; the phase shift is tiny and the same for dry and processed sound when
    // they are mixed at the high rate.

    class HalfbandIir
    {
    public:
        static constexpr int kMaxCoefs = 12;
        /** transition: the transition band's width as a share of the high sample rate (0 .. 0.5). */
        void design (int numCoefs, double transition);
        void reset();
        void up (double x, double& out0, double& out1);
        double down (double in0, double in1);
        const std::array<double, kMaxCoefs>& coefficients() const { return c; }
        int size() const { return n; }

    private:
        static void stages (int n, const std::array<double, kMaxCoefs>& c, double& s0, double& s1, std::array<double, kMaxCoefs>& x, std::array<double, kMaxCoefs>& y);
        int n = 0;
        std::array<double, kMaxCoefs> c {}, ux {}, uy {}, dx {}, dy {};
    };

    class Oversampler
    {
    public:
        static constexpr int kMaxStages = 3, kMaxIn = 32, kMaxChannels = 2;
        Oversampler();
        void reset();
        /** 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x. A change resets the filters. */
        void setStages (int s);
        int stageCount() const { return stages; }
        int factor() const { return 1 << stages; }
        /** n <= kMaxIn input samples of channel ch in; the n * factor() high-rate samples out (in place, valid
            until the next call for that channel). */
        double* up (int ch, const double* x, int n);
        /** The high-rate buffer of channel ch (after processing) back to n samples. */
        void down (int ch, double* out, int n);

    private:
        int stages = 0;
        std::array<std::array<HalfbandIir, kMaxStages>, kMaxChannels> hb;
        std::array<std::array<double, (kMaxIn << kMaxStages)>, kMaxChannels> buf {}, tmp {};
    };

    // ---------------------------------------------------------------------------------------------
    /** Lookahead peak limiter: the gain is lowered smoothly before a peak arrives (the audio is delayed),
        so nothing passes the ceiling. Sample peaks or true peaks (4x interpolated, BS.1770). Latency is
        fixed (5 ms + 12 samples) whatever the Lookahead knob says; the knob sets how early the gain moves. */
    class LookaheadLimiter
    {
    public:
        static constexpr double kMaxLookMs = 5.0;
        void prepare (double sampleRate, int channels);
        void reset();
        int latency() const { return maxLook + kDetectLag; }
        void set (double inputDb, double ceilingDb, double releaseMs, double lookMs, double character, bool truePeak);
        /** Feeds the delay line without limiting (while the limiter is off), so switching it on continues
            the audio instead of playing old samples. */
        void feed (const double* const* x, int nch, int n);
        /** In place: input gain, delay, limiting. w (0..1, per block start / end) blends the limiting in. */
        void process (double* const* x, int nch, int n, double w0, double w1);
        double reductionDb() const { return lastGr; }

    private:
        static constexpr int kDetectLag = TruePeakDetector::kLatency;
        double detect (int nch, const double* in);   // one frame (input already scaled)
        double rate = 48000.0;
        int channels = 2, maxLook = 240, look = 72, ringSize = 0, pos = 0;
        double ceiling = 0.89, inGain = 1.0, relC = 0.0, g2 = 1.0, boxSum = 0.0, lastGr = 0.0;
        bool truePeak = true, needFresh = true;
        std::vector<double> audio[2];          // input delay line (ringSize)
        std::vector<double> boxRing, gainRing;
        std::vector<long long> dqIdx;          // sliding minimum (monotonic deque, ring of maxLook + 2)
        std::vector<double> dqVal;
        int dqHead = 0, dqTail = 0, boxPos = 0, gainPos = 0;
        long long counter = 0;
        std::array<TruePeakDetector, 2> tp;
    };
}
