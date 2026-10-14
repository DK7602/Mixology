#include "mixology/SpaceFx.h"

#include <algorithm>

namespace mixology::dsp
{
    namespace
    {
        constexpr double kTwoPi = 6.283185307179586;

        std::size_t nextPow2 (std::size_t n)
        {
            std::size_t p = 1;
            while (p < n) p <<= 1;
            return p;
        }

        /** Coefficient of a one-pole low pass at hz (y += (x - y) * a). */
        double onePole (double hz, double sampleRate)
        {
            return 1.0 - std::exp (-kTwoPi * std::clamp (hz, 1.0, 0.49 * sampleRate) / sampleRate);
        }

        /** In-place 8-point Hadamard transform, scaled to keep energy (orthogonal). */
        void hadamard8 (std::array<double, 8>& v)
        {
            for (int h = 1; h < 8; h *= 2)
                for (int i = 0; i < 8; i += 2 * h)
                    for (int j = i; j < i + h; ++j)
                    {
                        const double a = v[static_cast<std::size_t> (j)], b = v[static_cast<std::size_t> (j + h)];
                        v[static_cast<std::size_t> (j)] = a + b;
                        v[static_cast<std::size_t> (j + h)] = a - b;
                    }
            for (auto& x : v) x *= 0.35355339059327373;   // 1 / sqrt (8)
        }

        constexpr std::array<double, 8> kLineMs { 31.3, 37.9, 41.7, 47.3, 53.1, 61.7, 67.9, 73.9 };
        constexpr std::array<double, 4> kDiffMs { 4.77, 3.59, 12.73, 9.31 };
        constexpr std::array<double, 8> kOutL { 1.0, 1.0, -1.0, -1.0, 1.0, -1.0, 1.0, -1.0 };
        constexpr std::array<double, 8> kOutR { 1.0, -1.0, 1.0, -1.0, -1.0, 1.0, 1.0, -1.0 };
        constexpr std::array<double, 8> kEarlyLMs { 7.1, 11.3, 17.9, 23.4, 29.8, 37.2, 43.6, 52.1 };
        constexpr std::array<double, 8> kEarlyRMs { 8.4, 13.0, 16.2, 25.5, 31.1, 35.9, 46.7, 55.3 };
    }

    // ---------------------------------------------------------------------------------------------
    void DelayLine::prepare (int maxDelaySamples)
    {
        const auto size = nextPow2 (static_cast<std::size_t> (std::max (4, maxDelaySamples + 4)));
        buf.assign (size, 0.0);
        mask = size - 1;
        pos = 0;
        maxDelay = static_cast<double> (maxDelaySamples);
    }

