#include "mixology/ProcChain.h"
#include "mixology/SpaceFx.h"

#include <cmath>

namespace mixology::proc
{
    using namespace dsp;

    namespace
    {
        constexpr double kGlideSeconds = 0.03;   // knob moves
        constexpr double kFadeSeconds = 0.02;    // a module switching on / off
        constexpr Module kOrder[] = { Module::gate, Module::eq, Module::comp, Module::multi, Module::trans, Module::tone,
                                      Module::loud, Module::harm, Module::analog, Module::clip, Module::stereo, Module::final,
                                      Module::space, Module::limit };

        std::size_t ix (int i) { return static_cast<std::size_t> (i); }
        std::size_t ix (Module m) { return static_cast<std::size_t> (m); }

        double effectiveQ (EqMode mode, EqShape shape, double q, double gainDb)
        {
            const bool cut = shape == EqShape::lowCut || shape == EqShape::highCut;
            if (mode == EqMode::smooth && ! cut) return q * 0.6;   // wider, gentler
            if (mode == EqMode::natural && shape == EqShape::bell)  // analog style: small moves broad, big moves narrower
                return q * std::clamp (0.45 + std::abs (gainDb) * 0.09, 0.5, 1.6);
            return q;
        }

        float metric (float a, float b) { return std::max (a, b); }
    }

    // ---------------------------------------------------------------------------------------------
    // Space: every effect's engine, made on demand (they hold seconds of delay lines).

    struct Chain::SpaceBank
    {
        FdnReverb plate, hall, room, special;
        StereoDelay delay;
        Chorus chorus;
        Phaser phaser;
        PitchShifter shifter;
        Filter2 lowCut, highCut, toneLp;   // the wet sound's EQ, and Special FX's Tone
        Filter2 shimmerHp;                  // no lows in the shimmer's feedback
        float lcHz = -1.0f, hcHz = -1.0f, toneHz = -1.0f;
        int type = -1, prevType = -1;
        double fade = 1.0;                  // from the previous effect to this one (a switch never clicks)
        double keyEnv = 0.0, duckDb = 0.0;
        double swell = 1.0, swellFast = 0.0, swellSlow = 0.0;
        double rate = 48000.0;
        std::array<double, Chain::kChunk> shimmer {};   // last chunk's wet sound, pitched and fed back
        std::array<std::array<double, Chain::kChunk>, 2> a {}, b {};

        void prepare (double sr)
        {
            rate = sr;
            for (auto* r : { &plate, &hall, &room, &special }) r->prepare (sr);
            delay.prepare (sr);
            chorus.prepare (sr);
            phaser.prepare (sr);
            shifter.prepare (sr);
            shimmerHp.k = design (Shape::highPass, 120.0, 0.0, 0.7071, sr);
            resetAll();
        }
        /** Instant (the delay lines forget what they hold, nothing is cleared), so the audio thread may call it. */
        void resetType (int t)
        {
            switch (static_cast<SpaceType> (t))
            {
                case SpaceType::reverb: plate.reset(); break;
                case SpaceType::delay: delay.reset(); break;
                case SpaceType::chorus: chorus.reset(); break;
                case SpaceType::phaser: phaser.reset(); break;
                case SpaceType::hall: hall.reset(); break;
                case SpaceType::room: room.reset(); break;
                case SpaceType::special: special.reset(); shifter.reset(); shimmerHp.reset(); shimmer = {}; swell = 1.0; break;
            }
        }
        void resetAll()
        {
            for (int t = 0; t < 7; ++t) resetType (t);
            lowCut.reset(); highCut.reset(); toneLp.reset();
            lcHz = hcHz = toneHz = -1.0f;
            type = prevType = -1;
            fade = 1.0;
            keyEnv = duckDb = 0.0;
        }
    };

    Chain::Chain() = default;
    Chain::~Chain() = default;

    void Chain::ensureSpace()
    {
        const std::lock_guard<std::mutex> lock (spaceLock);   // the timer and a prepare on another thread may both ask
        if (spaceOwner != nullptr) return;
        auto bankNew = std::make_unique<SpaceBank>();
        bankNew->prepare (rate);
        spaceBank.store (bankNew.get(), std::memory_order_release);
        spaceOwner = std::move (bankNew);
    }

    double Chain::tailSecondsFor (const Values& v)
    {
        if (! isOn (v, Module::space)) return 0.0;
        const auto f = [&v] (int id) { return static_cast<double> (v[ix (id)]); };
        switch (static_cast<SpaceType> (std::clamp (static_cast<int> (f (spType)), 0, 6)))
        {
            case SpaceType::reverb: return f (revPre) / 1000.0 + f (revDecay) * 1.3;
            case SpaceType::hall: return f (hallPre) / 1000.0 + f (hallDecay) * 1.3;
            case SpaceType::room: return f (roomPre) / 1000.0 + f (roomDecay) * 1.3;
            case SpaceType::delay:
            {
                const double fb = std::max (0.01, f (dlyFeedback) / 100.0);
                const double repeats = std::log (0.001) / std::log (fb) + 1.0;
                return std::min (30.0, repeats * kDelayBeats[std::clamp (static_cast<int> (f (dlyTime)), 0, 8)] * 1.0);   // ~60 BPM or faster
            }
            case SpaceType::special: return static_cast<SpecialMode> (static_cast<int> (f (spxMode))) == SpecialMode::freeze ? 45.0 : 12.0 + 10.0 * f (spxFeedback) / 100.0;
            case SpaceType::chorus:
            case SpaceType::phaser: return 0.1;
        }
        return 0.0;
    }

