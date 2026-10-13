#pragma once

#include <array>
#include <atomic>
#include <cstdint>

/** Lock-free ring of the latest left / right samples for the vectorscope and correlation meter.
    The audio thread writes, the UI thread reads; a torn read at the buffer edge only blurs one dot. */
class StereoTap
{
public:
    static constexpr int kSize = 4096;   // power of two

    template <typename Sample>
    void push (const Sample* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0) return;
        const Sample* l = channels[0];
        const Sample* r = numChannels > 1 ? channels[1] : channels[0];
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
