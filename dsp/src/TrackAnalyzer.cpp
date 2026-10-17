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

    TrackAnalyzer::TrackAnalyzer() = default;

    void TrackAnalyzer::prepare (double sampleRate)
    {
        preparedRate = sampleRate;
        const int n = sizeFor (sampleRate);
        if (n != size)
        {
            size = n;
            int order = 0;
            while ((1 << order) < n) ++order;
            fft = std::make_unique<Fft> (order);
            buffer.assign (static_cast<std::size_t> (n), {});
            window.assign (static_cast<std::size_t> (n), 0.0f);
            windowPower = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);   // Hann
                window[static_cast<std::size_t> (i)] = static_cast<float> (w);
                windowPower += w * w;
            }
        }

        shares.clear();
        binKWeight.assign (static_cast<std::size_t> (size / 2), 0.0f);
        const double edge = std::pow (2.0, 1.0 / 6.0);
        const double binHz = sampleRate / size;
        for (int k = 1; k < size / 2; ++k)
        {
            const double hz = k * binHz;
            binKWeight[static_cast<std::size_t> (k)] = static_cast<float> (kWeightPower (hz));
            // The bin covers hz +- half a bin; each band it overlaps gets that part of its energy.
            const double lo = hz - 0.5 * binHz, hi = hz + 0.5 * binHz;
            for (int b = 0; b < kBands; ++b)
            {
                const double overlap = std::min (hi, bandHz (b) * edge) - std::max (lo, bandHz (b) / edge);
                if (overlap > 0.0) shares.push_back ({ k, b, static_cast<float> (overlap / binHz) });
            }
        }
    }

    AnalysisFrame TrackAnalyzer::analyse (const float* left, const float* right, double sampleRate)
    {
        if (! (sampleRate >= 8000.0)) sampleRate = 48000.0;   // not known yet
        if (sampleRate != preparedRate) prepare (sampleRate);

        AnalysisFrame f;
        double ll = 0.0, rr = 0.0, lr = 0.0, peak = 0.0;
        for (int i = 0; i < size; ++i)
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

        const double meanSquare = 0.5 * (ll + rr) / size;
        f.rmsDb = energyDb (meanSquare);
        f.peakDb = peak > 1.0e-7 ? static_cast<float> (20.0 * std::log10 (peak)) : kSilentDb;
        f.silent = meanSquare < 1.0e-10;   // below about -100 dBFS
        f.correlation = ll * rr > 1.0e-20 ? static_cast<float> (lr / std::sqrt (ll * rr)) : 1.0f;
        if (ll + rr > 1.0e-12)
            f.balanceDb = static_cast<float> (std::clamp (10.0 * std::log10 ((ll + 1.0e-12) / (rr + 1.0e-12)), -40.0, 40.0));
        if (f.silent) return f;

        fft->forward (buffer);

        // One-sided power, normalised so the bands of a signal add up to its mean square (Parseval).
        const double norm = 2.0 / (static_cast<double> (size) * windowPower);
        std::vector<double>& pm = binMid;
        std::vector<double>& ps = binSide;
        pm.assign (static_cast<std::size_t> (size / 2), 0.0);
        ps.assign (static_cast<std::size_t> (size / 2), 0.0);
        double weighted = 0.0, totalMid = 0.0, totalSide = 0.0;
        for (int k = 1; k < size / 2; ++k)
        {
            const auto i = static_cast<std::size_t> (k);
            const auto z = buffer[i];
            const auto zc = std::conj (buffer[static_cast<std::size_t> (size - k)]);
            const auto m = 0.5f * (z + zc);                                  // FFT of mid
            const auto s = std::complex<float> (0.0f, -0.5f) * (z - zc);     // FFT of side
            pm[i] = std::norm (m) * norm;
            ps[i] = std::norm (s) * norm;
            totalMid += pm[i];
            totalSide += ps[i];
            weighted += (pm[i] + ps[i]) * binKWeight[i];
        }
        std::array<double, kBands> mid {}, side {};
        for (const auto& sh : shares)
        {
            mid[static_cast<std::size_t> (sh.band)] += pm[static_cast<std::size_t> (sh.bin)] * sh.weight;
            side[static_cast<std::size_t> (sh.band)] += ps[static_cast<std::size_t> (sh.bin)] * sh.weight;
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
