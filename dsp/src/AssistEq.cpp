#include "mixology/AssistEq.h"

#include <algorithm>
#include <complex>

namespace mixology
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kGlideSeconds = 0.03;
    }

    void AssistEq::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        channelCount = std::clamp (numChannels, 1, kMaxChannels);
        glideCoeff = 1.0 - std::exp (-static_cast<double> (kChunk) / (kGlideSeconds * rate));
        current = target;   // no glide on load
        lowCutHz = target.lowCutOn() ? target.lowCutHz : kParkedHz;
        gliding = false;
        reset();
        updateStages();
    }

    void AssistEq::reset() noexcept
    {
        for (auto& st : stages)
            for (auto& z : st.z) z = { 0.0, 0.0 };
    }

    void AssistEq::glide() noexcept
    {
        if (current == target && ! gliding) return;
        auto toward = [this] (float& v, float to) {
            const double next = v + (to - v) * glideCoeff;
            v = std::abs (to - next) < 0.005 ? to : static_cast<float> (next);
        };
        auto towardHz = [this] (float& v, float to) {   // frequencies glide in octaves
            const double a = std::log2 (std::max (1.0f, v)), b = std::log2 (std::max (1.0f, to));
            const double next = a + (b - a) * glideCoeff;
            v = std::abs (b - next) < 0.002 ? to : static_cast<float> (std::exp2 (next));
        };
        // An off low cut sits at 10 Hz (inaudible): switching it on or off glides from / to there.
        towardHz (lowCutHz, target.lowCutOn() ? target.lowCutHz : kParkedHz);
        current.lowCutHz = target.lowCutOn() || lowCutHz > kParkedHz ? lowCutHz : 0.0f;
        towardHz (current.mudHz, target.mudHz);
        toward (current.mudDb, target.mudDb);
        towardHz (current.pocketHz, target.pocketHz);
        toward (current.pocketDb, target.pocketDb);
        gliding = ! (current == target);
        updateStages();
    }

    void AssistEq::updateStages() noexcept
    {
        auto set = [] (Stage& st, bool on, const Coeffs& k) {
            if (on && ! st.on)
                for (auto& z : st.z) z = { 0.0, 0.0 };
            st.on = on;
            if (on) st.k = k;
        };
        const bool lcOn = target.lowCutOn() || lowCutHz > kParkedHz;
        set (stages[0], lcOn, lcOn ? highPass (lowCutHz, rate) : Coeffs {});
        const bool mudOn = current.mudDb < -0.001f;
        set (stages[1], mudOn, mudOn ? peak (current.mudHz, current.mudDb, kMudQ, rate) : Coeffs {});
        const bool pocketOn = current.pocketDb < -0.001f;
        set (stages[2], pocketOn, pocketOn ? peak (current.pocketHz, current.pocketDb, kPocketQ, rate) : Coeffs {});
    }

    // RBJ cookbook biquads, normalised (a0 = 1).
    AssistEq::Coeffs AssistEq::highPass (double hz, double sampleRate)
    {
        const double w = 2.0 * kPi * std::clamp (hz, 5.0, 0.45 * sampleRate) / sampleRate;
        const double alpha = std::sin (w) / (2.0 * 0.70710678118654752);
        const double cw = std::cos (w), a0 = 1.0 + alpha;
        Coeffs k;
        k.b0 = (1.0 + cw) / 2.0 / a0;
        k.b1 = -(1.0 + cw) / a0;
        k.b2 = k.b0;
        k.a1 = -2.0 * cw / a0;
        k.a2 = (1.0 - alpha) / a0;
        return k;
    }

    AssistEq::Coeffs AssistEq::peak (double hz, double db, double q, double sampleRate)
    {
        const double w = 2.0 * kPi * std::clamp (hz, 10.0, 0.45 * sampleRate) / sampleRate;
        const double a = std::pow (10.0, db / 40.0);
        const double alpha = std::sin (w) / (2.0 * q);
        const double cw = std::cos (w), a0 = 1.0 + alpha / a;
        Coeffs k;
        k.b0 = (1.0 + alpha * a) / a0;
        k.b1 = -2.0 * cw / a0;
        k.b2 = (1.0 - alpha * a) / a0;
        k.a1 = -2.0 * cw / a0;
        k.a2 = (1.0 - alpha / a) / a0;
        return k;
    }

    double AssistEq::magnitudeDb (const Coeffs& k, double hz, double sampleRate)
    {
        const double w = 2.0 * kPi * hz / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto h = (k.b0 + k.b1 * z1 + k.b2 * z2) / (1.0 + k.a1 * z1 + k.a2 * z2);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    double AssistEq::responseDb (const AssistSettings& s, double hz, double sampleRate)
    {
        double db = 0.0;
        if (s.lowCutOn()) db += magnitudeDb (highPass (s.lowCutHz, sampleRate), hz, sampleRate);
        if (s.mudDb < -0.001f) db += magnitudeDb (peak (s.mudHz, s.mudDb, kMudQ, sampleRate), hz, sampleRate);
        if (s.pocketDb < -0.001f) db += magnitudeDb (peak (s.pocketHz, s.pocketDb, kPocketQ, sampleRate), hz, sampleRate);
        return db;
    }
}