    // ---------------------------------------------------------------------------------------------
    void Chain::prepare (double sampleRate, int numChannels)
    {
        const std::lock_guard<std::mutex> lock (spaceLock);   // never while ensureSpace() makes the engine at the old rate
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        channels = std::clamp (numChannels, 1, kMaxChannels);
        compressor.prepare (rate);
        multiComp[0].prepare (rate); multiComp[0].configure (25.0, 180.0, 3.0, 10.0);
        multiComp[1].prepare (rate); multiComp[1].configure (8.0, 110.0, 3.0, 10.0);
        multiComp[2].prepare (rate); multiComp[2].configure (3.0, 70.0, 3.0, 10.0);
        shaper.prepare (rate);
        gater.prepare (rate);
        toneGlue.prepare (rate); toneGlue.configure (30.0, 250.0, 1.0, 8.0);
        punch.prepare (rate);
        density.prepare (rate); density.configure (3.0, 80.0, 5.0, 12.0);
        finalGlue.prepare (rate); finalGlue.configure (30.0, 300.0, 1.0, 8.0);
        limiter.prepare (rate, channels);
        limiterLatency.store (limiter.latency());
        noiseTone.k = design (Shape::lowPass, 6000.0, 0.0, 0.7071, rate);
        harmOver.setStages (1);
        analogOver.setStages (1);
        if (spaceOwner != nullptr) spaceOwner->prepare (rate);
        fresh = true;
        w = {};
        reset();
    }

    void Chain::reset()
    {
        for (int m = 0; m < kModules; ++m) resetModule (static_cast<Module> (m));
        limiter.reset();
        meters = {};
    }

    void Chain::setValues (const Values& v)
    {
        if (! fresh && v == lastInput) return;   // called every block; the values rarely change
        lastInput = v;
        for (int i = 0; i < count; ++i) target[ix (i)] = sanitise (i, v[ix (i)]);
        if (fresh)
        {
            cur = target;
            fresh = false;
        }
    }

    int Chain::latencyFor (const Values& v) const
    {
        if (! isOn (v, Module::limit)) return 0;
        const int l = limiterLatency.load();
        if (l > 0) return l;
        return static_cast<int> (std::ceil (LookaheadLimiter::kMaxLookMs * 0.001 * rate)) + TruePeakDetector::kLatency;
    }

    bool Chain::active() const
    {
        for (int m = 0; m < kModules; ++m)
            if (w[ix (m)] > 0.0 || isOn (target, static_cast<Module> (m))) return true;
        return false;
    }

    Meters Chain::takeMeters()
    {
        Meters m = meters;
        m.running = 0;
        for (int i = 0; i < kModules; ++i)
            if (w[ix (i)] > 0.0) m.running |= 1u << i;
        meters = {};
        return m;
    }

    void Chain::peakInto (float& l, float& r, int nch, int n) const
    {
        for (int c = 0; c < nch; ++c)
        {
            double p = 0.0;
            for (int i = 0; i < n; ++i) p = std::max (p, std::abs (work[ix (c)][ix (i)]));
            auto& dst = c == 0 ? l : r;
            dst = std::max (dst, static_cast<float> (gainToDb (p)));
        }
        if (nch == 1) r = l;
    }

    void Chain::idle (int nch, int n)
    {
        float l = -200.0f, r = -200.0f;   // nothing runs: what goes out is what came in (measured once)
        peakInto (l, r, nch, n);
        meters.inL = std::max (meters.inL, l);
        meters.inR = std::max (meters.inR, r);
        meters.outL = std::max (meters.outL, l);
        meters.outR = std::max (meters.outR, r);
        const double* rows[2] = { work[0].data(), work[1].data() };
        limiter.feed (rows, nch, n);
    }

    void Chain::glide (int n)
    {
        const double c = 1.0 - std::exp (-static_cast<double> (n) / (kGlideSeconds * rate));
        const auto& sp = specs();
        for (int i = 0; i < count; ++i)
        {
            auto& x = cur[ix (i)];
            const float t = target[ix (i)];
            if (x == t) continue;
            const auto& s = sp[ix (i)];
            if (s.kind != Kind::number) { x = t; continue; }
            double next;
            if (s.curve == Curve::log && s.min > 0.0f)
                next = std::exp (std::log (static_cast<double> (x)) + (std::log (static_cast<double> (t)) - std::log (static_cast<double> (x))) * c);
            else
                next = x + (t - x) * c;
            x = std::abs (t - next) <= 1.0e-4 * (s.max - s.min) ? t : static_cast<float> (next);
        }
    }

    void Chain::processChunk (int nch, int n)
    {
        glide (n);
        peakInto (meters.inL, meters.inR, nch, n);
        const double step = static_cast<double> (n) / (kFadeSeconds * rate);
        for (auto m : kOrder)
        {
            const bool on = isOn (target, m);
            const double w0 = w[ix (m)];
            const double w1 = on ? std::min (1.0, w0 + step) : std::max (0.0, w0 - step);
            if (w0 <= 0.0 && w1 <= 0.0)
            {
                if (m == Module::limit)
                {
                    const double* rows[2] = { work[0].data(), work[1].data() };
                    limiter.feed (rows, nch, n);
                }
                continue;
            }
            if (w0 <= 0.0) resetModule (m);
            w[ix (m)] = w1;

            if (m == Module::limit)
            {
                limiter.set (v (limIn), v (limCeil), v (limRel), v (limLook), v (limChar) / 100.0, v (limDetect) > 0.5);
                double* rows[2] = { work[0].data(), work[1].data() };
                limiter.process (rows, nch, n, w0, w1);
                if (scrub (nch, n)) limiter.reset();
                meters.limit = metric (meters.limit, static_cast<float> (limiter.reductionDb()));
                continue;
            }

            const bool blend = w0 < 1.0 || w1 < 1.0;
            if (blend) dry = work;
            runModule (m, nch, n);
            if (scrub (nch, n)) resetModule (m);   // it broke (e.g. a runaway tail): it starts again from silence
            if (blend)
                for (int c = 0; c < nch; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const double wi = w0 + (w1 - w0) * (i + 1) / n;
                        auto& x = work[ix (c)][ix (i)];
                        const double d = dry[ix (c)][ix (i)];
                        x = d + wi * (x - d);
                    }
        }
        peakInto (meters.outL, meters.outR, nch, n);
    }

