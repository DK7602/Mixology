#pragma once

#include "mixology/ProcDsp.h"
#include "mixology/ProcParams.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

/** Milestone 5: a track's processing (or the full mix's): Gate, EQ, Compressor, Multiband, Transient,
    Tone Balance, Loudness, Harmonics, Analog Touch, Clip Control, Stereo, Final Touch and Limiter, in that
    order. Every control comes from ProcParams.

    - All modules off (the default): the audio is not touched, bit for bit.
    - Switching a module on or off fades it in / out over 20 ms; knob moves glide over about 30 ms. No clicks.
    - Latency: none, except while the Limiter is on (its 5 ms lookahead; latencyFor() says how much, so the
      host can line the track up).
    - Audio thread: setValues() then process(). Never allocates, never locks. */
namespace mixology::proc
{
    class Chain
    {
    public:
        static constexpr int kMaxChannels = 2, kChunk = 32;

        void prepare (double sampleRate, int numChannels);
        void reset();
        /** Where the controls should be (they glide there). */
        void setValues (const Values&);
        const Values& values() const { return target; }
        /** Samples of latency these settings need (the Limiter's lookahead when it is on, else 0). */
        int latencyFor (const Values&) const;
        bool active() const;

        template <typename Sample>
        void process (Sample* const* ch, int numChannels, int numSamples)
        {
            const int nch = std::min ({ numChannels, channels, kMaxChannels });
            if (nch <= 0 || numSamples <= 0) return;
            const bool run = active();
            for (int start = 0; start < numSamples; start += kChunk)
            {
                const int len = std::min (kChunk, numSamples - start);
                for (int c = 0; c < nch; ++c)
                    for (int i = 0; i < len; ++i)
                        work[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = static_cast<double> (ch[c][start + i]);
                if (! run)
                {
                    idle (nch, len);
                    continue;
                }
                processChunk (nch, len);
                for (int c = 0; c < nch; ++c)
                    for (int i = 0; i < len; ++i)
                        ch[c][start + i] = static_cast<Sample> (work[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]);
            }
        }

        /** The meters since the last call (audio thread). */
        Meters takeMeters();

        /** The EQ module's response (dB) at a frequency, as it sounds with these settings. */
        static double eqResponseDb (const Values&, double hz, double sampleRate);
        /** One band's shape after the character (Precision / Smooth / Natural) is applied. */
        struct BandDesign
        {
            bool active = false;
            dsp::Coeffs k;
            int stages = 1;
        };
        static BandDesign designBand (const Values&, int band, double sampleRate);

    private:
        using Buffer = std::array<std::array<double, kChunk>, kMaxChannels>;

        void idle (int nch, int n);
        void processChunk (int nch, int n);
        void glide (int n);
        void resetModule (Module);
        void runModule (Module, int nch, int n);
        void peakInto (float& l, float& r, int nch, int n) const;

        void gate (int nch, int n);
        void eq (int nch, int n);
        void comp (int nch, int n);
        void multi (int nch, int n);
        void trans (int nch, int n);
        void tone (int nch, int n);
        void loud (int nch, int n);
        void harm (int nch, int n);
        void analog (int nch, int n);
        void clip (int nch, int n);
        void stereo (int nch, int n);
        void finalTouch (int nch, int n);

        double v (int id) const { return static_cast<double> (cur[static_cast<std::size_t> (id)]); }
        static void setShelf (dsp::Filter2&, dsp::Shape, double hz, double db, double q, double sampleRate, float& lastDb);

        double rate = 48000.0;
        int channels = 2;
        std::atomic<int> limiterLatency { 0 };
        Values target {}, cur {};
        bool fresh = true;
        std::array<double, kModules> w {};
        Buffer work {}, dry {};
        Meters meters;

        // EQ
        struct Band
        {
            dsp::Filter2 a, b;
            bool active = false;
            int stages = 1;
            std::array<float, 5> last { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };   // type, freq, gain, q, mode
        };
        std::array<Band, kEqBands> bands;

        dsp::Compressor compressor;

        dsp::LR4 lp1, hp1, lp2, hp2;
        dsp::Filter2 ap2;
        std::array<dsp::AutoComp, 3> multiComp;
        float lastX1 = -1.0f, lastX2 = -1.0f;

        dsp::TransientShaper shaper;
        dsp::Filter2 focusFilter;
        int lastFocus = -1;

        dsp::Gate gater;

        dsp::Filter2 warmth, presence, air;
        float warmthDb = 99.0f, presenceDb = 99.0f, airDb = 99.0f;
        dsp::AutoComp toneGlue;
        dsp::TransientShaper punch;

        dsp::Filter2 lowEnd, topEnd;
        float lowEndDb = 99.0f, topEndDb = 99.0f;
        dsp::AutoComp density;

        dsp::Filter2 preLow, preHigh, postLow, postHigh;
        float tiltDb = 99.0f;
        dsp::Oversampler harmOver;
        std::array<double, 2> dcX {}, dcY {};

        dsp::Oversampler analogOver;
        dsp::Filter2 bump, hfLoss, noiseTone;
        float bumpDb = 99.0f, hfDb = 99.0f;
        std::uint32_t rng = 0x9e3779b9u;
        double noiseEnv = 0.0;

        dsp::Oversampler clipOver;

        dsp::LR4 sideLp1, sideHp1, sideLp2, sideHp2;   // the side signal in three bands
        dsp::Filter2 sideAp2, midAp1, midAp2;         // all-passes that keep every part in phase
        float lastLowX = -1.0f, lastHighX = -1.0f;

        dsp::Filter2 finalAir;
        float finalAirDb = 99.0f;
        dsp::AutoComp finalGlue;

        dsp::LookaheadLimiter limiter;
    };
}
