// CPU check: a big session the way Cubase runs it. Loads N real "Mixology Link.vst3" instances (one per track) and the
// real "Mixology.vst3" on the Stereo Out, plays music-like audio through them and measures:
//   - the audio thread: the time each block takes for all the Links plus Mixology, against the block's length
//     (mean and the worst 0.1 %), in three setups: everything off (as inserted), typical after Auto Mix (EQ and a
//     compressor on every track, a reverb on two FX tracks), and everything on (every module of every Link);
//   - the rest: the Links' analysis thread and their timers (the message thread), while the audio plays in real time.
// Usage: MixologyCpuCheck <Mixology Link.vst3> <Mixology.vst3> [tracks = 48] [block = 256] [--strict]
// Prints a table; with --strict it fails when a budget below is exceeded (CI runs it so).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <sys/resource.h>
 #include <time.h>
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    // ---- CPU time of this thread / the whole process (seconds) ------------------------------------
    double threadCpuSeconds()
    {
#if JUCE_WINDOWS
        FILETIME c, e, k, u;
        GetThreadTimes (GetCurrentThread(), &c, &e, &k, &u);
        auto sec = [] (const FILETIME& f) { return (static_cast<double> (f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) * 1.0e-7; };
        return sec (k) + sec (u);
#else
        timespec ts {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
        return static_cast<double> (ts.tv_sec) + static_cast<double> (ts.tv_nsec) * 1.0e-9;
#endif
    }

    double processCpuSeconds()
    {
#if JUCE_WINDOWS
        FILETIME c, e, k, u;
        GetProcessTimes (GetCurrentProcess(), &c, &e, &k, &u);
        auto sec = [] (const FILETIME& f) { return (static_cast<double> (f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) * 1.0e-7; };
        return sec (k) + sec (u);
#else
        rusage r {};
        getrusage (RUSAGE_SELF, &r);
        return static_cast<double> (r.ru_utime.tv_sec + r.ru_stime.tv_sec) + static_cast<double> (r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1.0e-6;
#endif
    }

    // ---- a playing transport ----------------------------------------------------------------------
    struct PlayHead final : juce::AudioPlayHead
    {
        std::atomic<juce::int64> samples { 0 };
        double rate = 48000.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (true);
            p.setBpm (120.0);
            p.setTimeInSamples (samples.load());
            p.setTimeInSeconds (static_cast<double> (samples.load()) / rate);
            p.setPpqPosition (static_cast<double> (samples.load()) / rate * 2.0);
            return p;
        }
    };

    // ---- music-like audio, different on every track (tones with an envelope on the beat, noise hits, some stereo) --
    struct Track
    {
        std::vector<float> l, r;
    };

    Track makeTrack (int index, double sr, int length)
    {
        std::mt19937 rng (static_cast<unsigned> (1000 + index));
        std::uniform_real_distribution<double> u (0.0, 1.0);
        Track t;
        t.l.resize (static_cast<std::size_t> (length));
        t.r.resize (static_cast<std::size_t> (length));
        const double f1 = 40.0 * std::pow (2.0, u (rng) * 7.0), f2 = f1 * (1.5 + u (rng)), beat = sr * 60.0 / 120.0 / (1 + index % 4);
        const double width = index % 2 == 0 ? 0.0 : 0.3 + 0.4 * u (rng);
        for (int i = 0; i < length; ++i)
        {
            const double env = std::exp (-std::fmod (static_cast<double> (i), beat) / (0.15 * sr));
            const double tone = 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * f1 * i / sr) + 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * f2 * i / sr);
            const double noise = (u (rng) * 2.0 - 1.0) * 0.15 * env;
            const double side = (u (rng) * 2.0 - 1.0) * 0.1 * width;
            const double m = 0.5 * (tone * (0.3 + 0.7 * env) + noise);
            t.l[static_cast<std::size_t> (i)] = static_cast<float> (m + side);
            t.r[static_cast<std::size_t> (i)] = static_cast<float> (width > 0.0 ? m - side : m);
        }
        return t;
    }

    juce::AudioProcessorParameter* find (juce::AudioProcessor& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (64) == name) return param;
        return nullptr;
    }

    int failures = 0;
    void setOrFail (juce::AudioProcessor& p, const juce::String& name, float normalised)
    {
        if (auto* param = find (p, name)) { param->setValueNotifyingHost (normalised); return; }
        std::printf ("FAIL parameter '%s' not found\n", name.toRawUTF8());
        ++failures;
    }

    struct Session
    {
        double sr = 48000.0;
        int block = 256;
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> links;
        std::unique_ptr<juce::AudioPluginInstance> mix;
        std::vector<Track> tracks;
        PlayHead head;
        juce::AudioBuffer<float> trackBuf, mixBuf;
        juce::MidiBuffer midi;
        int position = 0;
        int length = 0;

        // One block of the whole session; returns the seconds the Links and Mixology took.
        std::pair<double, double> runBlock()
        {
            mixBuf.clear();
            const auto t0 = Clock::now();
            for (std::size_t k = 0; k < links.size(); ++k)
            {
                const auto& tr = tracks[k];
                for (int i = 0; i < block; ++i)
                {
                    const auto at = static_cast<std::size_t> ((position + i) % length);
                    trackBuf.setSample (0, i, tr.l[at]);
                    trackBuf.setSample (1, i, tr.r[at]);
                }
                links[k]->processBlock (trackBuf, midi);
                mixBuf.addFrom (0, 0, trackBuf, 0, 0, block, 0.25f);
                mixBuf.addFrom (1, 0, trackBuf, 1, 0, block, 0.25f);
            }
            const auto t1 = Clock::now();
            mix->processBlock (mixBuf, midi);
            const auto t2 = Clock::now();
            position = (position + block) % length;
            head.samples += block;
            return { std::chrono::duration<double> (t1 - t0).count(), std::chrono::duration<double> (t2 - t1).count() };
        }
    };

    struct AudioResult { double meanPct = 0, worstPct = 0, linkPct = 0, mixPct = 0, perLinkPct = 0; };

    // As fast as possible on this thread: the share of each block's real-time length the session needs.
    AudioResult measureAudio (Session& s, double seconds)
    {
        const int blocks = static_cast<int> (seconds * s.sr / s.block);
        const double blockSec = s.block / s.sr;
        for (int i = 0; i < 200; ++i) s.runBlock();   // warm up (caches, smoothing, the reverb's first fill)
        std::vector<double> totals;
        totals.reserve (static_cast<std::size_t> (blocks));
        double linkSum = 0.0, mixSum = 0.0;
        for (int i = 0; i < blocks; ++i)
        {
            const auto [l, m] = s.runBlock();
            linkSum += l;
            mixSum += m;
            totals.push_back (l + m);
        }
        if (const auto dump = juce::SystemStats::getEnvironmentVariable ("CPU_DUMP", ""); dump.isNotEmpty())
        {
            juce::String lines;
            for (double t : totals) lines << juce::String (t * 1.0e6, 1) << "\n";
            juce::File (dump).appendText (lines + "---\n");
        }
        std::sort (totals.begin(), totals.end());
        AudioResult r;
        r.linkPct = 100.0 * linkSum / blocks / blockSec;
        r.mixPct = 100.0 * mixSum / blocks / blockSec;
        r.meanPct = r.linkPct + r.mixPct;
        r.worstPct = 100.0 * totals[static_cast<std::size_t> (static_cast<double> (totals.size() - 1) * 0.999)] / blockSec;
        r.perLinkPct = r.linkPct / static_cast<double> (s.links.size());
        return r;
    }

    struct BackgroundResult { double messagePct = 0, otherPct = 0, audioPct = 0; };

    // Audio in real time on its own thread (like Cubase's), the message loop here: what else the plug-ins use.
    BackgroundResult measureBackground (Session& s, double seconds)
    {
        std::atomic<bool> stop { false };
        std::atomic<double> audioCpu { 0.0 };
        const auto p0 = processCpuSeconds();
        const auto m0 = threadCpuSeconds();
        std::thread audio ([&] {
            const auto a0 = threadCpuSeconds();
            auto next = Clock::now();
            const auto step = std::chrono::duration_cast<Clock::duration> (std::chrono::duration<double> (s.block / s.sr));
            while (! stop.load())
            {
                s.runBlock();
                next += step;
                std::this_thread::sleep_until (next);
            }
            audioCpu = threadCpuSeconds() - a0;
        });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (static_cast<int> (seconds * 1000.0));
        const auto m1 = threadCpuSeconds();
        stop = true;
        audio.join();
        const auto p1 = processCpuSeconds();
        BackgroundResult r;
        r.messagePct = 100.0 * (m1 - m0) / seconds;
        r.audioPct = 100.0 * audioCpu.load() / seconds;
        r.otherPct = std::max (0.0, 100.0 * (p1 - p0) / seconds - r.messagePct - r.audioPct);
        return r;
    }

    void allLinks (Session& s, const std::function<void (juce::AudioPluginInstance&, std::size_t)>& f)
    {
        for (std::size_t k = 0; k < s.links.size(); ++k) f (*s.links[k], k);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);   // timers make the Space engines etc.
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::printf ("usage: MixologyCpuCheck <Mixology Link.vst3> <Mixology.vst3> [tracks = 48] [block = 256] [--strict]\n");
        return 2;
    }
    bool strict = false;
    std::vector<juce::String> args;
    for (int i = 3; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--strict") strict = true;
        else if (juce::String (argv[i]) == "--modules") args.push_back ("--modules");
        else args.push_back (argv[i]);
    }
    Session s;
    std::vector<juce::String> numbers;
    for (const auto& a : args) if (a.containsOnly ("0123456789")) numbers.push_back (a);
    const int trackCount = numbers.size() > 0 ? juce::jlimit (1, 200, numbers[0].getIntValue()) : 48;
    s.block = numbers.size() > 1 ? juce::jlimit (32, 4096, numbers[1].getIntValue()) : 256;
    s.head.rate = s.sr;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> linkTypes, mixTypes;
    format.findAllTypesForFile (linkTypes, juce::String::fromUTF8 (argv[1]));
    format.findAllTypesForFile (mixTypes, juce::String::fromUTF8 (argv[2]));
    if (linkTypes.isEmpty() || mixTypes.isEmpty()) { std::printf ("FAIL could not load the plug-ins\n"); return 1; }

    juce::String error;
    s.length = static_cast<int> (s.sr * 2.0);
    for (int k = 0; k < trackCount; ++k)
    {
        auto p = format.createInstanceFromDescription (*linkTypes[0], s.sr, s.block, error);
        if (p == nullptr) { std::printf ("FAIL Link %d: %s\n", k, error.toRawUTF8()); return 1; }
        p->setPlayConfigDetails (2, 2, s.sr, s.block);
        p->setPlayHead (&s.head);
        p->prepareToPlay (s.sr, s.block);
        juce::AudioProcessor::TrackProperties tp;
        tp.name = juce::String ("Track ") + juce::String (k + 1);
        p->updateTrackProperties (tp);
        s.links.push_back (std::move (p));
        s.tracks.push_back (makeTrack (k, s.sr, s.length));
    }
    s.mix = format.createInstanceFromDescription (*mixTypes[0], s.sr, s.block, error);
    if (s.mix == nullptr) { std::printf ("FAIL Mixology: %s\n", error.toRawUTF8()); return 1; }
    s.mix->setPlayConfigDetails (2, 2, s.sr, s.block);
    s.mix->setPlayHead (&s.head);
    s.mix->prepareToPlay (s.sr, s.block);
    s.trackBuf.setSize (2, s.block);
    s.mixBuf.setSize (2, s.block);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);

    const double blockMs = 1000.0 * s.block / s.sr;
    std::printf ("CPU check: %d Mixology Links + Mixology, %.0f Hz, %d-sample blocks (%.2f ms), one core\n", trackCount, s.sr, s.block, blockMs);
    std::printf ("%-34s %9s %9s %10s %9s %11s\n", "setup", "mean", "worst", "per Link", "Mixology", "budget");
    std::printf ("%-34s %9s %9s\n", "", "", "(0.1 %)");

    // Budgets on the mean (one core, all tracks): the worst blocks depend on the machine (other programs, a busy
    // virtual machine), so they are shown, not judged. Cubase spreads the tracks over all cores besides.
    struct Budget { const char* name; double mean; };
    auto report = [&] (const Budget& b, const AudioResult& r) {
        const bool ok = r.meanPct <= b.mean;
        std::printf ("%-34s %8.1f%% %8.1f%% %9.2f%% %8.1f%%   %s (mean <= %.0f %%)\n", b.name, r.meanPct, r.worstPct, r.perLinkPct, r.mixPct,
                     ok ? "ok  " : "OVER", b.mean);
        if (! ok && strict) ++failures;
    };

    // 1. As inserted: every module off (the audio passes bit for bit), the meters and analysis running.
    const auto off = measureAudio (s, 10.0);
    report ({ "everything off (as inserted)", 15.0 }, off);

    // 2. After Auto Mix, typically: EQ and a compressor on every track, a reverb on two FX tracks, Tone Balance on the mix.
    allLinks (s, [] (juce::AudioPluginInstance& p, std::size_t k) {
        setOrFail (p, "EQ", 1.0f);
        setOrFail (p, "Compressor", 1.0f);
        if (k < 2) setOrFail (p, "Space", 1.0f);
    });
    setOrFail (*s.mix, "Tone Balance", 1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    const auto typical = measureAudio (s, 10.0);
    report ({ "typical (EQ + comp, 2 reverbs)", 35.0 }, typical);
    const auto bg = measureBackground (s, 6.0);

    // 3. Everything on everywhere (no real session does this: the ceiling).
    allLinks (s, [] (juce::AudioPluginInstance& p, std::size_t) {
        for (const char* m : { "Multiband", "Transient", "Gate", "Limiter", "Tone Balance", "Loudness", "Clip Control", "Harmonics",
                               "Analog Touch", "Final Touch", "Stereo", "Space" })
            setOrFail (p, m, 1.0f);
    });
    const auto heavy = measureAudio (s, 5.0);
    report ({ "everything on (the ceiling)", 1000.0 }, heavy);

    // 4. One module at a time on every Link (--modules): which ones cost the most.
    if (std::find (args.begin(), args.end(), juce::String ("--modules")) != args.end() || juce::SystemStats::getEnvironmentVariable ("CPU_MODULES", "") == "1")
    {
        const char* modules[] = { "EQ", "Compressor", "Multiband", "Transient", "Gate", "Limiter", "Tone Balance", "Loudness", "Clip Control",
                                  "Harmonics", "Analog Touch", "Final Touch", "Stereo", "Space" };
        std::printf ("\nOne module on every Link (cost per Link, on top of 'off'):\n");
        for (const char* only : modules)
        {
            allLinks (s, [&] (juce::AudioPluginInstance& p, std::size_t) { for (const char* m : modules) setOrFail (p, m, std::string (m) == only ? 1.0f : 0.0f); });
            const auto r = measureAudio (s, 3.0);
            std::printf ("  %-14s %6.2f %% per Link\n", only, r.perLinkPct - off.perLinkPct);
        }
    }

    std::printf ("\nWhile it plays in real time (typical setup), besides the audio thread (%.1f %% of a core):\n", bg.audioPct);
    std::printf ("  message thread (the Links' timers)      %5.1f %% of a core   %s\n", bg.messagePct, bg.messagePct <= 15.0 ? "ok" : "OVER (<= 15 %)");
    std::printf ("  other threads (the Links' analysis)     %5.1f %% of a core   %s\n", bg.otherPct, bg.otherPct <= 25.0 ? "ok" : "OVER (<= 25 %)");
    if (strict && (bg.messagePct > 15.0 || bg.otherPct > 25.0)) ++failures;

    for (auto& p : s.links) p->releaseResources();
    s.mix->releaseResources();
    s.links.clear();
    s.mix.reset();
    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