    void DelayLine::reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0);
        pos = 0;
    }

    // ---------------------------------------------------------------------------------------------
    void FdnReverb::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        const auto ms = [this] (double m) { return static_cast<int> (std::ceil (m * 0.001 * rate)); };
        pre.prepare (ms (260.0));
        for (std::size_t i = 0; i < diffusers.size(); ++i) diffusers[i].prepare (ms (kDiffMs[i] * 2.3 + 2.0));
        early.prepare (ms (60.0 * 2.3 + 2.0));
        for (std::size_t i = 0; i < lines.size(); ++i) lines[i].prepare (ms (kLineMs[i] * 2.3 + 6.0));
        for (std::size_t i = 0; i < lfoPhase.size(); ++i) lfoPhase[i] = 0.7853981633974483 * static_cast<double> (i);
        set (s);
        reset();
    }

    void FdnReverb::reset()
    {
        pre.reset();
        early.reset();
        for (auto& d : diffusers) d.reset();
        for (auto& l : lines) l.reset();
        damp = {};
    }

    void FdnReverb::set (const Settings& next)
    {
        s = next;
        const double size = std::clamp (s.size, 0.2, 2.3);
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            length[i] = kLineMs[i] * size * 0.001 * rate;
            const double rt = s.freeze ? 1.0e6 : std::max (0.05, s.decaySeconds);
            gain[i] = std::pow (10.0, -3.0 * (length[i] / rate) / rt);   // -60 dB after RT60
            lfoInc[i] = kTwoPi * s.modHz * (1.0 + 0.13 * static_cast<double> (i)) / rate;
        }
        for (std::size_t i = 0; i < diffDelay.size(); ++i) diffDelay[i] = kDiffMs[i] * std::clamp (0.6 + 0.5 * size, 0.6, 2.2) * 0.001 * rate;
        dampA = s.freeze ? 1.0 : onePole (s.dampHz, rate);
        erScale = std::clamp (size, 0.25, 2.2);
    }

    void FdnReverb::process (const double* inL, const double* inR, double* outL, double* outR, int n, const double* extra)
    {
        const double preSamples = std::clamp (s.preDelayMs, 0.0, 250.0) * 0.001 * rate;
        const double modSamples = std::max (0.0, s.modMs) * 0.001 * rate;
        const double inGain = s.freeze ? 0.0 : 1.0;
        const double width = std::clamp (s.width, 0.0, 1.0);
        for (int k = 0; k < n; ++k)
        {
            const double mono = 0.5 * (inL[k] + (inR != nullptr ? inR[k] : inL[k])) * inGain;
            pre.push (mono);
            double x = pre.read (preSamples);

            // Early reflections: a handful of taps, different left and right.
            early.push (x);
            double eL = 0.0, eR = 0.0;
            if (s.early > 0.0)
            {
                double g = 0.85;
                for (std::size_t t = 0; t < kEarlyLMs.size(); ++t)
                {
                    const double sign = (t % 2) ? -1.0 : 1.0;
                    eL += sign * g * early.read (kEarlyLMs[t] * erScale * 0.001 * rate);
                    eR += sign * g * early.read (kEarlyRMs[t] * erScale * 0.001 * rate);
                    g *= 0.78;
                }
                eL *= 0.35 * s.early;
                eR *= 0.35 * s.early;
            }

            // Diffusion (four all-passes) smears the input into a dense wash before the tail.
            if (extra != nullptr) x += extra[k];
            for (std::size_t d = 0; d < diffusers.size(); ++d)
            {
                constexpr double g = 0.62;
                const double delayed = diffusers[d].read (diffDelay[d]);
                const double w = x + g * delayed;
                diffusers[d].push (w);
                x = delayed - g * w;
            }

            std::array<double, 8> y {};
            for (std::size_t i = 0; i < lines.size(); ++i)
            {
                lfoPhase[i] += lfoInc[i];
                if (lfoPhase[i] > kTwoPi) lfoPhase[i] -= kTwoPi;
                const double raw = lines[i].read (length[i] + modSamples * (1.0 + std::sin (lfoPhase[i])));
                damp[i] += (raw - damp[i]) * dampA;
                y[i] = damp[i];
            }
            double tL = 0.0, tR = 0.0;
            for (std::size_t i = 0; i < y.size(); ++i)
            {
                tL += kOutL[i] * y[i];
                tR += kOutR[i] * y[i];
            }
            auto mixed = y;
            hadamard8 (mixed);
            for (std::size_t i = 0; i < lines.size(); ++i)
                lines[i].push (gain[i] * mixed[i] + ((i % 2) ? -x : x) * 0.5);

            tL *= 0.3 * s.late;
            tR *= 0.3 * s.late;
            const double m = 0.5 * (tL + tR), sd = 0.5 * (tL - tR) * width;
            outL[k] = m + sd + eL;
            outR[k] = m - sd + eR;
        }
    }

    // ---------------------------------------------------------------------------------------------
    void StereoDelay::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        dl.prepare (static_cast<int> (4.1 * rate));
        dr.prepare (static_cast<int> (4.1 * rate));
        timeC = std::exp (-1.0 / (0.05 * rate));   // the time glides (no clicks when the tempo changes)
        reset();
    }

    void StereoDelay::reset()
    {
        dl.reset();
        dr.reset();
        lpL = lpR = hpL = hpR = 0.0;
        fresh = true;
    }

    void StereoDelay::set (double timeSeconds, double feedback, double toneHz, double pingPong)
    {
        target = std::clamp (timeSeconds, 0.001, 4.0);
        if (fresh) { time = target; fresh = false; }
        fb = std::clamp (feedback, 0.0, 0.95);
        pp = std::clamp (pingPong, 0.0, 1.0);
        lpA = onePole (toneHz, rate);
        hpA = onePole (70.0, rate);   // no low-end build-up in the repeats
    }

    void StereoDelay::process (const double* inL, const double* inR, double* outL, double* outR, int n)
    {
        for (int k = 0; k < n; ++k)
        {
            time = target + (time - target) * timeC;
            const double d = time * rate;
            const double wl = dl.read (d), wr = dr.read (d);
            // Tone in the loop: each repeat a little darker; no build-up of lows.
            lpL += (wl - lpL) * lpA;
            lpR += (wr - lpR) * lpA;
            hpL += (lpL - hpL) * hpA;
            hpR += (lpR - hpR) * hpA;
            const double fl = lpL - hpL, fr = lpR - hpR;
            const double l = inL[k], r = inR != nullptr ? inR[k] : inL[k];
            // Ping-pong: the input goes in on the left, every repeat crosses to the other side.
            dl.push (l + r * pp + fb * ((1.0 - pp) * fl + pp * fr));
            dr.push (r * (1.0 - pp) + fb * ((1.0 - pp) * fr + pp * fl));
            outL[k] = wl;
            outR[k] = wr;
        }
    }

    // ---------------------------------------------------------------------------------------------
    void Chorus::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        dl.prepare (static_cast<int> (0.08 * rate));
        dr.prepare (static_cast<int> (0.08 * rate));
        reset();
    }

    void Chorus::reset()
    {
        dl.reset();
        dr.reset();
        phase = 0.0;
    }

    void Chorus::set (double rateHz, double d, double delayMs, double w)
    {
        lfoHz = std::clamp (rateHz, 0.01, 10.0);
        depth = std::clamp (d, 0.0, 1.0);
        base = std::clamp (delayMs, 1.0, 40.0);
        width = std::clamp (w, 0.0, 1.0);
    }

    void Chorus::process (const double* inL, const double* inR, double* outL, double* outR, int n)
    {
        const double baseS = base * 0.001 * rate, swing = depth * 0.45 * baseS + depth * 0.0015 * rate;
        const double inc = kTwoPi * lfoHz / rate;
        for (int k = 0; k < n; ++k)
        {
            phase += inc;
            if (phase > kTwoPi) phase -= kTwoPi;
            dl.push (inL[k]);
            dr.push (inR != nullptr ? inR[k] : inL[k]);
            double l = 0.0, r = 0.0;
            for (int v = 0; v < 3; ++v)
            {
                const double ph = phase + v * 2.0943951023931953;   // voices 120 degrees apart
                l += dl.read (baseS + swing * (0.5 + 0.5 * std::sin (ph)));
                r += dr.read (baseS + swing * (0.5 + 0.5 * std::sin (ph + 1.5707963267948966 * width)));   // right a quarter turn later
            }
            l /= 3.0;
            r /= 3.0;
            const double m = 0.5 * (l + r), sd = 0.5 * (l - r);
            outL[k] = m + sd * (0.3 + 0.7 * width);
            outR[k] = m - sd * (0.3 + 0.7 * width);
        }
    }

    // ---------------------------------------------------------------------------------------------
    void Phaser::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void Phaser::reset()
    {
        x1 = {};
        y1 = {};
        last = {};
        phase = 0.0;
    }

    void Phaser::set (double rateHz, double d, double feedback, double centreHz)
    {
        lfoHz = std::clamp (rateHz, 0.01, 20.0);
        depth = std::clamp (d, 0.0, 1.0);
        fb = std::clamp (feedback, 0.0, 0.9);
        centre = std::clamp (centreHz, 50.0, 0.4 * rate);
    }

    void Phaser::process (const double* inL, const double* inR, double* outL, double* outR, int n)
    {
        const double inc = kTwoPi * lfoHz / rate;
        for (int k = 0; k < n; ++k)
        {
            phase += inc;
            if (phase > kTwoPi) phase -= kTwoPi;
            for (int c = 0; c < 2; ++c)
            {
                const double lfo = std::sin (phase + c * 1.5707963267948966);   // right a quarter turn later
                const double hz = std::clamp (centre * std::pow (2.0, 2.0 * depth * lfo), 30.0, 0.45 * rate);
                const double t = std::tan (3.141592653589793 * hz / rate);
                const double a = (t - 1.0) / (t + 1.0);
                const double in = c == 0 ? inL[k] : (inR != nullptr ? inR[k] : inL[k]);
                double x = in + fb * last[static_cast<std::size_t> (c)];
                auto& xs = x1[static_cast<std::size_t> (c)];
                auto& ys = y1[static_cast<std::size_t> (c)];
                for (std::size_t st = 0; st < kStages; ++st)
                {
                    const double y = a * x + xs[st] - a * ys[st];
                    xs[st] = x;
                    ys[st] = y;
                    x = y;
                }
                last[static_cast<std::size_t> (c)] = x;
                const double wet = 0.5 * (in + x);   // the notches come from adding the phase-shifted copy
                (c == 0 ? outL : outR)[k] = wet;
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    void PitchShifter::prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        window = 0.05 * rate;
        line.prepare (static_cast<int> (window * 2.0 + 8.0));
        reset();
    }

    void PitchShifter::reset()
    {
        line.reset();
        phase = 0.0;
    }

    double PitchShifter::process (double x)
    {
        line.push (x);
        phase += (1.0 - ratio) / window;   // the read taps slide through the window at the pitch ratio
        phase -= std::floor (phase);
        const double p2 = phase + 0.5 - std::floor (phase + 0.5);
        const double w1 = std::sin (3.141592653589793 * phase), w2 = std::sin (3.141592653589793 * p2);
        return w1 * w1 * line.read (phase * window + 1.0) + w2 * w2 * line.read (p2 * window + 1.0);
    }
}
