#pragma once

#include "mixology/Fft.h"

#include <array>
#include <complex>
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
        static constexpr int kOrder = 12;
        static constexpr int kSize = 1 << kOrder;   // 4096 samples (85 ms at 48 kHz)

        TrackAnalyzer();

        /** left / right: the newest kSize samples (right may equal left for a mono track). */
        AnalysisFrame analyse (const float* left, const float* right, double sampleRate);

    private:
        void prepareBands (double sampleRate);

        Fft fft { kOrder };
        std::vector<float> window;
        double windowPower = 0.0;
        std::vector<std::complex<float>> buffer;

        // For each FFT bin: the band it falls into and a K-weighting factor (prepared per sample rate).
        double preparedRate = 0.0;
        std::vector<int> binBand;
        std::vector<float> binKWeight;
    };
}
