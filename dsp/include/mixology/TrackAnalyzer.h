#pragma once

#include "mixology/Fft.h"

#include <array>
#include <complex>
#include <memory>
#include <vector>

/** What Mixology Link hears on its track at one moment (about 85 ms of audio), for Analyze Mix:
    - 31 one-third-octave bands (20 Hz - 20 kHz), and how much of each band is on the sides (stereo),
    - loudness (K-weighted, LUFS-like), RMS and peak (dBFS), correlation, overall width and left / right balance.
    Band levels are dB relative to a full-scale sine: a 0 dBFS sine reads 0 dB in its band, and the bands
    of any signal add up (as energies) to its RMS level + 3 dB.

    Not for the audio thread: Link runs it on a background thread from a snapshot of the latest audio. */
namespace mixology
{
    constexpr int kBands = 31;
    constexpr float kSilentDb = -120.0f;

    /** ISO 1/3-octave band centres: 1000 * 2^(k/3), k = -17 .. 13 (about 20 Hz .. 20 kHz). */
    double bandHz (int band);

    struct AnalysisFrame
    {
        std::array<float, kBands> bandDb {};   // mid + side energy per band
        std::array<float, kBands> sideShare {}; // 0 = all in the middle (mono), 1 = all on the sides
        float loudness = kSilentDb;   // K-weighted energy over the window, LUFS-like
        float rmsDb = kSilentDb;      // average of both channels, dBFS (full-scale sine = -3)
        float peakDb = kSilentDb;     // highest sample, dBFS
        float correlation = 1.0f;     // +1 mono, 0 unrelated, -1 out of phase
        float width = 0.0f;           // side share of the whole signal
        float balanceDb = 0.0f;       // left minus right energy (dB): + leans left, - leans right
        bool silent = true;

        AnalysisFrame() { bandDb.fill (kSilentDb); sideShare.fill (0.0f); }
    };

    class TrackAnalyzer
    {
    public:
        static constexpr int kMaxSize = 1 << 14;   // 16384 samples (85 ms at 192 kHz)

        /** How many samples one moment takes at this rate: about 85 ms, so the bands stay the same width in Hz
            at every sample rate (4096 up to 50 kHz, 8192 up to 100 kHz, 16384 above). */
        static constexpr int sizeFor (double sampleRate)
        {
            return sampleRate <= 50000.0 ? 1 << 12 : sampleRate <= 100000.0 ? 1 << 13 : kMaxSize;
        }

        TrackAnalyzer();

        /** left / right: the newest sizeFor (sampleRate) samples (right may equal left for a mono track). */
        AnalysisFrame analyse (const float* left, const float* right, double sampleRate);

    private:
        void prepare (double sampleRate);

        int size = 0;
        std::unique_ptr<Fft> fft;
        std::vector<float> window;
        double windowPower = 0.0;
        std::vector<std::complex<float>> buffer;

        // Each FFT bin's share of the bands it overlaps (a bin can be wider than the lowest bands, so its energy
        // is split by overlap and no band is ever left empty), and its K-weighting factor. Prepared per sample rate.
        struct BinShare { int bin; int band; float weight; };
        double preparedRate = 0.0;
        std::vector<BinShare> shares;
        std::vector<float> binKWeight;
        std::vector<double> binMid, binSide;   // per bin, reused by every analysis
    };
}