    void Chain::runModule (Module m, int nch, int n)
    {
        switch (m)
        {
            case Module::gate: gate (nch, n); break;
            case Module::eq: eq (nch, n); break;
            case Module::comp: comp (nch, n); break;
            case Module::multi: multi (nch, n); break;
            case Module::trans: trans (nch, n); break;
            case Module::tone: tone (nch, n); break;
            case Module::loud: loud (nch, n); break;
            case Module::harm: harm (nch, n); break;
            case Module::analog: analog (nch, n); break;
            case Module::clip: clip (nch, n); break;
            case Module::stereo: stereo (nch, n); break;
            case Module::final: finalTouch (nch, n); break;
            case Module::space: space (nch, n); break;
            case Module::limit:
            case Module::count: break;
        }
    }

    bool Chain::scrub (int nch, int n)
    {
        bool bad = false;
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i)
            {
                auto& x = work[ix (c)][ix (i)];
                if (! std::isfinite (x)) { x = 0.0; bad = true; }
            }
        return bad;
    }

    void Chain::resetModule (Module m)
    {
        switch (m)
        {
            case Module::gate: gater.reset(); break;
            case Module::eq:
                for (auto& b : bands) { b.a.reset(); b.b.reset(); b.active = false; b.last.fill (-1.0f); }
                break;
            case Module::comp: compressor.reset(); break;
            case Module::multi:
                lp1.reset(); hp1.reset(); lp2.reset(); hp2.reset(); ap2.reset();
                for (auto& c : multiComp) c.reset();
                lastX1 = lastX2 = -1.0f;
                break;
            case Module::trans: shaper.reset(); focusFilter.reset(); lastFocus = -1; break;
            case Module::tone:
                warmth.reset(); presence.reset(); air.reset(); toneGlue.reset(); punch.reset();
                warmthDb = presenceDb = airDb = 99.0f;
                break;
            case Module::loud: lowEnd.reset(); topEnd.reset(); density.reset(); lowEndDb = topEndDb = 99.0f; break;
            case Module::harm:
                preLow.reset(); preHigh.reset(); postLow.reset(); postHigh.reset(); harmOver.reset();
                dcX = {}; dcY = {}; tiltDb = 99.0f;
                break;
            case Module::analog: analogOver.reset(); bump.reset(); hfLoss.reset(); noiseTone.reset(); noiseEnv = 0.0; bumpDb = hfDb = 99.0f; break;
            case Module::clip: clipOver.reset(); break;
            case Module::stereo:
                sideLp1.reset(); sideHp1.reset(); sideLp2.reset(); sideHp2.reset(); sideAp2.reset(); midAp1.reset(); midAp2.reset();
                lastLowX = lastHighX = -1.0f;
                break;
            case Module::final: finalAir.reset(); finalGlue.reset(); finalAirDb = 99.0f; break;
            case Module::space: if (auto* sb = spaceBank.load (std::memory_order_acquire)) sb->resetAll(); break;
            case Module::limit:
            case Module::count: break;
        }
    }

    void Chain::setShelf (Filter2& f, Shape shape, double hz, double db, double q, double sampleRate, float& lastDb)
    {
        if (static_cast<float> (db) == lastDb) return;
        if (std::abs (lastDb) > 98.0f || (std::abs (lastDb) < 1.0e-4f && std::abs (db) >= 1.0e-4)) f.reset();   // waking up
        lastDb = static_cast<float> (db);
        f.k = design (shape, hz, db, q, sampleRate);
    }

    // ---------------------------------------------------------------------------------------------
    // Modules

    void Chain::gate (int nch, int n)
    {
        gater.set (v (gateThr), v (gateRange), v (gateAtt), v (gateHold), v (gateRel), v (gateMode) > 0.5);
        double most = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double level = 0.0;
            for (int c = 0; c < nch; ++c) level = std::max (level, std::abs (work[ix (c)][ix (i)]));
            const double g = gater.gain (level);
            for (int c = 0; c < nch; ++c) work[ix (c)][ix (i)] *= g;
            most = std::max (most, gater.attenuationDb());
        }
        meters.gate = metric (meters.gate, static_cast<float> (most));
    }

    Chain::BandDesign Chain::designBand (const Values& vals, int b, double sampleRate)
    {
        BandDesign d;
        const auto shape = static_cast<EqShape> (std::clamp (static_cast<int> (vals[ix (eqType (b))]), 0, 4));
        const auto mode = static_cast<EqMode> (std::clamp (static_cast<int> (vals[ix (eqMode)]), 0, 2));
        const double hz = vals[ix (eqFreq (b))], gain = vals[ix (eqGain (b))], q = vals[ix (eqQ (b))];
        const bool cut = shape == EqShape::lowCut || shape == EqShape::highCut;
        d.active = cut || std::abs (gain) > 1.0e-4;
        if (! d.active) return d;
        const double qq = effectiveQ (mode, shape, q, gain);
        switch (shape)
        {
            case EqShape::bell: d.k = design (Shape::bell, hz, gain, qq, sampleRate); break;
            case EqShape::lowShelf: d.k = design (Shape::lowShelf, hz, gain, qq, sampleRate); break;
            case EqShape::highShelf: d.k = design (Shape::highShelf, hz, gain, qq, sampleRate); break;
            case EqShape::lowCut: d.k = design (Shape::highPass, hz, 0.0, qq, sampleRate); break;
            case EqShape::highCut: d.k = design (Shape::lowPass, hz, 0.0, qq, sampleRate); break;
        }
        d.stages = cut && mode != EqMode::smooth ? 2 : 1;   // 24 dB / octave, or 12 in Smooth
        return d;
    }

    double Chain::eqResponseDb (const Values& vals, double hz, double sampleRate)
    {
        double db = 0.0;
        for (int b = 0; b < kEqBands; ++b)
        {
            const auto d = designBand (vals, b, sampleRate);
            if (d.active) db += d.stages * magnitudeDb (d.k, hz, sampleRate);
        }
        return db;
    }

    void Chain::eq (int nch, int n)
    {
        for (int b = 0; b < kEqBands; ++b)
        {
            auto& band = bands[ix (b)];
            const std::array<float, 5> now { cur[ix (eqType (b))], cur[ix (eqFreq (b))], cur[ix (eqGain (b))], cur[ix (eqQ (b))], cur[ix (eqMode)] };
            if (now != band.last)
            {
                band.last = now;
                const auto d = designBand (cur, b, rate);
                if (d.active && ! band.active) { band.a.reset(); band.b.reset(); }
                if (d.active && d.stages == 2 && band.stages == 1) band.b.z = band.a.z;   // a second stage joins: start it where the first is
                band.active = d.active;
                band.stages = d.stages;
                if (d.active) band.a.k = band.b.k = d.k;
            }
            if (! band.active) continue;
            for (int c = 0; c < nch; ++c)
                for (int i = 0; i < n; ++i)
                {
                    double x = band.a.process (c, work[ix (c)][ix (i)]);
                    if (band.stages == 2) x = band.b.process (c, x);
                    work[ix (c)][ix (i)] = x;
                }
        }
    }

    void Chain::comp (int nch, int n)
    {
        static constexpr double kKnees[] = { 12.0, 6.0, 0.0 };
        compressor.set (v (compThr), v (compRatio), v (compAtt), v (compRel), kKnees[std::clamp (static_cast<int> (v (compKnee)), 0, 2)], v (compMakeup));
        double most = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double level = 0.0;
            for (int c = 0; c < nch; ++c) level = std::max (level, std::abs (work[ix (c)][ix (i)]));
            const double g = compressor.gain (level);
            for (int c = 0; c < nch; ++c) work[ix (c)][ix (i)] *= g;
            most = std::max (most, compressor.reductionDb());
        }
        meters.comp = metric (meters.comp, static_cast<float> (most));
    }

    void Chain::multi (int nch, int n)
    {
        const bool three = v (multiBands) < 0.5;
        const float x1 = cur[multiX1], x2 = std::max (cur[multiX2], x1 * 1.5f);
        if (x1 != lastX1) { lastX1 = x1; lp1.set (false, x1, rate); hp1.set (true, x1, rate); }
        if (x2 != lastX2)
        {
            lastX2 = x2;
            lp2.set (false, x2, rate);
            hp2.set (true, x2, rate);
            ap2.k = design (Shape::allPass, x2, 0.0, 0.70710678118654752, rate);
        }
        const double amt[3] = { v (multiLow) / 100.0, three ? v (multiMid) / 100.0 : 0.0, v (multiHigh) / 100.0 };
        double most[3] {};
        for (int i = 0; i < n; ++i)
        {
            double lo[2] {}, mid[2] {}, hi[2] {};
            double lvl[3] {};
            for (int c = 0; c < nch; ++c)
            {
                const double x = work[ix (c)][ix (i)];
                lo[c] = lp1.process (c, x);
                const double up = hp1.process (c, x);
                if (three)
                {
                    mid[c] = lp2.process (c, up);
                    hi[c] = hp2.process (c, up);
                    lo[c] = ap2.process (c, lo[c]);   // keeps the low band in phase with the other two
                }
                else
                    hi[c] = up;
                lvl[0] = std::max (lvl[0], std::abs (lo[c]));
                lvl[1] = std::max (lvl[1], std::abs (mid[c]));
                lvl[2] = std::max (lvl[2], std::abs (hi[c]));
            }
            const double gl = multiComp[0].gain (lvl[0], amt[0]);
            const double gm = three ? multiComp[1].gain (lvl[1], amt[1]) : 1.0;
            const double gh = multiComp[2].gain (lvl[2], amt[2]);
            for (int c = 0; c < nch; ++c) work[ix (c)][ix (i)] = lo[c] * gl + mid[c] * gm + hi[c] * gh;
            for (int b = 0; b < 3; ++b) most[b] = std::max (most[b], multiComp[ix (b)].reductionDb());
        }
        meters.multiLow = metric (meters.multiLow, static_cast<float> (most[0]));
        meters.multiMid = metric (meters.multiMid, three ? static_cast<float> (most[1]) : 0.0f);
        meters.multiHigh = metric (meters.multiHigh, static_cast<float> (most[2]));
    }

    void Chain::trans (int nch, int n)
    {
        shaper.set (v (transAttack) / 100.0, v (transSustain) / 100.0, v (transSpeed));
        const int focus = std::clamp (static_cast<int> (v (transFocus)), 0, 2);
        if (focus != lastFocus)
        {
            lastFocus = focus;
            focusFilter.reset();
            if (focus == 1) focusFilter.k = design (Shape::lowPass, 150.0, 0.0, 0.7071, rate);
            if (focus == 2) focusFilter.k = design (Shape::highPass, 3000.0, 0.0, 0.7071, rate);
        }
        const double out = dbToGain (v (transOut)), mix = v (transMix) / 100.0;
        double biggest = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double mono = 0.0;
            for (int c = 0; c < nch; ++c) mono += work[ix (c)][ix (i)];
            mono /= nch;
            const double det = std::abs (focus == 0 ? mono : focusFilter.process (0, mono));
            const double g = shaper.gain (det) * out;
            const double k = 1.0 + mix * (g - 1.0);
            for (int c = 0; c < nch; ++c) work[ix (c)][ix (i)] *= k;
            if (std::abs (shaper.gainDb()) > std::abs (biggest)) biggest = shaper.gainDb();
        }
        if (std::abs (biggest) > std::abs (meters.trans)) meters.trans = static_cast<float> (biggest);
    }

    void Chain::tone (int nch, int n)
    {
        setShelf (warmth, Shape::lowShelf, 220.0, v (tbWarmth), 0.6, rate, warmthDb);
        setShelf (presence, Shape::bell, 3200.0, v (tbPresence), 0.8, rate, presenceDb);
        setShelf (air, Shape::highShelf, 11000.0, v (tbAir), 0.6, rate, airDb);
        const double glueAmt = v (tbGlue) / 100.0;
        punch.set (v (tbPunch) / 100.0, 0.0, 15.0);
        const bool doPunch = std::abs (v (tbPunch)) > 0.01;
        double most = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double level = 0.0;
            for (int c = 0; c < nch; ++c)
            {
                double x = work[ix (c)][ix (i)];
                if (std::abs (warmthDb) > 1.0e-4f) x = warmth.process (c, x);
                if (std::abs (presenceDb) > 1.0e-4f) x = presence.process (c, x);
                if (std::abs (airDb) > 1.0e-4f) x = air.process (c, x);
                work[ix (c)][ix (i)] = x;
                level = std::max (level, std::abs (x));
            }
            double g = 1.0;
            if (glueAmt > 0.0) { g *= toneGlue.gain (level, glueAmt); most = std::max (most, toneGlue.reductionDb()); }
            if (doPunch) g *= punch.gain (level);
            if (g != 1.0)
                for (int c = 0; c < nch; ++c) work[ix (c)][ix (i)] *= g;
        }
        meters.glue = metric (meters.glue, static_cast<float> (most));
    }

    void Chain::loud (int nch, int n)
    {
        setShelf (lowEnd, Shape::lowShelf, 90.0, v (ldLow), 0.7, rate, lowEndDb);
        setShelf (topEnd, Shape::highShelf, 9000.0, v (ldTop), 0.7, rate, topEndDb);
        const double trim = dbToGain (v (ldTrim)), dens = v (ldDensity) / 100.0 * 0.7;
        for (int i = 0; i < n; ++i)
        {
            double level = 0.0;
            for (int c = 0; c < nch; ++c)
            {
                double x = work[ix (c)][ix (i)] * trim;
                if (std::abs (lowEndDb) > 1.0e-4f) x = lowEnd.process (c, x);
                if (std::abs (topEndDb) > 1.0e-4f) x = topEnd.process (c, x);
                work[ix (c)][ix (i)] = x;
                level = std::max (level, std::abs (x));
            }
            if (dens > 0.0)
            {
                const double g = density.gain (level, 1.0);   // a hard-working compressor, blended in
                const double k = 1.0 + dens * (g - 1.0);
                for (int c = 0; c < nch; ++c) work[ix (c)][ix (i)] *= k;
            }
        }
    }

    void Chain::harm (int nch, int n)
    {
        const double drive = 1.0 + 9.0 * v (harmDrive) / 100.0, even = v (harmEvenOdd) / 100.0, mix = v (harmMix) / 100.0;
        const float tilt = cur[harmTone];
        if (tilt != tiltDb)
        {
            tiltDb = tilt;
            // Harmonics tilted brighter / darker: emphasis before the saturation, the exact opposite after it.
            preLow.k = design (Shape::lowShelf, 1000.0, -tilt / 2.0, 0.5, rate);
            preHigh.k = design (Shape::highShelf, 1000.0, tilt / 2.0, 0.5, rate);
            postLow.k = design (Shape::lowShelf, 1000.0, tilt / 2.0, 0.5, rate);
            postHigh.k = design (Shape::highShelf, 1000.0, -tilt / 2.0, 0.5, rate);
        }
        constexpr double bias = 0.25;
        const double tb = std::tanh (drive * bias), evenNorm = drive * (1.0 - tb * tb);
        const double dcR = 1.0 - 2.0 * kPi * 8.0 / rate;
        std::array<double, kChunk> tmp {};
        for (int c = 0; c < nch; ++c)
        {
            auto& row = work[ix (c)];
            for (int i = 0; i < n; ++i) tmp[ix (i)] = preHigh.process (c, preLow.process (c, row[ix (i)]));
            double* hi = harmOver.up (c, tmp.data(), n);
            for (int i = 0; i < n * harmOver.factor(); ++i)
            {
                const double u = hi[i];
                const double odd = std::tanh (drive * u) / drive;
                const double ev = (std::tanh (drive * (u + bias)) - tb) / evenNorm;
                hi[i] = u + mix * ((1.0 - even) * odd + even * ev - u);
            }
            harmOver.down (c, tmp.data(), n);
            for (int i = 0; i < n; ++i)
            {
                const double y = postHigh.process (c, postLow.process (c, tmp[ix (i)]));
                const double out = y - dcX[ix (c)] + dcR * dcY[ix (c)];   // the even harmonics' DC out
                dcX[ix (c)] = y;
                dcY[ix (c)] = out;
                row[ix (i)] = out;
            }
        }
    }

    void Chain::analog (int nch, int n)
    {
        const double tape = v (anTape) / 100.0, console = v (anConsole) / 100.0, noise = v (anNoise) / 100.0, mix = v (anMix) / 100.0;
        setShelf (bump, Shape::bell, 75.0, 1.5 * tape * mix, 1.0, rate, bumpDb);
        setShelf (hfLoss, Shape::highShelf, 14000.0, -2.5 * tape * mix, 0.7, rate, hfDb);
        const double gt = 1.0 + 2.0 * tape, k3 = 0.12 * console;
        std::array<double, kChunk> tmp {};
        for (int c = 0; c < nch; ++c)
        {
            auto& row = work[ix (c)];
            std::copy (row.begin(), row.begin() + n, tmp.begin());
            double* hi = analogOver.up (c, tmp.data(), n);
            for (int i = 0; i < n * analogOver.factor(); ++i)
            {
                const double u = hi[i];
                double x = u;
                if (k3 > 0.0) { const double y = std::clamp (x, -1.6, 1.6); x = y - k3 * y * y * y + (x - y); }
                if (tape > 0.0) x = std::tanh (gt * x) / gt + 0.02 * tape * x * x;
                hi[i] = u + mix * (x - u);
            }
            analogOver.down (c, row.data(), n);
            for (int i = 0; i < n; ++i)
            {
                double x = row[ix (i)];
                if (std::abs (bumpDb) > 1.0e-4f) x = bump.process (c, x);
                if (std::abs (hfDb) > 1.0e-4f) x = hfLoss.process (c, x);
                row[ix (i)] = x;
            }
        }
        if (nch == 2 && console > 0.0)
        {
            const double bleed = 0.01 * console * mix;   // channels leak into each other a little (-40 dB)
            for (int i = 0; i < n; ++i)
            {
                const double l = work[0][ix (i)], r = work[1][ix (i)];
                work[0][ix (i)] = l + bleed * r;
                work[1][ix (i)] = r + bleed * l;
            }
        }
        if (noise > 0.0)
        {
            const double level = dbToGain (-96.0 + 36.0 * noise) * mix;
            const double envAtt = timeCoeff (10.0, rate), envRel = timeCoeff (200.0, rate);
            for (int i = 0; i < n; ++i)
            {
                double peak = 0.0;
                for (int c = 0; c < nch; ++c) peak = std::max (peak, std::abs (work[ix (c)][ix (i)]));
                noiseEnv = peak + (noiseEnv - peak) * (peak > noiseEnv ? envAtt : envRel);
                const double gate = std::min (1.0, noiseEnv / 1.0e-3);   // hiss only while the track plays
                for (int c = 0; c < nch; ++c)
                {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    const double white = static_cast<double> (rng) / 4294967295.0 * 2.0 - 1.0;
                    work[ix (c)][ix (i)] += noiseTone.process (c, white) * level * gate;
                }
            }
        }
    }

    void Chain::clip (int nch, int n)
    {
        clipOver.setStages (std::clamp (static_cast<int> (v (clipOs)), 0, 3));
        const double t = dbToGain (v (clipThr)), soft = v (clipSoft) / 100.0, mix = v (clipMix) / 100.0;
        const double a = 1.0 - 0.9 * soft;   // where the curve starts to bend (share of the threshold)
        double over = 0.0;
        std::array<double, kChunk> tmp {};
        for (int c = 0; c < nch; ++c)
        {
            auto& row = work[ix (c)];
            for (int i = 0; i < n; ++i) over = std::max (over, std::abs (row[ix (i)]) / t);
            std::copy (row.begin(), row.begin() + n, tmp.begin());
            double* hi = clipOver.up (c, tmp.data(), n);
            for (int i = 0; i < n * clipOver.factor(); ++i)
            {
                const double u = hi[i], m = std::abs (u) / t;
                double y = m;
                if (m > a) y = a >= 1.0 ? 1.0 : a + (1.0 - a) * std::tanh ((m - a) / (1.0 - a));
                const double clipped = std::copysign (y * t, u);
                hi[i] = u + mix * (clipped - u);
            }
            clipOver.down (c, row.data(), n);
        }
        if (over > 1.0) meters.clip = metric (meters.clip, static_cast<float> (gainToDb (over)));
    }

    void Chain::stereo (int nch, int n)
    {
        if (nch < 2) return;
        constexpr double kButterworth = 0.70710678118654752;
        const float lx = cur[stLowX], hx = std::max (cur[stHighX], lx * 2.0f);
        if (lx != lastLowX)
        {
            lastLowX = lx;
            sideLp1.set (false, lx, rate);
            sideHp1.set (true, lx, rate);
            midAp1.k = design (Shape::allPass, lx, 0.0, kButterworth, rate);
        }
        if (hx != lastHighX)
        {
            lastHighX = hx;
            sideLp2.set (false, hx, rate);
            sideHp2.set (true, hx, rate);
            sideAp2.k = midAp2.k = design (Shape::allPass, hx, 0.0, kButterworth, rate);
        }
        const double width = v (stWidth) / 100.0, lo = v (stLow) / 100.0, mid = v (stMid) / 100.0, hi = v (stHigh) / 100.0;
        for (int i = 0; i < n; ++i)
        {
            const double l = work[0][ix (i)], r = work[1][ix (i)];
            // Side in three Linkwitz-Riley bands (they add back up to an all-pass); the mid goes through the
            // same all-pass, so at equal widths left and right only see a phase shift, the same on both.
            const double s = 0.5 * (l - r);
            const double sLow = sideAp2.process (0, sideLp1.process (0, s));
            const double sUp = sideHp1.process (0, s);
            const double sMid = sideLp2.process (0, sUp), sHigh = sideHp2.process (0, sUp);
            const double m = midAp2.process (0, midAp1.process (0, 0.5 * (l + r)));
            const double side = width * (lo * sLow + mid * sMid + hi * sHigh);
            work[0][ix (i)] = m + side;
            work[1][ix (i)] = m - side;
        }
    }

    void Chain::finalTouch (int nch, int n)
    {
        setShelf (finalAir, Shape::highShelf, 14000.0, v (finAir), 0.6, rate, finalAirDb);
        const double width = v (finWidth) / 100.0, glueAmt = v (finGlue) / 100.0, out = dbToGain (v (finOut));
        double most = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double level = 0.0;
            if (nch == 2 && width != 1.0)
            {
                const double l = work[0][ix (i)], r = work[1][ix (i)];
                const double m = 0.5 * (l + r), s = 0.5 * (l - r) * width;
                work[0][ix (i)] = m + s;
                work[1][ix (i)] = m - s;
            }
            for (int c = 0; c < nch; ++c)
            {
                double x = work[ix (c)][ix (i)];
                if (std::abs (finalAirDb) > 1.0e-4f) x = finalAir.process (c, x);
                work[ix (c)][ix (i)] = x;
                level = std::max (level, std::abs (x));
            }
            double g = out;
            if (glueAmt > 0.0) { g *= finalGlue.gain (level, glueAmt); most = std::max (most, finalGlue.reductionDb()); }
            if (g != 1.0)
                for (int c = 0; c < nch; ++c) work[ix (c)][ix (i)] *= g;
        }
        meters.glue = metric (meters.glue, static_cast<float> (most));
    }

    void Chain::space (int nch, int n)
    {
        auto* sb = spaceBank.load (std::memory_order_acquire);
        if (sb == nullptr)   // not made yet (the host makes it within a moment)
        {
            if (v (spRoute) >= 0.5)   // an FX channel plays only the effect: silence, never the dry sound twice
                for (int c = 0; c < nch; ++c) std::fill_n (work[static_cast<std::size_t> (c)].begin(), n, 0.0);
            return;                   // on a track the sound passes
        }
        auto& bank = *sb;
        const int type = std::clamp (static_cast<int> (v (spType)), 0, 6);
        if (type != bank.type)
        {
            bank.resetType (type);
            bank.prevType = bank.type;
            bank.fade = bank.type < 0 ? 1.0 : 0.0;
            bank.type = type;
        }

        const double* inL = work[0].data();
        const double* inR = nch > 1 ? work[1].data() : work[0].data();
        static constexpr int kMixIds[] = { revMix, dlyMix, choMix, phaMix, hallMix, roomMix, spxMix };

        // One effect's wet sound into out (L, R).
        auto run = [&] (int t, std::array<std::array<double, kChunk>, 2>& out) {
            double* oL = out[0].data();
            double* oR = out[1].data();
            FdnReverb::Settings rs;
            switch (static_cast<SpaceType> (t))
            {
                case SpaceType::reverb:
                    rs.preDelayMs = v (revPre); rs.decaySeconds = v (revDecay); rs.dampHz = v (revDamp); rs.size = 0.85;
                    rs.width = v (revWidth) / 100.0; rs.modMs = 0.2; rs.modHz = 0.6;
                    bank.plate.set (rs);
                    bank.plate.process (inL, inR, oL, oR, n);
                    break;
                case SpaceType::hall:
                    rs.preDelayMs = v (hallPre); rs.decaySeconds = v (hallDecay); rs.dampHz = v (hallDamp);
                    rs.size = 1.0 + 1.2 * v (hallSize) / 100.0; rs.early = 0.25; rs.modMs = 0.35; rs.modHz = 0.4;
                    bank.hall.set (rs);
                    bank.hall.process (inL, inR, oL, oR, n);
                    break;
                case SpaceType::room:
                {
                    const double late = v (roomLate) / 100.0;
                    rs.preDelayMs = v (roomPre); rs.decaySeconds = v (roomDecay); rs.dampHz = 8000.0;
                    rs.size = 0.25 + 0.55 * v (roomSize) / 100.0; rs.late = late; rs.early = 0.2 + 1.6 * (1.0 - late); rs.modMs = 0.1;
                    bank.room.set (rs);
                    bank.room.process (inL, inR, oL, oR, n);
                    break;
                }
                case SpaceType::special:
                {
                    const auto mode = static_cast<SpecialMode> (std::clamp (static_cast<int> (v (spxMode)), 0, 3));
                    const double fb = v (spxFeedback) / 100.0, motion = v (spxMotion) / 100.0;
                    rs.size = 1.9;
                    rs.decaySeconds = mode == SpecialMode::freeze ? 40.0 : mode == SpecialMode::swell ? 3.0 + 5.0 * fb : 2.5 + 7.0 * fb;
                    rs.modMs = 0.3 + (mode == SpecialMode::drift ? 3.0 : 1.0) * motion;
                    rs.modHz = 0.15 + 0.6 * motion;
                    rs.dampHz = std::min (20000.0, v (spxTone) * 1.2);
                    bank.special.set (rs);
                    // Shimmer: last chunk's wet sound, pitched by Shift, goes back into the tail. That loop must lose a
                    // little on every trip, whatever the settings, or the tail grows until it explodes: no lows (they
                    // keep their pitch and the tail rings them up), and with no Shift the sound lands back on the
                    // tail's own resonances, so there the feedback stays below what they ring up.
                    std::array<double, kChunk> extra {};
                    if (mode == SpecialMode::shimmer)
                    {
                        const double shift = v (spxShift), most = fb * 0.55;
                        const double unshifted = std::min (most, 0.9 / bank.special.resonance());
                        const double loop = unshifted + (most - unshifted) * std::min (1.0, std::abs (shift));
                        bank.shifter.setSemitones (shift);
                        for (int i = 0; i < n; ++i)
                        {
                            const double e = bank.shimmerHp.process (0, bank.shifter.process (bank.shimmer[ix (i)])) * loop;
                            const double ae = std::abs (e);
                            extra[ix (i)] = ae > 1.0 ? std::copysign (1.0 + std::tanh (ae - 1.0), e) : e;   // and never past 2
                        }
                    }
                    // Freeze: the tail hardly decays, so only a little of the track goes in.
                    std::array<double, kChunk> fl {}, fr {};
                    const double inGain = mode == SpecialMode::freeze ? 0.25 : 1.0;
                    for (int i = 0; i < n; ++i) { fl[ix (i)] = inL[i] * inGain; fr[ix (i)] = inR[i] * inGain; }
                    bank.special.process (fl.data(), fr.data(), oL, oR, n, extra.data());
                    if (bank.toneHz != cur[spxTone])
                    {
                        bank.toneHz = cur[spxTone];
                        bank.toneLp.k = design (Shape::lowPass, bank.toneHz, 0.0, 0.7071, rate);
                    }
                    const double swellTime = 0.25 + 1.2 * motion;
                    const double fastC = timeCoeff (5.0, rate), slowC = timeCoeff (400.0, rate), riseC = timeCoeff (swellTime * 1000.0 / 3.0, rate);
                    for (int i = 0; i < n; ++i)
                    {
                        double l = bank.toneLp.process (0, oL[i]), r = bank.toneLp.process (1, oR[i]);
                        bank.shimmer[ix (i)] = 0.5 * (l + r);
                        if (mode == SpecialMode::swell)
                        {
                            // Swell: after every new note the effect fades in (like a reversed tail).
                            const double x = std::abs (0.5 * (inL[i] + inR[i]));
                            bank.swellFast = x + (bank.swellFast - x) * fastC;
                            bank.swellSlow = x + (bank.swellSlow - x) * slowC;
                            if (bank.swellFast > 2.0 * bank.swellSlow + 1.0e-4 && bank.swell > 0.6) bank.swell = 0.0;
                            bank.swell = 1.0 + (bank.swell - 1.0) * riseC;
                            l *= bank.swell;
                            r *= bank.swell;
                        }
                        oL[i] = l;
                        oR[i] = r;
                    }
                    break;
                }
                case SpaceType::delay:
                {
                    const double bpm = hostBpm > 20.0 ? hostBpm : 120.0;
                    bank.delay.set (kDelayBeats[std::clamp (static_cast<int> (v (dlyTime)), 0, 8)] * 60.0 / bpm,
                                    v (dlyFeedback) / 100.0, v (dlyTone), v (dlyPingPong) / 100.0);
                    bank.delay.process (inL, inR, oL, oR, n);
                    break;
                }
                case SpaceType::chorus:
                    bank.chorus.set (v (choRate), v (choDepth) / 100.0, v (choDelay), v (choWidth) / 100.0);
                    bank.chorus.process (inL, inR, oL, oR, n);
                    break;
                case SpaceType::phaser:
                    bank.phaser.set (v (phaRate), v (phaDepth) / 100.0, v (phaFeedback) / 100.0, v (phaCentre));
                    bank.phaser.process (inL, inR, oL, oR, n);
                    break;
            }
        };

        run (type, bank.a);
        double mixAmt = v (kMixIds[type]) / 100.0;
        if (bank.fade < 1.0 && bank.prevType >= 0)
        {
            run (bank.prevType, bank.b);
            const double f0 = bank.fade, f1 = std::min (1.0, bank.fade + n / (0.06 * rate));
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const double f = f0 + (f1 - f0) * (i + 1) / n;
                    bank.a[ix (c)][ix (i)] = f * bank.a[ix (c)][ix (i)] + (1.0 - f) * bank.b[ix (c)][ix (i)];
                }
            mixAmt = f1 * mixAmt + (1.0 - f1) * v (kMixIds[bank.prevType]) / 100.0;
            bank.fade = f1;
        }

        // The wet sound's EQ: low cut and high cut (12 dB / octave).
        if (bank.lcHz != cur[spLowCut]) { bank.lcHz = cur[spLowCut]; bank.lowCut.k = design (Shape::highPass, bank.lcHz, 0.0, 0.7071, rate); }
        if (bank.hcHz != cur[spHighCut]) { bank.hcHz = cur[spHighCut]; bank.highCut.k = design (Shape::lowPass, bank.hcHz, 0.0, 0.7071, rate); }

        // Ducking: quieter while the key (the vocal) is there, back up in the gaps.
        const double chunkSec = static_cast<double> (n) / rate;
        double duck0 = bank.duckDb, duck1 = 0.0;
        if (hostKey >= 0.0)
        {
            const double key = hostKey;
            bank.keyEnv = key > bank.keyEnv ? key + (bank.keyEnv - key) * std::exp (-chunkSec / 0.005) : key + (bank.keyEnv - key) * std::exp (-chunkSec / 0.12);
            const double keyDb = gainToDb (bank.keyEnv);
            const double want = v (duckAmount) * std::clamp ((keyDb + 50.0) / 18.0, 0.0, 1.0);
            duck1 = want + (bank.duckDb - want) * std::exp (-chunkSec / (want > bank.duckDb ? 0.015 : 0.3));
        }
        else
            duck1 = bank.duckDb * std::exp (-chunkSec / 0.3);
        bank.duckDb = duck1;

        const bool fx = v (spRoute) > 0.5;
        const double level = fx ? dbToGain (v (spLevel)) : 1.0;   // FX channel: how loud the effect comes back
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double dg = dbToGain (-(duck0 + (duck1 - duck0) * (i + 1) / n));
            for (int c = 0; c < nch; ++c)
            {
                double wet = bank.a[ix (c)][ix (i)];
                wet = bank.highCut.process (c, bank.lowCut.process (c, wet)) * dg;
                // Safety: a runaway tail (huge feedback) is rounded off instead of exploding.
                const double aw = std::abs (wet);
                if (aw > 0.8) wet = std::copysign (0.8 + 0.4 * std::tanh ((aw - 0.8) / 0.4), wet);
                auto& x = work[ix (c)][ix (i)];
                x = fx ? wet * level : x + mixAmt * (wet - x);
                peak = std::max (peak, std::abs (fx ? x : wet));
            }
        }
        meters.spaceWet = metric (meters.spaceWet, static_cast<float> (gainToDb (peak)));
        meters.duck = metric (meters.duck, static_cast<float> (duck1));
    }
}
