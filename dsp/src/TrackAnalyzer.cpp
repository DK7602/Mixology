#include "mixology/TrackAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace mixology
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kSineRef = 3.0103;   // a full-scale sine has a mean square of 1/2: report it as 0 dB

        float energyDb (double e, double offset = 0.0)
        {
            return e > 1.0e-14 ? static_cast<float> (10.0 * std::log10 (e) + offset) : kSilentDb;
        }

        // BS.1770 K-weighting as a magnitude curve: the head-effect shelf (+4 dB above ~1.7 kHz) and the
        // RLB high-pass (~38 Hz, 2nd order). Close enough for comparing tracks with each other.
        double kWeightPower (double hz)
        {
            const double shelfDb = 4.0 / (1.0 + std::pow (1681.0 / std::max (hz, 1.0), 2.0));
            const double r = hz / 38.0;
            const double hp = (r * r * r * r) / (1.0 + r * r * r * r);
            return std::pow (10.0, shelfDb / 10.0) * hp;
        }
    }

    double bandHz (int band)
    {
        return 1000.0 * std::pow (2.0, (band - 17) / 3.0);
    }

    TrackAnalyzer::TrackAnalyzer()
        : window (static_cast<std::size_t> (kSize)), buffer (static_cast<std::size_t> (kSize))
    {
        for (int i = 0; i < kSize; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / kSize);   // Hann
            window[static_cast<std::size_t> (i)] = static_cast<float> (w);
            windowPower += w * w;
        }
    }

    void TrackAnalyzer::prepareBands (double sampleRate)
    {
        preparedRate = sampleRate;
        binBand.assign (static_cast<std::size_t> (kSize / 2), -1);
        binKWeight.assign (static_cast<std::size_t> (kSize / 2), 0.0f);
        const double edge = std::pow (2.0, 1.0 / 6.0);
        for (int k = 1; k < kSize / 2; ++k)
        {
            const double hz = k * sampleRate / kSize;
            binKWeight[static_cast<std::size_t> (k)] = static_cast<float> (kWeightPower (hz));
            for (int b = 0; b < kBands; ++b)
            {
                if (hz >= bandHz (b) / edge && hz < bandHz (b) * edge)
                {
                    binBand[static_cast<std::size_t> (k)] = b;
                    break;
                }
            }
        }
    }

    AnalysisFrame TrackAnalyzer::analyse (const float* left, const float* right, double sampleRate)
    {
        if (sampleRate != preparedRate) prepareBands (sampleRate);

        AnalysisFrame f;
        double ll = 0.0, rr = 0.0, lr = 0.0, peak = 0.0;
        for (int i = 0; i < kSize; ++i)
        {
            const double l = left[i], r = right[i];
            ll += l * l;
            rr += r * r;
            lr += l * r;
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
            // Mid in the real part, side in the imaginary part: one complex FFT gives both spectra.
            const float w = window[static_cast<std::size_t> (i)];
            buffer[static_cast<std::size_t> (i)] = { static_cast<float> (0.5 * (l + r)) * w, static_cast<float> (0.5 * (l - r)) * w };
        }

        const double meanSquare = 0.5 * (ll + rr) / kSize;
        f.rmsDb = energyDb (meanSquare);
        f.peakDb = peak > 1.0e-7 ? static_cast<float> (20.0 * std::log10 (peak)) : kSilentDb;
        f.silent = meanSquare < 1.0e-10;   // below about -100 dBFS
        f.correlation = ll * rr > 1.0e-20 ? static_cast<float> (lr / std::sqrt (ll * rr)) : 1.0f;
        if (f.silent) return f;

        fft.forward (buffer);

        // One-sided power, normalised so the bands of a signal add up to its mean square (Parseval).
        const double norm = 2.0 / (static_cast<double> (kSize) * windowPower);
        std::array<double, kBands> mid {}, side {};
        double weighted = 0.0, totalMid = 0.0, totalSide = 0.0;
        for (int k = 1; k < kSize / 2; ++k)
        {
            const auto z = buffer[static_cast<std::size_t> (k)];
            const auto zc = std::conj (buffer[static_cast<std::size_t> (kSize - k)]);
            const auto m = 0.5f * (z + zc);                                  // FFT of mid
            const auto s = std::complex<float> (0.0f, -0.5f) * (z - zc);     // FFT of side
            const double pm = std::norm (m) * norm, ps = std::norm (s) * norm;
            totalMid += pm;
            totalSide += ps;
            weighted += (pm + ps) * binKWeight[static_cast<std::size_t> (k)];
            const int b = binBand[static_cast<std::size_t> (k)];
            if (b < 0) continue;
            mid[static_cast<std::size_t> (b)] += pm;
            side[static_cast<std::size_t> (b)] += ps;
        }

        for (int b = 0; b < kBands; ++b)
        {
            const auto i = static_cast<std::size_t> (b);
            const double e = mid[i] + side[i];
            f.bandDb[i] = energyDb (e, kSineRef);
            f.sideShare[i] = e > 1.0e-14 ? static_cast<float> (side[i] / e) : 0.0f;
        }
        f.width = totalMid + totalSide > 1.0e-14 ? static_cast<float> (totalSide / (totalMid + totalSide)) : 0.0f;
        f.loudness = weighted > 1.0e-14 ? static_cast<float> (-0.691 + 10.0 * std::log10 (2.0 * weighted)) : kSilentDb;
        return f;
    }
}
