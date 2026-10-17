#pragma once

#include "KWeighting.h"

#include <array>
#include <cmath>
#include <cstdint>

/** What Mixology Link measures on its track: sample peaks per 50 ms window and BS.1770 momentary (400 ms)
    and short-term (3 s) loudness. Cheap enough to run on every track (no true-peak oversampling, no
    gating statistics: the main plug-in does those on the whole mix). Audio thread only, never allocates. */
namespace mixology
{
    class TrackMeter
    {
    public:
        static constexpr int kMaxChannels = 2;     // a third or later channel is ignored
        static constexpr float kSilentDb = -100.0f;

        void prepare (double sampleRate, int numChannels);
        void reset() noexcept;

        template <typename Sample>
        void process (const Sample* const* channels, int numChannels, int numSamples) noexcept
        {
            const int nch = numChannels < channelCount ? numChannels : channelCount;
            for (int i = 0; i < numSamples; ++i)
            {
                for (int c = 0; c < nch; ++c)
                {
                    double x = static_cast<double> (channels[c][i]);
                    if (! std::isfinite (x)) x = 0.0;   // not a number (NaN, infinity): it would stop the loudness for good
                    const double ax = std::abs (x);
                    auto& pk = windowPeak[static_cast<std::size_t> (c)];
                    if (ax > pk) pk = ax;
                    const double k = weighting[static_cast<std::size_t> (c)].process (x);
                    hopSum[static_cast<std::size_t> (c)] += k * k;
                }
                if (++windowPos == windowLength) finishWindow();
                if (++hopPos == hopLength) finishHop();
            }
        }

        /** Bumps every time a 50 ms window completes (publish then). */
        std::uint32_t windowCount() const noexcept { return windows; }
        float peakDb (int channel) const noexcept { return lastPeakDb[static_cast<std::size_t> (channel < channelCount ? channel : 0)]; }
        float momentaryLufs() const noexcept { return momentary; }
        float shortTermLufs() const noexcept { return shortTerm; }
        int channels() const noexcept { return channelCount; }

    private:
        void finishWindow() noexcept;
        void finishHop() noexcept;

        static constexpr int kShortTermHops = 30;   // 3 s of 100 ms hops
        static constexpr int kMomentaryHops = 4;    // 400 ms

        int channelCount = 2;
        int windowLength = 2400, windowPos = 0;
        int hopLength = 4800, hopPos = 0;
        std::array<KWeighting, kMaxChannels> weighting {};
        std::array<double, kMaxChannels> windowPeak {}, hopSum {};
        std::array<float, kMaxChannels> lastPeakDb { kSilentDb, kSilentDb };
        std::array<double, kShortTermHops> hopEnergy {};
        int hopIndex = 0, hopsFilled = 0;
        float momentary = kSilentDb, shortTerm = kSilentDb;
        std::uint32_t windows = 0;
    };
}
