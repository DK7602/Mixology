#pragma once

#include <array>
#include <atomic>
#include <cstdint>

/** The latest stereo audio, for analysis off the audio thread. The audio thread writes (wait-free); an
    analysis thread copies a snapshot. A copy can overlap a write at the very edge of the ring, which only
    blurs a few samples of one analysis frame: fine for measuring, never for audio. */
namespace mixology
{
    class AudioRing
    {
    public:
        static constexpr int kSize = 16384;   // power of two, >= TrackAnalyzer::kMaxSize

        template <typename Sample>
        void push (const Sample* const* channels, int numChannels, int numSamples) noexcept
        {
            if (numChannels <= 0) return;
            const Sample* l = channels[0];
            const Sample* r = numChannels > 1 ? channels[1] : channels[0];   // mono track: same on both sides
            auto w = writeIndex.load (std::memory_order_relaxed);
            for (int i = 0; i < numSamples; ++i, ++w)
            {
                left[w & (kSize - 1)] = static_cast<float> (l[i]);
                right[w & (kSize - 1)] = static_cast<float> (r[i]);
            }
            writeIndex.store (w, std::memory_order_release);
        }

        /** Copies the newest n frames (n <= kSize), oldest first. */
        void copyLatest (float* l, float* r, int n) const noexcept
        {
            const auto w = writeIndex.load (std::memory_order_acquire);
            for (int i = 0; i < n; ++i)
            {
                const auto k = (w - static_cast<std::uint32_t> (n) + static_cast<std::uint32_t> (i)) & (kSize - 1);
                l[i] = left[k];
                r[i] = right[k];
            }
        }

    private:
        std::array<float, kSize> left {}, right {};
        std::atomic<std::uint32_t> writeIndex { 0 };
    };
}
