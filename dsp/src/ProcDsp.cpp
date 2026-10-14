#include "mixology/ProcDsp.h"

#include <algorithm>
#include <complex>

namespace mixology::dsp
{
    // ---------------------------------------------------------------------------------------------
    Coeffs design (Shape shape, double hz, double db, double q, double sampleRate)
    {
        const double sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double w0 = 2.0 * kPi * std::clamp (hz, 5.0, 0.49 * sr) / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * std::max (q, 0.05));
        const double A = std::pow (10.0, db / 40.0);
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;
        switch (shape)
        {
            case Shape::bell:
                b0 = 1.0 + alpha * A; b1 = -2.0 * cw; b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A; a1 = -2.0 * cw; a2 = 1.0 - alpha / A;
                break;
            case Shape::lowShelf:
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1.0) - (A - 1.0) * cw + s); b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw); b2 = A * ((A + 1.0) - (A - 1.0) * cw - s);
                a0 = (A + 1.0) + (A - 1.0) * cw + s; a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw); a2 = (A + 1.0) + (A - 1.0) * cw - s;
                break;
            }
            case Shape::highShelf:
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1.0) + (A - 1.0) * cw + s); b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw); b2 = A * ((A + 1.0) + (A - 1.0) * cw - s);
                a0 = (A + 1.0) - (A - 1.0) * cw + s; a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw); a2 = (A + 1.0) - (A - 1.0) * cw - s;
                break;
            }
            case Shape::lowPass:
                b0 = (1.0 - cw) / 2.0; b1 = 1.0 - cw; b2 = (1.0 - cw) / 2.0;
                a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
                break;
            case Shape::highPass:
                b0 = (1.0 + cw) / 2.0; b1 = -(1.0 + cw); b2 = (1.0 + cw) / 2.0;
                a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
                break;
            case Shape::allPass:
                b0 = 1.0 - alpha; b1 = -2.0 * cw; b2 = 1.0 + alpha;
                a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
                break;
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    double magnitudeDb (const Coeffs& k, double hz, double sampleRate)
    {
        const double w = 2.0 * kPi * hz / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto h = (k.b0 + k.b1 * z1 + k.b2 * z2) / (1.0 + k.a1 * z1 + k.a2 * z2);
        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    // ---------------------------------------------------------------------------------------------
    void Compressor::set (double thrDb, double r, double attMs, double relMs, double kneeDb, double makeupDb)
    {
        thr = thrDb;
        ratio = std::max (1.0, r);
        knee = std::max (0.0, kneeDb);
        makeup = makeupDb;
        gr.set (attMs, relMs, rate);
    }

    double Compressor::staticReductionDb (double inDb, double thrDb, double r, double kneeDb)
    {
        const double over = inDb - thrDb, slope = 1.0 - 1.0 / std::max (1.0, r);
        if (kneeDb > 0.0 && std::abs (over) <= kneeDb / 2.0)
            return slope * (over + kneeDb / 2.0) * (over + kneeDb / 2.0) / (2.0 * kneeDb);
        return over > 0.0 ? slope * over : 0.0;
    }

    double Compressor::gain (double level)
    {
        const double target = staticReductionDb (gainToDb (level), thr, ratio, knee);
        return dbToGain (makeup - gr.step (target));
    }

    // ---------------------------------------------------------------------------------------------
    void AutoComp::prepare (double sampleRate)
    {
        rate = sampleRate;
        rmsC = timeCoeff (10.0, rate);
        avgC = timeCoeff (2000.0, rate);
        slowC = timeCoeff (1500.0, rate);
        reset();
    }

    void AutoComp::reset()
    {
        ms = avg = slowGr = 0.0;
        primed = false;
        gr.v = 0.0;
    }

    void AutoComp::configure (double attMs, double relMs, double ratioSpan, double depthDb)
    {
        gr.set (attMs, relMs, rate);
        span = ratioSpan;
        depth = depthDb;
    }

    double AutoComp::gain (double level, double amount)
    {
        const double p = level * level;
        ms = p + (ms - p) * rmsC;
        if (level > 1.0e-4)   // only sound above -80 dBFS teaches it the average level
        {
            if (! primed) { avg = std::max (ms, p); primed = true; }
            avg = ms + (avg - ms) * avgC;
        }
        double target = 0.0;
        if (amount > 0.0 && primed)
        {
            const double levelDb = 10.0 * std::log10 (ms + 1.0e-20);
            const double thr = 10.0 * std::log10 (avg + 1.0e-20) + 4.0 - depth * amount;
            target = Compressor::staticReductionDb (levelDb, thr, 1.0 + span * amount, 6.0);
        }
        const double g = gr.step (target);
        slowGr = g + (slowGr - g) * slowC;
        return dbToGain (0.6 * slowGr - g);
    }

    // ---------------------------------------------------------------------------------------------
    void TransientShaper::prepare (double sampleRate)
    {
        rate = sampleRate;
        speed = -1.0;
        set (attack, sustain, 10.0);
        smoothC = timeCoeff (0.3, rate);
        reset();
    }

    void TransientShaper::reset()
    {
        aFast.v = aSlow.v = sFast.v = sSlow.v = 0.0;
        smooth = 0.0;
    }

    void TransientShaper::set (double a, double s, double speedMs)
    {
        attack = std::clamp (a, -1.0, 1.0);
        sustain = std::clamp (s, -1.0, 1.0);
        if (std::abs (speedMs - speed) < 1.0e-6) return;
        speed = speedMs;
        const double fast = timeCoeff (0.2, rate);
        aFast.att = fast;                         aFast.rel = timeCoeff (3.0 * speed, rate);
        aSlow.att = timeCoeff (speed, rate);      aSlow.rel = aFast.rel;
        sFast.att = fast;                         sFast.rel = timeCoeff (speed, rate);
        sSlow.att = fast;                         sSlow.rel = timeCoeff (8.0 * speed, rate);
    }

    double TransientShaper::gain (double level)
    {
        constexpr double floor = 1.0e-6;
        const double af = std::max (aFast.step (level), floor), as = std::max (aSlow.step (level), floor);
        const double sf = std::max (sFast.step (level), floor), ss = std::max (sSlow.step (level), floor);
        const double onset = std::max (0.0, 20.0 * std::log10 (af / as));
        const double tail = std::max (0.0, 20.0 * std::log10 (ss / sf));
        const double target = std::clamp (attack * std::min (onset, 20.0) * 0.75 + sustain * std::min (tail, 20.0) * 0.6, -15.0, 15.0);
        smooth = target + (smooth - target) * smoothC;
        return dbToGain (smooth);
    }

    // ---------------------------------------------------------------------------------------------
    void Gate::prepare (double sampleRate)
    {
        rate = sampleRate;
        keyRel = timeCoeff (10.0, rate);
        reset();
    }

    void Gate::reset()
    {
        key = 0.0;
        atten = 0.0;
        open = true;
        hold = 0;
    }

    void Gate::set (double thrDb, double rangeDb, double attMs, double holdMs, double relMs, bool exp)
    {
        thr = thrDb;
        range = std::max (0.0, rangeDb);
        attC = timeCoeff (attMs, rate);
        relC = timeCoeff (relMs, rate);
        holdSamples = static_cast<int> (std::max (0.0, holdMs) * 0.001 * rate);
        expander = exp;
    }

    double Gate::gain (double level)
    {
        key = level > key ? level : level + (key - level) * keyRel;
        const double k = gainToDb (key);
        double target = 0.0;
        if (! expander)
        {
            if (k > thr) { open = true; hold = holdSamples; }
            else if (k < thr - 3.0) { if (hold > 0) --hold; else open = false; }
            target = open ? 0.0 : range;
        }
        else
        {
            if (k >= thr) hold = holdSamples;
            else if (hold > 0) --hold;
            target = k < thr && hold == 0 ? std::min (range, thr - k) : 0.0;   // 2:1 below the threshold
        }
        atten = target + (atten - target) * (target < atten ? attC : relC);
        return dbToGain (-atten);
    }

    // ---------------------------------------------------------------------------------------------
    namespace
    {
        double ipow (double x, int n)
        {
            double r = 1.0;
            for (int i = 0; i < n; ++i) r *= x;
            return r;
        }

        double accNum (double q, int order, int c)
        {
            double acc = 0.0, term = 0.0;
            int i = 0, j = 1;
            do
            {
                term = ipow (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPi / order) * j;
                acc += term;
                j = -j;
                ++i;
            } while (std::abs (term) > 1.0e-100 && i < 100);
            return acc;
        }

        double accDen (double q, int order, int c)
        {
            double acc = 0.0, term = 0.0;
            int i = 1, j = -1;
            do
            {
                term = ipow (q, i * i) * std::cos (i * 2 * c * kPi / order) * j;
                acc += term;
                j = -j;
                ++i;
            } while (std::abs (term) > 1.0e-100 && i < 100);
            return acc;
        }
    }

    void HalfbandIir::design (int numCoefs, double transition)
    {
        n = std::clamp (numCoefs, 1, kMaxCoefs);
        double k = std::tan ((1.0 - transition * 2.0) * kPi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e4 = e * e * e * e;
        const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
        const int order = n * 2 + 1;
        for (int index = 0; index < n; ++index)
        {
            const int cc = index + 1;
            const double num = accNum (q, order, cc) * std::pow (q, 0.25);
            const double den = accDen (q, order, cc) + 0.5;
            const double ww = num / den, wwsq = ww * ww;
            const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            c[static_cast<std::size_t> (index)] = (1.0 - x) / (1.0 + x);
        }
        reset();
    }

    void HalfbandIir::reset()
    {
        ux = {}; uy = {}; dx = {}; dy = {};
    }

    void HalfbandIir::stages (int count, const std::array<double, kMaxCoefs>& c, double& s0, double& s1,
                              std::array<double, kMaxCoefs>& x, std::array<double, kMaxCoefs>& y)
    {
        for (std::size_t i = 0; i < static_cast<std::size_t> (count); i += 2)
        {
            const double t0 = (s0 - y[i]) * c[i] + x[i];
            x[i] = s0;
            y[i] = t0;
            s0 = t0;
            if (i + 1 < static_cast<std::size_t> (count))
            {
                const double t1 = (s1 - y[i + 1]) * c[i + 1] + x[i + 1];
                x[i + 1] = s1;
                y[i + 1] = t1;
                s1 = t1;
            }
        }
    }

    void HalfbandIir::up (double in, double& out0, double& out1)
    {
        double s0 = in, s1 = in;
        stages (n, c, s0, s1, ux, uy);
        out0 = s0;
        out1 = s1;
    }

    double HalfbandIir::down (double in0, double in1)
    {
        double s0 = in1, s1 = in0;
        stages (n, c, s0, s1, dx, dy);
        return 0.5 * (s0 + s1);
    }

    // ---------------------------------------------------------------------------------------------
    Oversampler::Oversampler()
    {
        // Stage 0 (base <-> 2x) keeps the audio band up to ~0.45 x the base rate and stops what would fold
        // back into it; the later stages only have to protect the band below the base rate's Nyquist.
        for (auto& ch : hb)
        {
            ch[0].design (12, 0.025);
            ch[1].design (6, 0.12);
            ch[2].design (4, 0.18);
        }
    }

    void Oversampler::reset()
    {
        for (auto& ch : hb)
            for (auto& f : ch) f.reset();
    }

    void Oversampler::setStages (int s)
    {
        s = std::clamp (s, 0, kMaxStages);
        if (s == stages) return;
        stages = s;
        reset();
    }

    double* Oversampler::up (int ch, const double* x, int n)
    {
        auto& b = buf[static_cast<std::size_t> (ch)];
        auto& t = tmp[static_cast<std::size_t> (ch)];
        n = std::min (n, kMaxIn);
        if (stages == 0)
        {
            std::copy (x, x + n, b.begin());
            return b.data();
        }
        // Ping-pong so the last stage writes into buf.
        double* src = nullptr;
        double* dst = (stages % 2 == 1) ? b.data() : t.data();
        std::copy (x, x + n, (stages % 2 == 1) ? t.begin() : b.begin());
        src = (stages % 2 == 1) ? t.data() : b.data();
        int m = n;
        for (int s = 0; s < stages; ++s)
        {
            auto& f = hb[static_cast<std::size_t> (ch)][static_cast<std::size_t> (s)];
            for (int i = 0; i < m; ++i)
                f.up (src[i], dst[2 * i], dst[2 * i + 1]);
            m *= 2;
            std::swap (src, dst);
        }
        return src;
    }

    void Oversampler::down (int ch, double* out, int n)
    {
        auto& b = buf[static_cast<std::size_t> (ch)];
        auto& t = tmp[static_cast<std::size_t> (ch)];
        n = std::min (n, kMaxIn);
        if (stages == 0)
        {
            std::copy (b.begin(), b.begin() + n, out);
            return;
        }
        double* src = b.data();
        double* dst = t.data();
        int m = n << stages;
        for (int s = stages - 1; s >= 0; --s)
        {
            auto& f = hb[static_cast<std::size_t> (ch)][static_cast<std::size_t> (s)];
            const int half = m / 2;
            double* target = s == 0 ? out : dst;
            for (int i = 0; i < half; ++i)
                target[i] = f.down (src[2 * i], src[2 * i + 1]);
            m = half;
            std::swap (src, dst);
        }
    }

    // ---------------------------------------------------------------------------------------------
    void LookaheadLimiter::prepare (double sampleRate, int numChannels)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        channels = std::clamp (numChannels, 1, 2);
        maxLook = std::max (1, static_cast<int> (std::ceil (kMaxLookMs * 0.001 * rate)));
        ringSize = maxLook + kDetectLag + 1;
        for (auto& a : audio) a.assign (static_cast<std::size_t> (ringSize), 0.0);
        boxRing.assign (static_cast<std::size_t> (maxLook + 1), 1.0);
        gainRing.assign (static_cast<std::size_t> (maxLook + 1), 1.0);
        dqIdx.assign (static_cast<std::size_t> (maxLook + 2), 0);
        dqVal.assign (static_cast<std::size_t> (maxLook + 2), 1.0);
        look = std::clamp (look, 1, maxLook);
        pos = 0;
        reset();
    }

    void LookaheadLimiter::reset()
    {
        for (auto& a : audio) std::fill (a.begin(), a.end(), 0.0);
        needFresh = true;
    }

    void LookaheadLimiter::set (double inputDb, double ceilingDb, double releaseMs, double lookMs, double character, bool tpMode)
    {
        inGain = dbToGain (inputDb);
        ceiling = dbToGain (std::min (0.0, ceilingDb));
        relC = timeCoeff (std::max (1.0, releaseMs * (1.5 - std::clamp (character, 0.0, 1.0))), rate);
        const int newLook = std::clamp (static_cast<int> (std::lround (lookMs * 0.001 * rate)), 1, maxLook);
        if (tpMode != truePeak) { truePeak = tpMode; for (auto& d : tp) d.reset(); }
        if (newLook != look && ! boxRing.empty())
        {
            look = newLook;
            const int size = static_cast<int> (boxRing.size());
            boxSum = 0.0;
            for (int k = 1; k <= look + 1; ++k)
                boxSum += boxRing[static_cast<std::size_t> (((boxPos - k) % size + size) % size)];
        }
    }

    void LookaheadLimiter::feed (const double* const* x, int nch, int n)
    {
        if (ringSize == 0) return;
        nch = std::min (nch, channels);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nch; ++c) audio[static_cast<std::size_t> (c)][static_cast<std::size_t> (pos)] = x[c][i];
            if (nch == 1 && channels == 2) audio[1][static_cast<std::size_t> (pos)] = x[0][i];
            pos = (pos + 1) % ringSize;
        }
        needFresh = true;
    }

    double LookaheadLimiter::detect (int nch, const double* in)
    {
        double d = 0.0;
        if (truePeak)
        {
            for (int c = 0; c < nch; ++c) d = std::max (d, tp[static_cast<std::size_t> (c)].process (in[c] * inGain));
        }
        else
        {
            const auto lagged = static_cast<std::size_t> (((pos - kDetectLag) % ringSize + ringSize) % ringSize);
            for (int c = 0; c < nch; ++c) d = std::max (d, std::abs (audio[static_cast<std::size_t> (c)][lagged]) * inGain);
        }
        return d;
    }

    void LookaheadLimiter::process (double* const* x, int nch, int n, double w0, double w1)
    {
        if (ringSize == 0) return;
        nch = std::min (nch, channels);
        if (needFresh)
        {
            needFresh = false;
            for (auto& d : tp) d.reset();
            std::fill (boxRing.begin(), boxRing.end(), 1.0);
            std::fill (gainRing.begin(), gainRing.end(), 1.0);
            boxSum = look + 1.0;
            dqHead = dqTail = 0;
            g2 = 1.0;
        }
        const int D = maxLook + kDetectLag;
        const int boxSize = static_cast<int> (boxRing.size());
        const int dqSize = static_cast<int> (dqIdx.size());
        lastGr = 0.0;
        double in[2] {};
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nch; ++c)
            {
                in[c] = x[c][i];
                audio[static_cast<std::size_t> (c)][static_cast<std::size_t> (pos)] = in[c];
            }
            const double d = detect (nch, in);
            const double req = d > ceiling ? ceiling / d : 1.0;

            // Sliding minimum of the required gain over the last look + 1 frames.
            ++counter;
            while (dqTail != dqHead && dqVal[static_cast<std::size_t> ((dqTail - 1 + dqSize) % dqSize)] >= req)
                dqTail = (dqTail - 1 + dqSize) % dqSize;
            dqIdx[static_cast<std::size_t> (dqTail)] = counter;
            dqVal[static_cast<std::size_t> (dqTail)] = req;
            dqTail = (dqTail + 1) % dqSize;
            while (dqHead != dqTail && dqIdx[static_cast<std::size_t> (dqHead)] < counter - look)
                dqHead = (dqHead + 1) % dqSize;
            const double g1 = dqVal[static_cast<std::size_t> (dqHead)];

            g2 = g1 < g2 ? g1 : g1 + (g2 - g1) * relC;

            // Moving average over look + 1 frames: the gain glides down over the lookahead.
            const auto leaving = static_cast<std::size_t> (((boxPos - (look + 1)) % boxSize + boxSize) % boxSize);
            boxSum += g2 - boxRing[leaving];
            boxRing[static_cast<std::size_t> (boxPos)] = g2;
            boxPos = (boxPos + 1) % boxSize;
            const double gA = std::min (1.0, boxSum / (look + 1.0));

            // That gain belongs to the frame look frames back; the audio comes out maxLook frames back.
            gainRing[static_cast<std::size_t> (gainPos)] = gA;
            const double g = gainRing[static_cast<std::size_t> (((gainPos - (maxLook - look)) % boxSize + boxSize) % boxSize)];
            gainPos = (gainPos + 1) % boxSize;

            const double w = w0 + (w1 - w0) * (i + 1) / n;
            const auto rd = static_cast<std::size_t> (((pos - D) % ringSize + ringSize) % ringSize);
            for (int c = 0; c < nch; ++c)
            {
                const double delayed = audio[static_cast<std::size_t> (c)][rd];
                double y = delayed * (1.0 + w * (inGain * g - 1.0));
                if (w >= 0.999) y = std::clamp (y, -ceiling, ceiling);   // safety: never past the ceiling
                x[c][i] = y;
            }
            lastGr = std::max (lastGr, -gainToDb (g) * w);
            pos = (pos + 1) % ringSize;
        }
        if ((counter & 0xffff) < static_cast<long long> (n))   // now and then: rebuild the running sum
        {
            boxSum = 0.0;
            for (int k = 1; k <= look + 1; ++k)
                boxSum += boxRing[static_cast<std::size_t> (((boxPos - k) % boxSize + boxSize) % boxSize)];
        }
    }
}
