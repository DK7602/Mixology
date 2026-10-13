#pragma once

#include <complex>
#include <vector>

/** A small in-place radix-2 complex FFT (no external library: the engine stays framework-free).
    Fast enough for Mixology Link's analysis (one 4096-point transform per track, five times a second). */
namespace mixology
{
    class Fft
    {
    public:
        explicit Fft (int order);   // size = 2^order

        int size() const { return n; }

        /** Forward transform, in place. data.size() must equal size(). */
        void forward (std::vector<std::complex<float>>& data) const;

    private:
        int n = 0;
        std::vector<int> bitReversed;
        std::vector<std::complex<float>> twiddles;
    };
}
