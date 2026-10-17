#pragma once

#include <array>
#include <cmath>

/** Mixology Link's Assist EQ: the three moves Mixology's smart tools make on a track.

    - Low Cut: a 12 dB / octave high-pass (Clean Up takes rumble out of tracks that don't need lows).
    - Mud: a broad dip in the low mids (Clean Up).
    - Pocket: a broad dip where the lead vocal's words live (Vocal Pocket, on the instruments covering it).

    Neutral settings pass the audio through bit for bit. Changes glide over about 30 ms (the coefficients are
    recomputed every 32 samples while gliding), so Apply never clicks. Audio thread: never allocates. */
namespace mixology
{
    struct AssistSettings
    {
        static constexpr float kLowCutOff = 20.0f;   // below this the low cut is off
        float lowCutHz = 0.0f;                       // 0 = off, else 20..500 Hz
        float mudHz = 300.0f, mudDb = 0.0f;          // 120..800 Hz, -12..0 dB
        float pocketHz = 2500.0f, pocketDb = 0.0f;   // 800..8000 Hz, -12..0 dB

        bool lowCutOn() const { return lowCutHz >= kLowCutOff; }
        bool neutral() const { return ! lowCutOn() && mudDb > -0.01f && pocketDb > -0.01f; }
        bool operator== (const AssistSettings&) const = default;
    };

    class AssistEq
    {
    public:
        static constexpr int kMaxChannels = 2;
        static constexpr int kChunk = 32;
        static constexpr double kMudQ = 1.0, kPocketQ = 1.2;

        void prepare (double sampleRate, int numChannels);
        void reset() noexcept;
        /** Audio thread, before process(): where the settings should glide to. */
        void setTarget (const AssistSettings& s) noexcept { target = s; }
        bool active() const noexcept { return stages[0].on || stages[1].on || stages[2].on || gliding || ! (current == target); }

        template <typename Sample>
        void process (Sample* const* channels, int numChannels, int numSamples) noexcept
        {
            if (! active()) return;
            const int nch = numChannels < channelCount ? numChannels : channelCount;
            for (int start = 0; start < numSamples; start += kChunk)
            {
                glide();
                const int end = start + kChunk < numSamples ? start + kChunk : numSamples;
                for (auto& st : stages)
                {
                    if (! st.on) continue;
                    const auto k = st.k;
                    for (int c = 0; c < nch; ++c)
                    {
                        auto& z = st.z[static_cast<std::size_t> (c)];
                        Sample* x = channels[c];
                        double z1 = z[0], z2 = z[1];
                        for (int i = start; i < end; ++i)
                        {
                            const double in = static_cast<double> (x[i]);
                            const double out = k.b0 * in + z1;
                            z1 = k.b1 * in - k.a1 * out + z2;
                            z2 = k.b2 * in - k.a2 * out;
                            x[i] = static_cast<Sample> (out);
                        }
                        z[0] = z1;
                        z[1] = z2;
                        if (! std::isfinite (z1 + z2))   // it broke (a sample that was not a number): again from silence
                        {
                            z = { 0.0, 0.0 };
                            for (int i = start; i < end; ++i) x[i] = Sample (0);
                        }
                    }
                }
            }
        }

        /** The Assist EQ's response (dB) at a frequency: for the interface and the tests. */
        static double responseDb (const AssistSettings&, double hz, double sampleRate);

    private:
        struct Coeffs { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
        struct Stage
        {
            bool on = false;
            Coeffs k;
            std::array<std::array<double, 2>, kMaxChannels> z {};
        };
        static Coeffs highPass (double hz, double sampleRate);
        static Coeffs peak (double hz, double db, double q, double sampleRate);
        static double magnitudeDb (const Coeffs&, double hz, double sampleRate);
        void glide() noexcept;
        void updateStages() noexcept;

        static constexpr float kParkedHz = 10.0f;
        double rate = 48000.0, glideCoeff = 0.1;
        float lowCutHz = kParkedHz;   // where the low cut is while it glides
        int channelCount = 2;
        AssistSettings target, current;
        bool gliding = false;
        std::array<Stage, 3> stages;   // low cut, mud, pocket
    };
}
