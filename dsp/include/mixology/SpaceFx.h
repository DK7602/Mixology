#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/** Milestone 5, round 2: the Space effects (the Space page). Each one makes the WET sound only; ProcChain
    mixes it with the dry track (or plays it alone on an FX channel), filters it (low / high cut) and ducks it
    under the vocal.

    - FdnReverb: an 8-line feedback delay network (Hadamard mixing, damping inside the loop, slow modulation,
      input diffusion, early reflections). Reverb (plate), Hall and Room are this engine with different sizes.
    - StereoDelay: tempo-synced echoes with feedback, tone (low pass in the loop) and ping-pong.
    - Chorus: three modulated voices per side.
    - Phaser: six all-pass stages swept by an LFO, with feedback.
    - PitchShifter: two crossfaded taps (for the Special FX shimmer: pitched echoes fed back into a reverb).

    Framework-free, double precision, allocation only in prepare(). */
namespace mixology::dsp
{
    /** A delay line read at fractional delays (linear interpolation). reset() is instant: it forgets what was
        written (it reads as silence) instead of clearing seconds of memory, so the audio thread may call it. */
    class DelayLine
    {
    public:
        void prepare (int maxDelaySamples);
        void reset();
        void push (double x) { buf[pos & mask] = x; ++pos; filled += filled <= mask ? 1 : 0; }
        /** The sample written `delay` samples before the latest one (0 = the latest). */
        double read (double delay) const
        {
            const double d = delay < 0.0 ? 0.0 : (delay > maxDelay ? maxDelay : delay);
            const auto whole = static_cast<std::size_t> (d);
            const double frac = d - static_cast<double> (whole);
            const double a = whole < filled ? buf[(pos - 1 - whole) & mask] : 0.0;
            const double b = whole + 1 < filled ? buf[(pos - 2 - whole) & mask] : 0.0;
            return a + (b - a) * frac;
        }
        double maxDelaySamples() const { return maxDelay; }

    private:
        std::vector<double> buf;
        std::size_t mask = 0, pos = 0, filled = 0;   // filled: samples written since the last reset (up to the size)
        double maxDelay = 0.0;
    };

    class FdnReverb
    {
    public:
        static constexpr int kLines = 8;
        struct Settings
        {
            double preDelayMs = 20.0;
            double decaySeconds = 1.8;   // RT60
            double dampHz = 6500.0;      // high frequencies fade faster above this
            double size = 1.0;           // delay-line scale (0.25 small room .. 2.2 large hall)
            double width = 1.0;          // 0 mono .. 1 full
            double early = 0.0;          // early reflections level (0..1)
            double late = 1.0;           // the tail's level (0..1)
            double modMs = 0.25;         // slow pitch drift of the tail (smoother, less metallic)
            double modHz = 0.5;
            bool freeze = false;         // the tail holds forever, nothing new comes in
        };
        void prepare (double sampleRate);
        void reset();
        void set (const Settings&);
        /** Mono or stereo in, stereo wet out. `extra` (optional) is added to the tail's input (the shimmer). */
        void process (const double* inL, const double* inR, double* outL, double* outR, int n, const double* extra = nullptr);

    private:
        double rate = 48000.0;
        Settings s;
        DelayLine pre;
        std::array<DelayLine, 4> diffusers;
        std::array<double, 4> diffDelay {};
        DelayLine early;
        std::array<DelayLine, kLines> lines;
        std::array<double, kLines> length {}, gain {}, damp {}, lfoPhase {}, lfoInc {};
        double dampA = 1.0;
        double erScale = 1.0;
    };

    class StereoDelay
    {
    public:
        void prepare (double sampleRate);
        void reset();
        /** feedback 0..0.95, pingPong 0..1. */
        void set (double timeSeconds, double feedback, double toneHz, double pingPong);
        void process (const double* inL, const double* inR, double* outL, double* outR, int n);

    private:
        double rate = 48000.0, time = 0.25, target = 0.25, timeC = 0.0, fb = 0.35, pp = 0.0, lpA = 1.0, hpA = 0.0;
        DelayLine dl, dr;
        double lpL = 0.0, lpR = 0.0, hpL = 0.0, hpR = 0.0;
        bool fresh = true;
    };

    class Chorus
    {
    public:
        void prepare (double sampleRate);
        void reset();
        /** depth 0..1, width 0..1. */
        void set (double rateHz, double depth, double delayMs, double width);
        void process (const double* inL, const double* inR, double* outL, double* outR, int n);

    private:
        double rate = 48000.0, lfoHz = 0.8, depth = 0.4, base = 12.0, width = 1.0, phase = 0.0;
        DelayLine dl, dr;
    };

    class Phaser
    {
    public:
        static constexpr int kStages = 6;
        void prepare (double sampleRate);
        void reset();
        /** depth 0..1 (sweep, up to +-2 octaves), feedback 0..0.9. */
        void set (double rateHz, double depth, double feedback, double centreHz);
        void process (const double* inL, const double* inR, double* outL, double* outR, int n);

    private:
        double rate = 48000.0, lfoHz = 0.4, depth = 0.6, fb = 0.4, centre = 1000.0, phase = 0.0;
        std::array<std::array<double, kStages>, 2> x1 {}, y1 {};
        std::array<double, 2> last {};
    };

    class PitchShifter
    {
    public:
        void prepare (double sampleRate);
        void reset();
        void setSemitones (double st) { ratio = std::pow (2.0, st / 12.0); }
        double process (double x);

    private:
        double rate = 48000.0, ratio = 2.0, window = 2400.0, phase = 0.0;
        DelayLine line;
    };
}
