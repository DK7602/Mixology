#pragma once

#include "mixology/TrackAnalyzer.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/** Analyze Mix: listens to every track (through its Mixology Link) and the whole mix while the song plays,
    then explains in plain language what stands out, each point with "Do I need to fix it?".

    Framework-free and unit-tested. The plug-in feeds it one Moment about five times a second while the
    transport runs; evaluate() can run again later (e.g. after you correct a track's role) without listening
    again, because the moments are kept. */
namespace mixology::mix
{
    // ---------------------------------------------------------------------------------------------
    // Track roles (same keys as the interface's ROLES table)

    enum class Role { kick, snare, hats, perc, bass, keys, synth, guitar, vox, adlibs, fx, other };
    constexpr int kRoleCount = 12;

    const char* roleKey (Role);
    std::optional<Role> roleFromKey (const std::string&);

    /** From the channel name ("808", "Lead Vox", "Hi Hat"...); nothing when the name doesn't say. */
    std::optional<Role> roleFromName (const std::string& name);

    /** The six colours of the Overview analyzer: Kick, Bass, Drums, Vocals, Instruments, Other. */
    enum class Group { kick, bass, drums, vocals, instruments, other };
    constexpr int kGroupCount = 6;
    Group groupOf (Role);
    const char* groupKey (Group);

    // ---------------------------------------------------------------------------------------------
    // Input

    struct TrackSample
    {
        std::uint32_t hash = 0;    // name hash (same as the Cubase bridge's); 0 = the host gave no name
        std::string name;
        double faderDb = 0.0;      // the channel's Cubase fader (Link measures before it)
        bool faderKnown = false;
        AnalysisFrame frame;       // what its Mixology Link heard (before the fader)
    };

    struct Moment
    {
        AnalysisFrame mix;         // what Mixology on the Stereo Out heard
        std::vector<TrackSample> tracks;
    };

    /** The mix style's targets (from the style picked in the header). */
    struct Style
    {
        std::string key = "hiphop.modern";   // "genre.style"
        std::string name = "Hip Hop - Modern Clean";
        double bass = 4.0, scoop = 1.5, air = 2.0;   // tonal target (dB), same as the Tone page
    };

    struct TrackSettings
    {
        std::optional<Role> role;  // your correction
        bool excluded = false;     // left out of Analyze Mix (reference tracks, printed stems...)
    };

    // ---------------------------------------------------------------------------------------------
    // Output

    struct TrackReport
    {
        std::uint32_t hash = 0;
        std::string name;
        Role role = Role::other;
        std::string roleFrom;      // "you", "name" or "sound"
        bool excluded = false;
        bool faderKnown = false;
        double activity = 0.0;     // share of the listening time it was playing
        double loudness = -120.0;  // after the fader, while playing (LUFS-like)
        double peakBeforeFader = -120.0;
        double width = 0.0;        // 0 mono .. 1 all sides
        double lowWidth = 0.0;     // the same below 160 Hz
        double crest = 0.0;        // peak minus RMS (dB): punchy / transient sounds score high
        std::vector<double> bands; // average spectrum after the fader (dB per 1/3 octave)
    };

    struct Suggestion
    {
        std::string id;            // stable key ("kick-bass", "mud"...)
        std::string kind;          // "warn" (worth fixing), "info" (have a look), "calm" (fine)
        std::string icon;          // interface icon name
        std::string title;
        std::string text;
        std::string need;          // "Do I need to fix it?" answer
        std::vector<std::string> steps;
        std::vector<std::string> tracks;   // the tracks it is about
    };

    struct Report
    {
        bool valid = false;
        double secondsHeard = 0.0;
        Style style;
        std::vector<TrackReport> tracks;
        std::vector<Suggestion> suggestions;
        std::vector<double> mixBands;     // average mix spectrum (dB), and the style's target, both
        std::vector<double> targetBands;  // normalised to the same overall level
        double mixLoudness = -120.0, mixPeak = -120.0, mixCorrelation = 1.0;
        std::vector<std::string> unheard; // channels in Cubase without a Link (not part of this analysis)
    };

    // ---------------------------------------------------------------------------------------------

    class Analysis
    {
    public:
        static constexpr double kMomentSeconds = 0.2;
        static constexpr int kMaxMoments = 600;      // 2 minutes
        static constexpr double kEnoughSeconds = 20.0;

        void begin (const Style&);
        void clear();
        void add (Moment);
        double secondsHeard() const { return static_cast<double> (moments.size()) * kMomentSeconds; }
        bool full() const { return static_cast<int> (moments.size()) >= kMaxMoments; }
        bool hasMoments() const { return ! moments.empty(); }

        Report evaluate (const std::map<std::uint32_t, TrackSettings>& settings,
                         const std::vector<std::string>& unheard = {}) const;

        /** The style's tonal target at a frequency (same curve as the Tone page; relative dB). */
        static double targetDb (const Style&, double hz);

    private:
        Style style;
        std::vector<Moment> moments;
    };
}
