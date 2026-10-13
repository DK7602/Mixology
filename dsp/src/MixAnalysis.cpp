#include "mixology/MixAnalysis.h"

#include "mixology/BridgeProtocol.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <regex>
#include <set>

namespace mixology::mix
{
    namespace
    {
        double toE (double db) { return std::pow (10.0, db / 10.0); }
        double toDb (double e) { return e > 1.0e-14 ? 10.0 * std::log10 (e) : -120.0; }

        std::string fmt1 (double v)
        {
            char b[32];
            std::snprintf (b, sizeof (b), "%.1f", v);
            return b;
        }
        std::string fmt0 (double v)
        {
            char b[32];
            std::snprintf (b, sizeof (b), "%.0f", v);
            return b;
        }
        std::string signedDb (double v) { return (v >= 0.05 ? "+" : "") + fmt1 (v) + " dB"; }

        std::string joinNames (const std::vector<std::string>& names, std::size_t max = 3)
        {
            std::string out;
            for (std::size_t i = 0; i < names.size() && i < max; ++i)
            {
                if (i > 0) out += i + 1 == std::min (names.size(), max) ? " and " : ", ";
                out += "'" + names[i] + "'";
            }
            if (names.size() > max) out += " and " + std::to_string (names.size() - max) + " more";
            return out;
        }

        // Band ranges (indices into the 31 one-third-octave bands)
        constexpr int kSubTo = 5;      // <= 63 Hz
        constexpr int kLowTo = 9;      // <= 158 Hz
        constexpr int kHpTo = 7;       // <= 100 Hz: what a high-pass on a non-bass track would remove
        constexpr int kMudFrom = 10, kMudTo = 14;      // 200 - 500 Hz
        constexpr int kMidFrom = 15, kMidTo = 20;      // 630 Hz - 2 kHz
        constexpr int kPresFrom = 19, kPresTo = 23;    // 1.6 - 4 kHz: where words are understood
        constexpr int kHarshFrom = 21, kHarshTo = 24;  // 2.5 - 5 kHz
        constexpr int kHighFrom = 25;                  // >= 6.3 kHz
        constexpr int kAirFrom = 26, kAirTo = 29;      // 8 - 16 kHz
        constexpr int kKickBassFrom = 3, kKickBassTo = 9;   // 40 - 160 Hz

        double rangeE (const std::array<float, kBands>& db, int from, int to, double offset = 0.0)
        {
            double e = 0.0;
            for (int b = from; b <= to; ++b) e += toE (db[static_cast<std::size_t> (b)] + offset);
            return e;
        }
        double rangeE (const std::vector<double>& db, int from, int to)
        {
            double e = 0.0;
            for (int b = from; b <= to; ++b) e += toE (db[static_cast<std::size_t> (b)]);
            return e;
        }

        // How far above (+) or below (-) the rest of the mix the lead vocal usually sits, per genre (dB, measured
        // as K-weighted loudness while the vocal sings). Vocal-led genres sit it forward; rock keeps it in the band.
        double vocalTarget (const std::string& styleKey)
        {
            const auto genre = styleKey.substr (0, styleKey.find ('.'));
            if (genre == "rock") return -1.0;
            if (genre == "cinematic") return -2.0;
            if (genre == "electronic" || genre == "jazz") return 0.0;
            if (genre == "trap" || genre == "afro") return 0.5;
            if (genre == "pop" || genre == "country" || genre == "rnb" || genre == "folk") return 1.5;
            return 1.0;   // hip hop, gospel, worship, latin
        }

        // Left and right energy of a frame (from its level and balance), optionally after a fader.
        std::pair<double, double> leftRight (const AnalysisFrame& f, double faderDb = 0.0)
        {
            const double ms = toE (f.rmsDb + faderDb);       // mean square of the average channel
            const double r = toE (f.balanceDb);              // left / right
            return { 2.0 * ms * r / (1.0 + r), 2.0 * ms / (1.0 + r) };
        }
        constexpr double kVocalTolerance = 3.0;

        struct Acc   // one track over the whole listening time
        {
            std::uint32_t key = 0;
            std::string name;
            bool faderKnown = false;
            double maxLoud = -120.0;
            int seen = 0, active = 0;
            double loudE = 0.0, crestSum = 0.0, widthSum = 0.0, corrSum = 0.0, peak = -120.0, leftE = 0.0, rightE = 0.0;
            std::array<double, kBands> bandE {}, sideE {};
            std::vector<int> momentIndex;   // moments where it was active
        };

        std::uint32_t keyOf (const TrackSample& t) { return t.hash != 0 ? t.hash : bridge::nameHash ("#" + t.name); }

        bool activeIn (const TrackSample& t, double maxLoud)
        {
            const double l = t.frame.loudness + t.faderDb;
            return ! t.frame.silent && l > -70.0 && l > maxLoud - 25.0;
        }

