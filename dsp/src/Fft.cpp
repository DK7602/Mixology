#include "mixology/Fft.h"

#include <cmath>
#include <utility>

namespace mixology
{
    Fft::Fft (int order)
        : n (1 << order), bitReversed (static_cast<std::size_t> (n)), twiddles (static_cast<std::size_t> (n / 2))
    {
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b)) r |= 1 << (order - 1 - b);
            bitReversed[static_cast<std::size_t> (i)] = r;
        }
        const double pi = 3.14159265358979323846;
        for (int k = 0; k < n / 2; ++k)
            twiddles[static_cast<std::size_t> (k)] = std::polar (1.0f, static_cast<float> (-2.0 * pi * k / n));
    }

    void Fft::forward (std::vector<std::complex<float>>& d) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = bitReversed[static_cast<std::size_t> (i)];
            if (j > i) std::swap (d[static_cast<std::size_t> (i)], d[static_cast<std::size_t> (j)]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int start = 0; start < n; start += len)
                for (int k = 0; k < half; ++k)
                {
                    auto& a = d[static_cast<std::size_t> (start + k)];
                    auto& b = d[static_cast<std::size_t> (start + k + half)];
                    const auto t = b * twiddles[static_cast<std::size_t> (k * step)];
                    b = a - t;
                    a += t;
                }
        }
    }
}
