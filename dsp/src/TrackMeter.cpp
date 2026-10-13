#include "mixology/TrackMeter.h"

#include <algorithm>

namespace mixology
{
    namespace
    {
        float toDb (double gain) noexcept
        {
            return gain > 1.0e-5 ? static_cast<float> (20.0 * std::log10 (gain)) : TrackMeter::kSilentDb;
        }

        float toLufs (double energy) noexcept
        {
            return energy > 1.0e-10 ? static_cast<float> (-0.691 + 10.0 * std::log10 (energy)) : TrackMeter::kSilentDb;
        }
    }

    void TrackMeter::prepare (double sampleRate, int numChannels)
    {
        channelCount = std::clamp (numChannels, 1, kMaxChannels);
        windowLength = std::max (1, static_cast<int> (std::lround (sampleRate * 0.05)));
        hopLength = std::max (1, static_cast<int> (std::lround (sampleRate * 0.1)));
        for (auto& w : weighting) w.prepare (sampleRate);
        reset();
    }

    void TrackMeter::reset() noexcept
    {
        for (auto& w : weighting) w.reset();
        windowPeak.fill (0.0);
        hopSum.fill (0.0);
        lastPeakDb.fill (kSilentDb);
        hopEnergy.fill (0.0);
        windowPos = hopPos = hopIndex = hopsFilled = 0;
        momentary = shortTerm = kSilentDb;
    }

    void TrackMeter::finishWindow() noexcept
    {
        for (int c = 0; c < kMaxChannels; ++c)
        {
            const auto i = static_cast<std::size_t> (c < channelCount ? c : 0);
            lastPeakDb[static_cast<std::size_t> (c)] = toDb (windowPeak[i]);
        }
        windowPeak.fill (0.0);
        windowPos = 0;
        ++windows;
    }

    void TrackMeter::finishHop() noexcept
    {
        double energy = 0.0;   // BS.1770: channel energies add up (L and R weight 1)
        for (int c = 0; c < channelCount; ++c)
        {
            energy += hopSum[static_cast<std::size_t> (c)] / hopLength;
            hopSum[static_cast<std::size_t> (c)] = 0.0;
        }
        hopPos = 0;
        hopEnergy[static_cast<std::size_t> (hopIndex)] = energy;
        hopIndex = (hopIndex + 1) % kShortTermHops;
        hopsFilled = std::min (hopsFilled + 1, kShortTermHops);

        auto mean = [this] (int hops) {
            double sum = 0.0;
            const int n = std::min (hops, hopsFilled);
            for (int k = 1; k <= n; ++k)
                sum += hopEnergy[static_cast<std::size_t> ((hopIndex - k + kShortTermHops) % kShortTermHops)];
            return n > 0 ? sum / hops : 0.0;   // missing hops count as silence, like a fresh meter
        };
        momentary = toLufs (mean (kMomentaryHops));
        shortTerm = toLufs (mean (kShortTermHops));
    }
}