        Role roleFromSound (const TrackReport& t)
        {
            double total = 0.0, low = 0.0, high = 0.0, mud = 0.0, mid = 0.0, pres = 0.0;
            for (int b = 0; b < kBands; ++b)
            {
                const double e = toE (t.bands[static_cast<std::size_t> (b)]);
                total += e;
                if (b <= kLowTo) low += e;
                if (b >= kHighFrom) high += e;
                if (b >= kMudFrom && b <= kMudTo) mud += e;
                if (b >= kMidFrom && b <= kMidTo) mid += e;
                if (b >= kHarshFrom && b <= kHarshTo) pres += e;
            }
            if (total <= 0.0) return Role::other;
            low /= total; high /= total; mud /= total; mid /= total; pres /= total;
            if (low > 0.55) return t.crest >= 11.0 ? Role::kick : Role::bass;
            if (high > 0.45) return Role::hats;
            if (t.crest >= 13.0) return mud + pres > 0.4 ? Role::snare : Role::perc;
            if (t.width < 0.12 && mid + pres > 0.45) return Role::vox;
            if (t.width >= 0.2) return Role::synth;
            if (mud > 0.35) return Role::keys;
            return Role::other;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Roles

    double vocalTargetFor (const std::string& styleKey) { return vocalTarget (styleKey); }

    const char* roleKey (Role r)
    {
        static const char* keys[] = { "kick", "snare", "hats", "perc", "bass", "keys", "synth", "guitar", "vox", "adlibs", "fx", "other" };
        return keys[static_cast<int> (r)];
    }

    std::optional<Role> roleFromKey (const std::string& k)
    {
        for (int i = 0; i < kRoleCount; ++i)
            if (k == roleKey (static_cast<Role> (i))) return static_cast<Role> (i);
        return std::nullopt;
    }

    std::optional<Role> roleFromName (const std::string& name)
    {
        // Same rules, same order as guessRole() in the interface (pages_main.js).
        static const std::vector<std::pair<Role, std::regex>> rules = [] {
            const auto f = std::regex::ECMAScript | std::regex::icase;
            return std::vector<std::pair<Role, std::regex>> {
                { Role::adlibs, std::regex ("ad.?lib|adlib|hype|dub vox|backing|bgv|double", f) },
                { Role::vox,    std::regex ("vox|vocal|voc\\b|lead v|verse|hook|rap|sing|acap", f) },
                { Role::kick,   std::regex ("kick|kik|\\bbd\\b|bassdrum", f) },
                { Role::snare,  std::regex ("snare|snr|clap|\\bsd\\b|rim", f) },
                { Role::hats,   std::regex ("hat|\\bhh\\b|cymbal|ride|crash|open h", f) },
                { Role::perc,   std::regex ("perc|shaker|tamb|conga|bongo|tom|cowbell|drum", f) },
                { Role::bass,   std::regex ("808|bass|sub\\b|subs", f) },
                { Role::keys,   std::regex ("key|piano|rhodes|organ|\\bep\\b|wurl", f) },
                { Role::guitar, std::regex ("guitar|gtr|acoustic|strat|electric", f) },
                { Role::synth,  std::regex ("synth|pad|lead|pluck|arp|string|brass|bell|choir|sample|melod|loop", f) },
                { Role::fx,     std::regex ("\\bfx\\b|riser|sweep|impact|reverse|noise|verb|delay|transition", f) },
            };
        }();
        for (const auto& [role, re] : rules)
            if (std::regex_search (name, re)) return role;
        return std::nullopt;
    }

    Group groupOf (Role r)
    {
        switch (r)
        {
            case Role::kick: return Group::kick;
            case Role::bass: return Group::bass;
            case Role::snare: case Role::hats: case Role::perc: return Group::drums;
            case Role::vox: case Role::adlibs: return Group::vocals;
            case Role::keys: case Role::synth: case Role::guitar: return Group::instruments;
            default: return Group::other;
        }
    }

    const char* groupKey (Group g)
    {
        static const char* keys[] = { "kick", "bass", "drums", "vocals", "instruments", "other" };
        return keys[static_cast<int> (g)];
    }

    // ---------------------------------------------------------------------------------------------

    void Analysis::begin (const Style& s)
    {
        style = s;
        moments.clear();
    }

    void Analysis::clear() { moments.clear(); }

    void Analysis::add (Moment m)
    {
        if (full()) return;
        moments.push_back (std::move (m));
    }

    double Analysis::targetDb (const Style& s, double hz)
    {
        const double lf = std::log2 (hz / 1000.0);
        const double bass = s.bass * std::exp (-std::pow (std::log2 (hz / 55.0), 2.0) / 2.2);
        const double scoop = -s.scoop * std::exp (-std::pow (std::log2 (hz / 350.0), 2.0) / 1.2);
        const double top = s.air * std::clamp ((lf - 2.0) / 3.0, 0.0, 1.0);
        return 2.0 + bass + scoop + top - 0.6 * lf;
    }

    Report Analysis::evaluate (const std::map<std::uint32_t, TrackSettings>& settings, const std::vector<std::string>& unheard) const
    {
        Report r;
        r.style = style;
        r.secondsHeard = secondsHeard();
        r.unheard = unheard;
        if (moments.empty()) return r;
        r.valid = true;

        // ---- each track over all moments -------------------------------------------------------
        std::map<std::uint32_t, Acc> acc;
        for (const auto& m : moments)
            for (const auto& t : m.tracks)
            {
                auto& a = acc[keyOf (t)];
                a.key = keyOf (t);
                a.name = t.name;
                a.faderKnown = a.faderKnown || t.faderKnown;
                if (! t.frame.silent) a.maxLoud = std::max (a.maxLoud, static_cast<double> (t.frame.loudness) + t.faderDb);
            }
        for (int mi = 0; mi < static_cast<int> (moments.size()); ++mi)
            for (const auto& t : moments[static_cast<std::size_t> (mi)].tracks)
            {
                auto& a = acc[keyOf (t)];
                ++a.seen;
                a.peak = std::max (a.peak, static_cast<double> (t.frame.peakDb));
                if (! activeIn (t, a.maxLoud)) continue;
                ++a.active;
                a.momentIndex.push_back (mi);
                a.loudE += toE (t.frame.loudness + t.faderDb);
                a.crestSum += t.frame.peakDb - t.frame.rmsDb;
                a.widthSum += t.frame.width;
                a.corrSum += t.frame.correlation;
                const auto [l, rgt] = leftRight (t.frame, t.faderDb);
                a.leftE += l;
                a.rightE += rgt;
                for (int b = 0; b < kBands; ++b)
                {
                    const auto i = static_cast<std::size_t> (b);
                    const double e = toE (t.frame.bandDb[i] + t.faderDb);
                    a.bandE[i] += e;
                    a.sideE[i] += e * t.frame.sideShare[i];
                }
            }

        const auto total = static_cast<double> (moments.size());
        std::map<std::uint32_t, std::size_t> indexOf;
        for (auto& [key, a] : acc)
        {
            TrackReport t;
            t.hash = key;
            t.name = a.name;
            t.faderKnown = a.faderKnown;
            t.peakBeforeFader = a.peak;
            t.activity = a.active / total;
            t.bands.assign (kBands, -120.0);
            if (a.active > 0)
            {
                const double n = a.active;
                t.loudness = toDb (a.loudE / n);
                t.crest = a.crestSum / n;
                t.width = a.widthSum / n;
                double lowE = 0.0, lowSide = 0.0;
                for (int b = 0; b < kBands; ++b)
                {
                    const auto i = static_cast<std::size_t> (b);
                    t.bands[i] = toDb (a.bandE[i] / n);
                    if (b <= kLowTo) { lowE += a.bandE[i]; lowSide += a.sideE[i]; }
                }
                t.lowWidth = lowE > 0.0 ? lowSide / lowE : 0.0;
                t.balance = a.rightE > 0.0 && a.leftE > 0.0 ? std::clamp (toDb (a.leftE) - toDb (a.rightE), -40.0, 40.0) : 0.0;
            }

            const auto it = settings.find (key);
            t.reference = it != settings.end() && it->second.reference;
            t.excluded = (it != settings.end() && it->second.excluded) || t.reference;
            if (it != settings.end() && it->second.role) { t.role = *it->second.role; t.roleFrom = "you"; }
            else if (auto byName = roleFromName (t.name)) { t.role = *byName; t.roleFrom = "name"; }
            else { t.role = roleFromSound (t); t.roleFrom = "sound"; }

            indexOf[key] = r.tracks.size();
            r.tracks.push_back (std::move (t));
        }
        std::sort (r.tracks.begin(), r.tracks.end(), [] (const TrackReport& a, const TrackReport& b) { return a.loudness > b.loudness; });
        indexOf.clear();
        for (std::size_t i = 0; i < r.tracks.size(); ++i) indexOf[r.tracks[i].hash] = i;
        auto trackOf = [&] (std::uint32_t key) -> const TrackReport& { return r.tracks[indexOf.at (key)]; };
        auto counts = [&] (const TrackReport& t) { return ! t.excluded && t.activity > 0.0; };

        // ---- the mix ---------------------------------------------------------------------------
        std::array<double, kBands> mixE {}, mixSide {};
        double mixLoudE = 0.0, mixCorr = 0.0, mixLeft = 0.0, mixRight = 0.0, mixWidthSum = 0.0;
        int mixN = 0;
        for (const auto& m : moments)
        {
            if (m.mix.silent) continue;
            ++mixN;
            mixLoudE += toE (m.mix.loudness);
            mixCorr += m.mix.correlation;
            mixWidthSum += m.mix.width;
            const auto [ml, mr] = leftRight (m.mix);
            mixLeft += ml;
            mixRight += mr;
            r.mixPeak = std::max (r.mixPeak, static_cast<double> (m.mix.peakDb));
            for (int b = 0; b < kBands; ++b)
            {
                const auto i = static_cast<std::size_t> (b);
                mixE[i] += toE (m.mix.bandDb[i]);
                mixSide[i] += toE (m.mix.bandDb[i]) * m.mix.sideShare[i];
            }
        }
        // The channels without a Link, together: what the Stereo Out hears beyond the Links (moments where
        // that is clearly more than measuring error).
        if (! unheard.empty())
        {
            double resE = 0.0;
            int resN = 0;
            for (const auto& m : moments)
            {
                if (m.mix.silent) continue;
                double linked = 0.0;
                for (const auto& t : m.tracks)
                {
                    const auto it = indexOf.find (keyOf (t));
                    if (t.frame.silent || it == indexOf.end() || r.tracks[it->second].excluded) continue;
                    linked += toE (t.frame.loudness + t.faderDb);
                }
                const double rest = toE (m.mix.loudness) - linked;
                if (rest > 0.1 * toE (m.mix.loudness)) { resE += rest; ++resN; }
            }
            if (resN > 0) r.unheardLoudness = toDb (resE / resN);
        }

        std::vector<double> mixDb (kBands, -120.0);
        if (mixN > 0)
        {
            r.mixLoudness = toDb (mixLoudE / mixN);
            r.mixCorrelation = mixCorr / mixN;
            r.mixWidth = mixWidthSum / mixN;
            r.mixBalance = mixLeft > 0.0 && mixRight > 0.0 ? toDb (mixLeft) - toDb (mixRight) : 0.0;
            for (int b = 0; b < kBands; ++b) mixDb[static_cast<std::size_t> (b)] = toDb (mixE[static_cast<std::size_t> (b)] / mixN);
        }

        // Tonal balance against the style: both curves tilted so a balanced mix reads flat (1/3-octave bands
        // of a typical mix fall about 1.5 dB per octave), then lined up on their average 40 Hz - 16 kHz.
        r.mixBands.assign (kBands, 0.0);
        r.targetBands.assign (kBands, 0.0);
        double mixMean = 0.0, targetMean = 0.0;
        for (int b = 3; b <= 29; ++b)
        {
            const double hz = bandHz (b);
            mixMean += mixDb[static_cast<std::size_t> (b)] + 1.5 * std::log2 (hz / 1000.0);
            targetMean += targetDb (style, hz);
        }
        mixMean /= 27.0;
        targetMean /= 27.0;
        for (int b = 0; b < kBands; ++b)
        {
            const double hz = bandHz (b);
            r.mixBands[static_cast<std::size_t> (b)] = mixN > 0 ? mixDb[static_cast<std::size_t> (b)] + 1.5 * std::log2 (hz / 1000.0) - mixMean : 0.0;
            r.targetBands[static_cast<std::size_t> (b)] = targetDb (style, hz) - targetMean;
        }
        // Reference Match: the reference song's spectrum, tilted and lined up the same way as the mix's.
        for (const auto& t : r.tracks)
        {
            if (! t.reference) continue;
            auto& ref = r.reference;
            ref.chosen = true;
            ref.hash = t.hash;
            ref.name = t.name;
            ref.heard = t.activity > 0.0 && mixN > 0;
            if (! ref.heard) break;
            ref.loudness = t.loudness;
            ref.width = t.width;
            ref.loudnessDiff = t.loudness - r.mixLoudness;
            ref.widthDiff = r.mixWidth - t.width;
            double mean = 0.0;
            for (int b = 3; b <= 29; ++b) mean += t.bands[static_cast<std::size_t> (b)] + 1.5 * std::log2 (bandHz (b) / 1000.0);
            mean /= 27.0;
            ref.bands.assign (kBands, 0.0);
            for (int b = 0; b < kBands; ++b)
                ref.bands[static_cast<std::size_t> (b)] = t.bands[static_cast<std::size_t> (b)] + 1.5 * std::log2 (bandHz (b) / 1000.0) - mean;
            struct Span { const char* name; const char* what; int from, to; };
            for (const auto& sp : { Span { "Low end (40 - 125 Hz)", "low end", 3, 8 }, Span { "Low mids (200 - 500 Hz)", "low mids", 10, 14 },
                                    Span { "Mids (630 Hz - 2 kHz)", "mids", 15, 20 }, Span { "Presence (2.5 - 5 kHz)", "presence", 21, 24 },
                                    Span { "Air (8 - 16 kHz)", "air", 26, 29 } })
            {
                double d = 0.0;
                for (int b = sp.from; b <= sp.to; ++b) d += r.mixBands[static_cast<std::size_t> (b)] - ref.bands[static_cast<std::size_t> (b)];
                d /= (sp.to - sp.from + 1);
                ReferenceReport::Region rg;
                rg.name = sp.name;
                rg.diff = d;
                rg.text = std::abs (d) < 1.5 ? std::string ("About the same as '") + t.name + "'."
                                             : "Your mix has " + fmt1 (std::abs (d)) + " dB " + (d > 0.0 ? "more " : "less ") + sp.what + " than '" + t.name + "'.";
                ref.regions.push_back (rg);
            }
            break;
        }

        auto deviation = [&] (int from, int to) {
            double d = 0.0;
            for (int b = from; b <= to; ++b) d += r.mixBands[static_cast<std::size_t> (b)] - r.targetBands[static_cast<std::size_t> (b)];
            return d / (to - from + 1);
        };
        auto contributors = [&] (int from, int to, std::size_t max) {
            std::vector<std::pair<double, std::string>> v;
            for (const auto& t : r.tracks)
                if (counts (t)) v.push_back ({ rangeE (t.bands, from, to) * t.activity, t.name });
            std::sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            std::vector<std::string> names;
            for (std::size_t i = 0; i < v.size() && i < max; ++i) names.push_back (v[i].second);
            return names;
        };

        auto& out = r.suggestions;

        // ---- 1. listening time and unheard channels --------------------------------------------
        if (r.secondsHeard < kEnoughSeconds)
            out.push_back ({ "short", "info", "clock", "Only " + fmt0 (r.secondsHeard) + " seconds heard",
                             "The more of the song Mixology hears, the surer it is. Short listens can miss the hook or the drop.",
                             "Not urgent: for a reliable picture let it hear at least 20 seconds of the busiest part.",
                             { "Press Analyze Mix again, play the hook or the busiest 30 - 60 seconds, then Finish." }, {} });
        if (! unheard.empty())
            out.push_back ({ "unheard", "info", "link", std::to_string (unheard.size()) + " channel" + (unheard.size() == 1 ? "" : "s") + " without Mixology Link",
                             joinNames (unheard, 4) + (unheard.size() == 1 ? " plays" : " play") + " in your mix, but Mixology can't hear " + (unheard.size() == 1 ? "it" : "them") + " one by one, so the advice below leaves " + (unheard.size() == 1 ? "it" : "them") + " out.",
                             "Only if those tracks matter for the balance (they usually do).",
                             { "Insert Mixology Link on each of them, after their other plug-ins, then Analyze Mix again." }, unheard });

        // ---- 2. vocal level against the beat ---------------------------------------------------
        // The beat = everything but the vocals. Measured two ways and the larger one kept: the tracks with a Link
        // added up, and the whole mix (Mixology on the Stereo Out) minus the vocals, which also counts channels
        // without a Link (e.g. a printed beat).
        std::vector<std::uint32_t> vocals, others, adlibs;
        for (const auto& t : r.tracks)
        {
            if (! counts (t)) continue;
            if (t.role == Role::vox) vocals.push_back (t.hash);
            else if (t.role == Role::adlibs) adlibs.push_back (t.hash);
            else others.push_back (t.hash);
        }
        if (! vocals.empty())
        {
            std::vector<double> ratios;
            for (const auto& m : moments)
            {
                double vE = 0.0, aE = 0.0, bE = 0.0;
                for (const auto& t : m.tracks)
                {
                    const auto key = keyOf (t);
                    const auto& a = acc.at (key);
                    if (! activeIn (t, a.maxLoud)) continue;
                    const double e = toE (t.frame.loudness + t.faderDb);
                    if (std::find (vocals.begin(), vocals.end(), key) != vocals.end()) vE += e;
                    else if (std::find (adlibs.begin(), adlibs.end(), key) != adlibs.end()) aE += e;
                    else if (std::find (others.begin(), others.end(), key) != others.end()) bE += e;
                }
                if (! m.mix.silent) bE = std::max (bE, toE (m.mix.loudness) - vE - aE);
                if (vE > 0.0 && bE > 1.0e-12) ratios.push_back (toDb (vE) - toDb (bE));
            }
            if (ratios.size() >= 5)
            {
                std::nth_element (ratios.begin(), ratios.begin() + static_cast<long> (ratios.size() / 2), ratios.end());
                const double ratio = ratios[ratios.size() / 2];
                const double target = vocalTarget (style.key);
                r.hasVocalRatio = true;
                r.vocalRatio = ratio;
                r.vocalTarget = target;
                std::vector<std::string> names;
                for (auto k : vocals) names.push_back (trackOf (k).name);
                if (ratio < target - kVocalTolerance)
                    out.push_back ({ "vocal-low", "warn", "vox", "The vocal is buried",
                                     "While it sings, the vocal sits " + fmt1 (-ratio) + " dB under the rest of the mix. For " + style.name
                                         + " it usually sits about " + (target >= 0 ? fmt0 (target) + " dB above" : fmt0 (-target) + " dB under") + " it.",
                                     "Yes, if the words are hard to follow. That is the first thing listeners notice.",
                                     { "Raise " + joinNames (names) + " about " + fmt0 (target - ratio) + " dB (or lower the beat as much)"
                                           + (target - ratio > 6.0 ? ": start with 6 dB and listen." : "."),
                                       "Then check the vocal masking point below: clearing room for the vocal often beats turning it up." }, names });
                else if (ratio > target + kVocalTolerance + 1.0)
                    out.push_back ({ "vocal-high", "info", "vox", "The vocal sits far on top",
                                     "While it sings, the vocal is " + fmt1 (ratio) + " dB louder than the rest of the mix: the beat may feel small behind it.",
                                     "Only if the beat sounds weak. Some songs want the vocal right in your face.",
                                     { "Try " + joinNames (names) + " 2 - 3 dB lower and listen to the hook again." }, names });
                else
                    out.push_back ({ "vocal-ok", "calm", "vox", "Vocal level fits " + style.name,
                                     "While it sings, the vocal sits " + signedDb (ratio) + " against the rest of the mix.", "No.", {}, names });
            }
        }

        // ---- 3. kick and 808 / bass ------------------------------------------------------------
        std::vector<std::uint32_t> kicks, basses;
        for (const auto& t : r.tracks)
        {
            if (! counts (t)) continue;
            if (t.role == Role::kick) kicks.push_back (t.hash);
            if (t.role == Role::bass) basses.push_back (t.hash);
        }
        if (! kicks.empty() && ! basses.empty())
        {
            double shareSum = 0.0, kickLowE = 0.0, bassLowE = 0.0;
            int together = 0;
            for (const auto& m : moments)
            {
                std::array<double, kBands> k {}, b {};
                bool kOn = false, bOn = false;
                for (const auto& t : m.tracks)
                {
                    const auto key = keyOf (t);
                    const bool isKick = std::find (kicks.begin(), kicks.end(), key) != kicks.end();
                    const bool isBass = std::find (basses.begin(), basses.end(), key) != basses.end();
                    if (! (isKick || isBass) || ! activeIn (t, acc.at (key).maxLoud)) continue;
                    for (int i = kKickBassFrom; i <= kKickBassTo; ++i)
                        (isKick ? k : b)[static_cast<std::size_t> (i)] += toE (t.frame.bandDb[static_cast<std::size_t> (i)] + t.faderDb);
                    (isKick ? kOn : bOn) = true;
                }
                if (! (kOn && bOn)) continue;
                double shared = 0.0, kT = 0.0, bT = 0.0;
                for (int i = kKickBassFrom; i <= kKickBassTo; ++i)
                {
                    const auto u = static_cast<std::size_t> (i);
                    shared += std::min (k[u], b[u]);
                    kT += k[u];
                    bT += b[u];
                }
                if (std::min (kT, bT) <= 0.0) continue;
                shareSum += shared / std::min (kT, bT);
                kickLowE += kT;
                bassLowE += bT;
                ++together;
            }
            std::vector<std::string> names;
            for (auto key : kicks) names.push_back (trackOf (key).name);
            for (auto key : basses) names.push_back (trackOf (key).name);
            if (together >= 5)
            {
                const double overlap = shareSum / together;
                const double balance = toDb (kickLowE) - toDb (bassLowE);
                if (overlap > 0.5)
                    out.push_back ({ "kick-bass", "warn", "kick", "Kick and bass fight for the low end",
                                     "When they hit together, " + fmt0 (overlap * 100.0) + " % of their low end (40 - 160 Hz) sits in the same place. That makes the bottom muddy and the kick lose its punch.",
                                     "Yes, for most " + style.name + " mixes: a clean low end is what makes it hit on big speakers and in the car.",
                                     { "Let each own a spot: the kick's punch around 60 - 100 Hz, the 808 / bass below 60 Hz (a small EQ dip in each where the other lives).",
                                       "Or duck the 808 for a moment each time the kick hits (sidechain compression on the 808, triggered by the kick)." }, names });
                if (balance < -12.0)
                    out.push_back ({ "kick-quiet", "info", "kick", "The kick hides under the bass",
                                     "In the low end the kick is " + fmt1 (-balance) + " dB under the 808 / bass while both play.",
                                     "Only if the kick doesn't come through. In 808-heavy styles the kick is often just the click on top.",
                                     { "Raise " + joinNames ({ trackOf (kicks.front()).name }) + " 2 - 3 dB, or bring out its click around 2 - 4 kHz." }, names });
            }
        }

        // ---- 4. tonal balance against the style -----------------------------------------------
        if (mixN >= 5)
        {
            const double mud = deviation (kMudFrom, kMudTo);
            const double harsh = deviation (kHarshFrom, kHarshTo);
            const double lows = deviation (kKickBassFrom, kKickBassTo - 1);
            const double air = deviation (kAirFrom, kAirTo);
            if (mud > 2.5)
            {
                const auto names = contributors (kMudFrom, kMudTo, 3);
                out.push_back ({ "mud", mud > 4.5 ? "warn" : "info", "freq", "Muddy low mids (200 - 500 Hz)",
                                 "The mix has " + fmt1 (mud) + " dB more 200 - 500 Hz than " + style.name + " usually does. Too much there sounds boxy and cloudy.",
                                 mud > 4.5 ? "Yes: it clouds everything else." : "Only if the mix sounds boxy or crowded.",
                                 { "The most in that range: " + joinNames (names) + ".", "Dip them 2 - 3 dB around 300 Hz (a wide EQ bell), starting with the ones that aren't bass instruments." }, names });
            }
            if (harsh > 2.5)
            {
                const auto names = contributors (kHarshFrom, kHarshTo, 3);
                out.push_back ({ "harsh", harsh > 4.5 ? "warn" : "info", "freq", "Harsh upper mids (2.5 - 5 kHz)",
                                 "The mix has " + fmt1 (harsh) + " dB more 2.5 - 5 kHz than " + style.name + " usually does. That's the range that tires ears at high volume.",
                                 harsh > 4.5 ? "Yes: it gets painful when played loud." : "Only if it sounds sharp or tiring.",
                                 { "The most in that range: " + joinNames (names) + ".", "Turn the loudest of them down 1 - 2 dB, or dip it around 3 - 4 kHz." }, names });
            }
            if (lows < -3.0 || lows > 3.0)
            {
                const bool light = lows < 0.0;
                std::vector<std::string> names;
                for (const auto& t : r.tracks) if (counts (t) && (t.role == Role::kick || t.role == Role::bass)) names.push_back (t.name);
                out.push_back ({ light ? "bass-light" : "bass-heavy", std::abs (lows) > 5.0 ? "warn" : "info", "bass",
                                 light ? "The low end is light for " + style.name : "The low end is heavy for " + style.name,
                                 "40 - 125 Hz is " + fmt1 (std::abs (lows)) + " dB " + (light ? "under" : "over") + " what " + style.name + " usually has.",
                                 light ? "Only if the mix sounds thin on good speakers or headphones." : "Only if it booms or your speakers struggle.",
                                 { names.empty() ? std::string (light ? "Raise the kick and bass tracks 1 - 2 dB." : "Lower the kick and bass tracks 1 - 2 dB.")
                                                 : std::string (light ? "Raise " : "Lower ") + joinNames (names) + " 1 - 2 dB." }, names });
            }
            if (air < -3.5 || air > 3.5)
            {
                const bool dull = air < 0.0;
                out.push_back ({ dull ? "dull" : "bright", "info", "highshelf", dull ? "The top end is dull" : "The top end is very bright",
                                 "8 - 16 kHz is " + fmt1 (std::abs (air)) + " dB " + (dull ? "under" : "over") + " what " + style.name + " usually has.",
                                 dull ? "Only if the mix sounds closed in. Masterology can add a little air at the end too." : "Only if hats and esses sound sharp.",
                                 { dull ? "Brighten the vocal and hats a little (a gentle high shelf above 8 kHz)." : "Turn the hats down 1 - 2 dB or soften them above 8 kHz." }, {} });
            }
        }

        // ---- 5. what masks the vocal -----------------------------------------------------------
        if (! vocals.empty())
        {
            std::map<std::uint32_t, std::pair<double, int>> diff;   // presence-band level vs the vocal's
            for (const auto& m : moments)
            {
                double vE = 0.0;
                for (const auto& t : m.tracks)
                {
                    const auto key = keyOf (t);
                    if (std::find (vocals.begin(), vocals.end(), key) != vocals.end() && activeIn (t, acc.at (key).maxLoud))
                        vE += rangeE (t.frame.bandDb, kPresFrom, kPresTo, t.faderDb);
                }
                if (vE <= 0.0) continue;
                for (const auto& t : m.tracks)
                {
                    const auto key = keyOf (t);
                    const auto& tr = trackOf (key);
                    if (! counts (tr) || tr.role == Role::vox || tr.role == Role::adlibs || ! activeIn (t, acc.at (key).maxLoud)) continue;
                    auto& d = diff[key];
                    d.first += toDb (rangeE (t.frame.bandDb, kPresFrom, kPresTo, t.faderDb)) - toDb (vE);
                    ++d.second;
                }
            }
            std::vector<std::pair<double, std::string>> maskers;
            for (const auto& [key, d] : diff)
            {
                if (d.second >= 5) r.tracks[indexOf.at (key)].maskDb = d.first / d.second;
                if (d.second >= 5 && d.first / d.second > -6.0) maskers.push_back ({ d.first / d.second, trackOf (key).name });
            }
            std::sort (maskers.begin(), maskers.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            if (! maskers.empty())
            {
                std::vector<std::string> names;
                for (std::size_t i = 0; i < maskers.size() && i < 3; ++i) names.push_back (maskers[i].second);
                const bool strong = maskers.front().first > -3.0;
                out.push_back ({ "vocal-pocket", strong ? "warn" : "info", "mic", "Instruments cover the vocal's words",
                                 joinNames (names) + (names.size() == 1 ? " is" : " are") + " nearly as loud as the vocal at 1.6 - 4 kHz while it sings: the range where words are understood.",
                                 strong ? "Yes, if lyrics get lost in busy parts." : "Only if the vocal disappears in the busy parts.",
                                 { "Dip " + joinNames (names) + " 2 - 3 dB around 2 - 3 kHz (a medium EQ bell), or pan " + (names.size() == 1 ? "it" : "them") + " a little off-centre.",
                                   "Vocal Pocket (left of the analyzer) makes these dips for you, in each track's Mixology Link." }, names });
            }
        }

        // ---- 6. low end in tracks that don't need it --------------------------------------------
        {
            std::vector<std::string> names;
            bool vocalAmong = false;
            const double mixSub = rangeE (mixDb, 0, kHpTo);
            for (const auto& t : r.tracks)
            {
                if (! counts (t) || t.role == Role::kick || t.role == Role::bass || t.role == Role::fx) continue;
                const double low = rangeE (t.bands, 0, kHpTo), all = rangeE (t.bands, 0, kBands - 1);
                if (all <= 0.0 || low / all < 0.2 || toDb (low) < toDb (mixSub) - 18.0) continue;
                r.tracks[indexOf.at (t.hash)].rumble = true;
                names.push_back (t.name);
                vocalAmong = vocalAmong || t.role == Role::vox || t.role == Role::adlibs;
            }
            if (! names.empty())
                out.push_back ({ "highpass", vocalAmong ? "warn" : "info", "lowcut", "Low rumble in tracks that don't need it",
                                 joinNames (names) + (names.size() == 1 ? " has" : " have") + " a lot of energy below 100 Hz. It adds mud and fights the kick and bass without being heard as part of the sound.",
                                 vocalAmong ? "Yes for vocals: there's nothing useful down there." : "Usually yes: it's free headroom and a cleaner low end.",
                                 { std::string ("Put a low cut (high-pass) on ") + (names.size() == 1 ? "it" : "them") + ": about 80 - 100 Hz for vocals, 60 - 80 Hz for keys, guitars and synths." }, names });
        }

        // ---- 7. stereo: low end in the middle, mono-safe -------------------------------------
        {
            std::vector<std::string> wideLows;
            for (const auto& t : r.tracks)
                if (counts (t) && (t.role == Role::kick || t.role == Role::bass) && t.lowWidth > 0.15) wideLows.push_back (t.name);
            double mixLowSide = 0.0, mixLowE = 0.0;
            for (int b = 0; b <= kLowTo; ++b) { mixLowSide += mixSide[static_cast<std::size_t> (b)]; mixLowE += mixE[static_cast<std::size_t> (b)]; }
            const bool mixLowWide = mixLowE > 0.0 && mixLowSide / mixLowE > 0.15;
            if (! wideLows.empty() || mixLowWide)
                out.push_back ({ "low-wide", "warn", "mono", "The low end isn't centred",
                                 (wideLows.empty() ? std::string ("Part of the mix's low end") : joinNames (wideLows)) + " spreads bass to the sides. Low end should sit in the middle: club systems and phones play it in mono and it can thin out or vanish.",
                                 "Yes: it's an easy win for every playback system.",
                                 { "Make the low end mono below about 120 Hz (Cubase's MultiPanner or a stereo-width plug-in on those tracks).",
                                   "Check any stereo widener or chorus on bass sounds." }, wideLows });
            std::vector<std::string> outOfPhase;
            for (const auto& t : r.tracks)
            {
                if (! counts (t) || t.activity <= 0.0) continue;
                const auto& a = acc.at (t.hash);
                if (a.active > 0 && a.corrSum / a.active < -0.3) outOfPhase.push_back (t.name);
            }
            if (! outOfPhase.empty() || (mixN >= 5 && r.mixCorrelation < 0.2))
                out.push_back ({ "phase", "warn", "corr", outOfPhase.empty() ? "The mix may fall apart in mono" : "A track is out of phase",
                                 outOfPhase.empty() ? "Left and right of the mix are very different (correlation " + fmt1 (r.mixCorrelation) + "). In mono (phones, club PA) parts can cancel out."
                                                    : joinNames (outOfPhase) + " has left and right working against each other: in mono it will get thin or disappear.",
                                 "Yes: check it in mono before you finish the mix.",
                                 { outOfPhase.empty() ? std::string ("Find the widest effects (wideners, stereo delays) and narrow them a little.")
                                                      : "Flip the phase of one side on " + joinNames (outOfPhase) + " (or remove the widener on it), then compare in mono." }, outOfPhase });
        }

        // ---- 7b. left / right balance ----------------------------------------------------------
        if (mixN >= 5 && std::abs (r.mixBalance) >= 1.5)
        {
            const bool left = r.mixBalance > 0.0;
            std::vector<std::pair<double, std::string>> pulling;
            for (const auto& t : r.tracks)
                if (counts (t) && (left ? t.balance > 6.0 : t.balance < -6.0) && t.loudness > r.mixLoudness - 18.0)
                    pulling.push_back ({ t.loudness, t.name });
            std::sort (pulling.begin(), pulling.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            std::vector<std::string> names;
            for (const auto& p : pulling) names.push_back (p.second);
            const std::string side = left ? "left" : "right", other = left ? "right" : "left";
            const bool strong = std::abs (r.mixBalance) >= 3.0;
            out.push_back ({ left ? "lean-left" : "lean-right", strong ? "warn" : "info", "pan",
                             "The mix leans to the " + side,
                             "The mix is " + fmt1 (std::abs (r.mixBalance)) + " dB louder on the " + side + " than on the " + other + "."
                                 + (names.empty() ? std::string() : " Pulling it there: " + joinNames (names, 4) + ".")
                                 + " On headphones and in the car the song feels lopsided.",
                             strong ? "Yes: a lopsided mix sounds unfinished." : "Only if it feels lopsided on headphones.",
                             { names.size() >= 2 ? "Pan some of " + joinNames (names, 4) + " to the " + other + " (for example: one guitar left, its double right)."
                                                 : std::string ("Balance what is panned hard to the ") + side + " with something of similar weight on the " + other + ".",
                               "Check nothing loud is panned to one side by mistake (Pan on the Tracks page shows L / R)." }, names });
        }

        // ---- 8. tracks you can hardly hear, tracks running very hot ----------------------------
        {
            std::vector<std::string> quiet, hot;
            for (const auto& t : r.tracks)
            {
                if (! counts (t)) continue;
                if (t.role != Role::fx && t.activity > 0.2 && t.loudness < r.mixLoudness - 24.0) quiet.push_back (t.name);
                if (t.peakBeforeFader > 0.5) hot.push_back (t.name);
            }
            if (! quiet.empty())
                out.push_back ({ "quiet", "info", "speaker", "Tracks you can hardly hear",
                                 joinNames (quiet) + (quiet.size() == 1 ? " plays" : " play") + " more than 24 dB under the mix: in a full mix " + (quiet.size() == 1 ? "it's" : "they're") + " practically gone.",
                                 "Only if they're meant to be heard. Background textures can be that quiet on purpose.",
                                 { "Bring them up until you just notice them, then 1 - 2 dB more, or mute them if the song doesn't miss them." }, quiet });
            if (! hot.empty())
                out.push_back ({ "hot", "info", "headroom", "Very hot tracks before the fader",
                                 joinNames (hot) + (hot.size() == 1 ? " peaks" : " peak") + " above 0 dB before the fader. Cubase doesn't distort inside, but plug-ins and meters behave better with room to spare.",
                                 "Not for the sound. It's good housekeeping.",
                                 { std::string ("Turn the Trim in ") + (hot.size() == 1 ? "its" : "their") + " Mixology Link down about 6 dB and the fader up as much." }, hot });
        }

        // ---- 9. headroom for Masterology --------------------------------------------------------
        if (mixN >= 5)
        {
            if (r.mixPeak > -1.0)
                out.push_back ({ "headroom", "warn", "headroom", "No headroom left for mastering",
                                 "The mix peaks at about " + fmt1 (r.mixPeak) + " dBFS. Masterology (or any mastering) works best with 3 - 6 dB to spare.",
                                 "Yes, before mastering.",
                                 { "Pull all faders down together (or the Stereo Out fader) until the loudest parts peak around -6 to -3 dBFS." }, {} });
            else if (r.mixPeak > -3.0)
                out.push_back ({ "headroom", "info", "headroom", "A bit hot for mastering",
                                 "The mix peaks at about " + fmt1 (r.mixPeak) + " dBFS. Aim for peaks around -6 to -3 dBFS.",
                                 "Only before mastering.", { "Pull all faders down 2 - 3 dB together." }, {} });
        }

        // ---- order: worth fixing first; a calm word when nothing big stands out ------------------
        const bool anyWarn = std::any_of (out.begin(), out.end(), [] (const Suggestion& s) { return s.kind == "warn"; });
        if (! anyWarn && r.secondsHeard >= kEnoughSeconds)
            out.push_back ({ "fine", "calm", "check", "Nothing big stands out",
                             "For " + style.name + ", the balance, low end and stereo picture look healthy.", "No. Trust your ears for the details.", {}, {} });
        auto rank = [] (const Suggestion& s) { return s.kind == "warn" ? 0 : s.kind == "info" ? 1 : 2; };
        std::stable_sort (out.begin(), out.end(), [&] (const Suggestion& a, const Suggestion& b) { return rank (a) < rank (b); });
        return r;
    }
}
