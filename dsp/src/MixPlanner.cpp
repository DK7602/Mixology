#include "mixology/MixPlanner.h"

#include "mixology/ProcChain.h"
#include "mixology/ProcDsp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
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
            std::snprintf (b, sizeof (b), "%.1f", std::abs (v) < 0.05 ? 0.0 : v);
            return b;
        }

        // "4.2 dB over" / "only 0.7 dB under": a level against a reference that is too high for its role, either side of 0.
        std::string overUnder (double v)
        {
            return v >= 0.0 ? fmt1 (v) + " dB over" : "only " + fmt1 (-v) + " dB under";
        }
        std::string fmt0 (double v)
        {
            char b[32];
            std::snprintf (b, sizeof (b), "%.0f", v);
            return b;
        }
        std::string signedDb (double v) { return (v >= 0.05 ? "+" : "") + fmt1 (v) + " dB"; }
        std::string quoted (const std::string& s) { return "'" + s + "'"; }
        std::string panText (double p)
        {
            if (std::abs (p) < 0.5) return "C";
            return (p < 0.0 ? "L" : "R") + fmt0 (std::abs (p));
        }

        std::string joinNames (const std::vector<std::string>& names, std::size_t max = 3)
        {
            std::string out;
            for (std::size_t i = 0; i < names.size() && i < max; ++i)
            {
                if (i > 0) out += i + 1 == std::min (names.size(), max) ? " and " : ", ";
                out += quoted (names[i]);
            }
            if (names.size() > max) out += " and " + std::to_string (names.size() - max) + " more";
            return out;
        }

        std::string joinWords (const std::vector<std::string>& parts, const std::string& sep = "; ")
        {
            std::string out;
            for (const auto& p : parts) out += (out.empty() ? "" : sep) + p;
            return out;
        }

        double rangeE (const std::vector<double>& db, int from, int to)
        {
            double e = 0.0;
            for (int b = from; b <= to && b < static_cast<int> (db.size()); ++b) e += toE (db[static_cast<std::size_t> (b)]);
            return e;
        }

        const char* roleWords (Role r)
        {
            switch (r)
            {
                case Role::kick: return "the kick";
                case Role::snare: return "the snare";
                case Role::hats: return "the hats";
                case Role::perc: return "the percussion";
                case Role::bass: return "the bass / 808";
                case Role::keys: return "the keys";
                case Role::synth: return "the synths";
                case Role::guitar: return "the guitars";
                case Role::vox: return "the lead vocal";
                case Role::adlibs: return "the ad-libs";
                case Role::fx: return "the effects";
                case Role::other: return "the other tracks";
                case Role::beat: return "the beat";
            }
            return "the tracks";
        }

        // Loudness of each kind of instrument against the kick (dB, K-weighted, while it plays), per genre.
        // Starting points from typical mixes of each style: only clear outliers get moved.
        struct Weights
        {
            std::map<Role, double> instruments;
            double adlibs = -8.0;   // ad-libs against the lead vocal
        };

        Weights weightsFor (const std::string& styleKey)
        {
            const auto genre = styleKey.substr (0, styleKey.find ('.'));
            auto make = [] (double kick, double snare, double hats, double perc, double bass, double keys, double synth, double guitar, double adlibs) {
                Weights w;
                w.instruments = { { Role::kick, kick }, { Role::snare, snare }, { Role::hats, hats }, { Role::perc, perc }, { Role::bass, bass },
                                  { Role::keys, keys }, { Role::synth, synth }, { Role::guitar, guitar } };
                w.adlibs = adlibs;
                return w;
            };
            if (genre == "hiphop" || genre == "trap" || genre == "rnb" || genre == "afro" || genre == "latin")
                return make (0, -2, -9, -10, 1, -6, -5, -6, -7);
            if (genre == "pop" || genre == "electronic") return make (0, -1, -9, -10, -1, -5, -4, -5, -8);
            if (genre == "rock") return make (0, 1, -9, -9, -1, -5, -6, 1, -8);
            return make (0, 0, -9, -8, 0, -2, -5, -1, -8);   // country, folk, gospel, worship, jazz, cinematic
        }

        bool counts (const TrackReport& t) { return ! t.excluded && t.activity >= 0.03 && t.loudness > -100.0; }

        struct Lookup
        {
            std::map<std::uint32_t, const ChannelNow*> byHash;
            std::map<std::string, const ChannelNow*> byName;

            const ChannelNow* musicBus = nullptr;

            explicit Lookup (const std::vector<ChannelNow>& chans)
            {
                for (const auto& c : chans)
                {
                    if (c.hash != 0) byHash[c.hash] = &c;
                    byName[c.name] = &c;
                    if (c.musicBus && musicBus == nullptr) musicBus = &c;
                }
            }
            const ChannelNow* of (const TrackReport& t) const
            {
                if (auto it = byHash.find (t.hash); it != byHash.end()) return it->second;
                if (auto it = byName.find (t.name); it != byName.end()) return it->second;
                return nullptr;
            }
            const ChannelNow* named (const std::string& n) const
            {
                auto it = byName.find (n);
                return it != byName.end() ? it->second : nullptr;
            }
        };

        bool movable (const ChannelNow* c) { return c != nullptr && c->faderKnown && ! c->locked && ! c->sharedName && c->faderDb > -100.0; }

        // Two tracks with one name: Mixology can't tell which fader or Link is which, so it moves neither. Said once.
        void noteShared (Plan& plan, const std::string& name)
        {
            const auto note = quoted (name) + ": another track has the same name, so Mixology can't tell which fader and Mixology Link "
                                              "is which and leaves them alone. Rename one of them, then Analyze Mix again.";
            if (std::find (plan.notes.begin(), plan.notes.end(), note) == plan.notes.end()) plan.notes.push_back (note);
        }

        // How far its fader moved since Analyze Mix listened (dB): the measurements are corrected by this.
        double movedSince (const ChannelNow* c)
        {
            if (c == nullptr || ! c->faderKnown || ! c->hasFaderAtAnalysis) return 0.0;
            if (c->faderDb <= -100.0 || c->faderAtAnalysis <= -100.0) return 0.0;
            return c->faderDb - c->faderAtAnalysis;
        }

        // ---------------------------------------------------------------------------------------------
        // Room for Masterology: one gain on the whole mix, the Full Mix's Final Output (Final Touch). It sits after
        // Mixology's own processing (its glue still hears the mix as it was) and before the Limiter and Masterology,
        // so every track, FX return and group goes down together, the balance stays exactly, and it is one change to
        // undo. Moving every fader instead left out the tracks that didn't play while Analyze Mix listened (a chorus
        // while it heard a verse): they came back louder than the rest by the whole amount.

        constexpr double kRoomTargetDb = -6.0;   // Masterology works best with peaks around -6 dBTP (and under -3) ...
        constexpr double kRoomOfferDb = -5.0;    // ... offered once they are hotter than this ...
        constexpr double kRoomAgainDb = -3.0;    // ... and once room was made (Final Output down), only past Masterology's own
                                                 // limit: a mix a dB louder after more mixing isn't worth turning down (and
                                                 // running Auto-Master) again

        // analysisPeak: what Analyze Mix heard coming into Mixology (with the fader moves since). planShift: what this
        // plan's level moves do to the whole mix (dB).
        void planRoom (const Report& r, double analysisPeak, double planShift, const MixNow& mix, Plan& plan)
        {
            // The peak where Final Output sits, before it: the higher of Mixology's own output since play (Final
            // Output's gain taken back out) and what Analyze Mix heard. What Final Output already does counts, so it is
            // never applied twice.
            double peak = -200.0;
            if (r.mixPeak > -100.0) peak = analysisPeak;
            if (mix.known && mix.peakKnown && mix.peakBeforeFinalDb > -100.0) peak = std::max (peak, mix.peakBeforeFinalDb);
            if (peak <= -100.0) return;
            peak += planShift;
            const auto& v = mix.now;
            auto val = [&v] (int id) { return static_cast<double> (v[static_cast<std::size_t> (id)]); };
            const bool finalOn = mix.known && proc::isOn (v, proc::Module::final);
            const double outNow = finalOn ? val (proc::finOut) : 0.0;
            const double into = peak + outNow;   // what reaches Masterology
            const bool madeRoom = finalOn && outNow <= -0.5;
            if (into <= (madeRoom ? kRoomAgainDb : kRoomOfferDb)) return;
            const double need = into - kRoomTargetDb;

            // Only Mixology on the master channel can do it (there it comes before Masterology).
            if (! mix.known || ! mix.onMaster)
            {
                plan.notes.push_back ("Room for Masterology: your mix peaks at about " + signedDb (into) + ". Mixology turns the whole mix down in one go "
                                      "only on the {daw:master} channel (before Masterology), and " + std::string (mix.known ? "this Mixology isn't there" : "no Mixology is there")
                                      + ": put Mixology on the {daw:master} channel, or pull all the track faders down together by about " + fmt0 (need) + " dB.");
                return;
            }
            bool otherModules = false;
            for (int m = 0; m < proc::kModules; ++m)
                if (static_cast<proc::Module> (m) != proc::Module::final && val (proc::onParam (static_cast<proc::Module> (m))) > 0.5) otherModules = true;
            const bool chainOff = val (proc::chainOn) < 0.5;
            if (chainOff && otherModules)
            {
                plan.notes.push_back ("Room for Masterology: your mix peaks at about " + signedDb (into) + ", but Mixology's Full Mix processing is switched off, "
                                      "so its Final Output can't make room. Switch the processing on (Full Mix), or pull all the track faders down together by about "
                                      + fmt0 (need) + " dB.");
                return;
            }

            const auto& spec = proc::specs()[static_cast<std::size_t> (proc::finOut)];
            const double lowest = spec.min;
            double want = std::round ((kRoomTargetDb - peak) * 10.0) / 10.0;
            const bool capped = want < lowest;
            want = std::max (want, lowest);
            const double still = peak + want;   // where the peaks end up
            if (want > outNow - 0.5)
            {
                if (capped)
                    plan.notes.push_back ("Room for Masterology: Mixology's Final Output is at its lowest (" + signedDb (lowest) + ") and the mix still peaks at about "
                                          + signedDb (into) + ". Pull all the track faders down together by about " + fmt0 (into - kRoomTargetDb) + " dB (or turn the hottest tracks down).");
                return;
            }

            Change c;
            c.hash = 0;
            c.name = "Full Mix";
            c.role = Role::other;
            c.kind = "room";
            c.fullMix = true;
            if (chainOff) c.proc.push_back ({ proc::chainOn, 1.0f });   // nothing else is on: only Final Output plays
            if (! finalOn)   // Final Touch on with everything but its Output neutral: nothing else in the sound changes
                for (auto [id, value] : { std::pair { proc::finOn, 1.0f }, std::pair { proc::finWidth, 100.0f }, std::pair { proc::finAir, 0.0f },
                                          std::pair { proc::finGlue, 0.0f } })
                    c.proc.push_back ({ id, value });
            c.proc.push_back ({ proc::finOut, static_cast<float> (want) });
            c.from = outNow;
            c.to = c.wanted = want;
            c.fromText = finalOn ? "Final Output " + signedDb (outNow) : "Final Touch off";
            c.toText = "Final Output " + signedDb (want);
            c.why = "room for Masterology: the whole mix " + signedDb (want - outNow) + " in one go";
            plan.headroomChange = c;
            plan.headroomDb = std::round ((want - outNow) * 10.0) / 10.0;
            plan.headroomWhy = (madeRoom ? "Your mix got louder since you made room: it peaks at about " + signedDb (into) + ", over -3."
                                         : "Your mix peaks at about " + signedDb (into) + " (after Mixology's own processing).")
                               + " Masterology works best with peaks around -6 (and under -3): Mixology's Final Output on the Full Mix goes " + (finalOn ? "from " + signedDb (outNow) + " " : std::string())
                               + "to " + signedDb (want) + ", one gain on the whole mix, so every track, FX return and group goes down together and the balance "
                                 "stays exactly as it is" + (finalOn ? std::string (".") : " (Final Touch switches on; its width, air and glue stay neutral).")
                               + " The mix plays " + fmt1 (outNow - want) + " dB quieter until Masterology brings it back: run Auto-Master in Masterology "
                                 "afterwards (again, if it already ran).";
            if (capped)
                plan.headroomWhy += " Your mix is hotter than Final Output can take: at its lowest (" + signedDb (lowest) + ") the peaks still reach about "
                                    + signedDb (still) + ". Pull all the track faders down together by about " + fmt0 (still - kRoomTargetDb)
                                    + " dB too (or turn the hottest tracks down), then open Balance again.";
        }

        // ---------------------------------------------------------------------------------------------
        // Balance: fader levels

        std::string planBalance (const Report& r, const Lookup& look, const MixNow& mix, Plan& plan)
        {
            struct Member
            {
                std::string name;
                const ChannelNow* ch = nullptr;
                double e = 0.0;                   // its loudness energy (as it is now)
                const TrackReport* track = nullptr;
                double extra = 0.0;               // its own move on top of the group's (a vocal track with your setting)
                std::string why;
            };
            struct Group
            {
                Role role = Role::other;
                bool unheard = false;
                bool viaBus = false;     // reaches the Stereo Out through the music group (moves go to its fader)
                double e = 0.0;          // loudness energy of its tracks together
                double move = 0.0;
                std::vector<Member> members;
                std::vector<std::string> why;
                bool anyMovable() const { return std::any_of (members.begin(), members.end(), [] (const Member& m) { return movable (m.ch); }); }
                double level() const { return toDb (e); }
                bool anyExtra() const { return std::any_of (members.begin(), members.end(), [] (const Member& m) { return m.extra != 0.0; }); }
                // Its energy after moving `by` dB, each member's own move on top.
                double eAfter (double by) const
                {
                    if (! anyExtra()) return e * toE (by);
                    double sum = 0.0;
                    for (const auto& m : members) sum += m.e * toE (by + m.extra);
                    return sum;
                }
            };
            std::map<int, Group> groups;   // by role; 100 = channels without a Link
            std::map<int, double> heardE;  // each group's energy as Analyze Mix heard it (before later fader moves)
            int movedChannels = 0;
            // The music group: the instruments reach the Stereo Out through its fader.
            const ChannelNow* bus = look.musicBus;
            const bool busActive = movable (bus);
            const double busSince = movedSince (bus);
            if (std::abs (busSince) > 0.05) ++movedChannels;
            double busMove = 0.0;
            std::vector<std::string> busWhy;
            for (const auto& t : r.tracks)
            {
                if (! counts (t)) continue;
                auto& g = groups[static_cast<int> (t.role)];
                g.role = t.role;
                g.viaBus = busActive && viaMusicBus (t.role);
                const auto* ch = look.of (t);
                if (ch != nullptr && ch->sharedName) noteShared (plan, t.name);
                const double since = movedSince (ch) + (bus != nullptr && viaMusicBus (t.role) ? busSince : 0.0);
                if (std::abs (since) > 0.05) ++movedChannels;
                g.e += toE (t.loudness + since);
                heardE[static_cast<int> (t.role)] += toE (t.loudness);
                g.members.push_back ({ t.name, ch, toE (t.loudness + since), &t });
            }
            if (r.unheardLoudness > -100.0 && ! r.unheard.empty())
            {
                auto& g = groups[100];
                g.unheard = true;
                double factor = 0.0;
                for (const auto& n : r.unheard)
                {
                    const auto* ch = look.named (n);
                    const double since = movedSince (ch);
                    if (std::abs (since) > 0.05) ++movedChannels;
                    factor += toE (since);
                    g.members.push_back ({ n, ch });
                }
                g.e = toE (r.unheardLoudness) * factor / static_cast<double> (r.unheard.size());
                heardE[100] = toE (r.unheardLoudness);
            }
            if (groups.empty()) return {};
            if (movedChannels > 0)
                plan.notes.push_back (std::to_string (movedChannels) + " fader" + (movedChannels == 1 ? " has" : "s have")
                                      + " moved since Analyze Mix: this plan allows for that. For fresh measurements (after EQ or bigger changes), Analyze Mix again.");

            // The vocal against the rest, and the mix's peak, as they are now (corrected by the fader moves).
            double vocalNow = 0.0, vocalHeard = 0.0, restNow = 0.0, restHeard = 0.0, totalNow = 0.0, totalHeard = 0.0;
            for (auto& [k, g] : groups)
            {
                const double heard = heardE[k];
                totalNow += g.e;
                totalHeard += heard;
                if (k == static_cast<int> (Role::vox)) { vocalNow += g.e; vocalHeard += heard; }
                else if (k != static_cast<int> (Role::adlibs)) { restNow += g.e; restHeard += heard; }
            }
            const double vocalRatio = r.vocalRatio + (toDb (vocalNow) - toDb (vocalHeard)) - (toDb (restNow) - toDb (restHeard));
            const double mixPeak = r.mixPeak + (toDb (totalNow) - toDb (totalHeard));

            auto eff = [&] (const Group& g) { return g.move + (g.viaBus ? busMove : 0.0); };   // its whole move, bus included
            const auto w = weightsFor (r.style.key);
            auto isInstrument = [&] (const Group& g) { return ! g.unheard && w.instruments.count (g.role) != 0; };
            auto isRest = [&] (const Group& g) { return g.unheard || (g.role != Role::vox && g.role != Role::adlibs); };

            // A full drum kit on one track (no separate kick, snare or hats) is the drums: it weighs like a kick.
            auto weightOf = [&] (Role role) {
                if (role == Role::perc && ! groups.count (static_cast<int> (Role::kick)) && ! groups.count (static_cast<int> (Role::snare))
                    && ! groups.count (static_cast<int> (Role::hats)))
                    return 0.0;
                return w.instruments.at (role);
            };
            auto kindWords = [] (const Group& g) {
                return g.unheard ? std::string ("the channels without a Link") : std::string (roleWords (g.role));
            };

            // 1. Instruments against each other: only kinds clearly off the style's balance move (to within 1 dB).
            //    Not when a big part of the band plays on channels without a Link (a printed beat): Mixology can't
            //    hear those one by one, so it can't tell which instrument is too loud.
            double linkedInstruments = 0.0, beatE = 0.0;
            for (auto& [k, g] : groups)
            {
                if (isInstrument (g)) linkedInstruments += g.e;
                if (! g.unheard && g.role == Role::beat) beatE += g.e;   // a whole beat on one track: it is the music
            }
            const bool bigUnheard = groups.count (100) && groups[100].e > 0.25 * (linkedInstruments + beatE);
            if (bigUnheard)
                plan.notes.push_back (joinNames (r.unheard) + (r.unheard.size() == 1 ? " has" : " have")
                                      + " no Mixology Link and carr" + (r.unheard.size() == 1 ? "ies" : "y") + " a big part of the band, so Mixology doesn't judge "
                                        "the instruments against each other (only the vocal against everything). Insert Mixology Link there to change that.");
            // A whole beat on one track carries the music: Mixology can't hear its parts, so the instruments on top of it
            // aren't judged against each other either (the vocal still is, against all of it).
            const bool beatCarries = ! bigUnheard && beatE > 0.0 && linkedInstruments > 0.0 && beatE > 0.25 * linkedInstruments;
            if (beatCarries)
            {
                std::vector<std::string> beats;
                for (const auto& t : r.tracks) if (counts (t) && t.role == Role::beat) beats.push_back (t.name);
                plan.notes.push_back (joinNames (beats) + (beats.size() == 1 ? " is a whole beat" : " are whole beats")
                                      + " on one track: Mixology balances the vocal against the music, but doesn't judge the instruments on top of it "
                                        "against each other (it can't hear the beat's own kick, snare or 808).");
            }
            if (! bigUnheard && ! beatCarries)
            {
                std::vector<double> offsets, locked;
                for (auto& [k, g] : groups)
                    if (isInstrument (g))
                    {
                        offsets.push_back (g.level() - weightOf (g.role));
                        if (! g.anyMovable()) locked.push_back (offsets.back());
                    }
                if (offsets.size() >= 2)
                {
                    auto& ref = locked.empty() ? offsets : locked;
                    std::sort (ref.begin(), ref.end());
                    const double c = ref.size() % 2 ? ref[ref.size() / 2] : 0.5 * (ref[ref.size() / 2 - 1] + ref[ref.size() / 2]);
                    for (auto& [k, g] : groups)
                    {
                        if (! isInstrument (g)) continue;
                        const double dev = g.level() - weightOf (g.role) - c;
                        if (std::abs (dev) <= 2.5) continue;
                        g.move -= dev - std::copysign (1.0, dev);
                        g.why.push_back (kindWords (g) + (dev > 0.0 ? " sit " + fmt1 (dev) + " dB louder" : " sit " + fmt1 (-dev) + " dB quieter")
                                         + " against the rest of the band than " + r.style.name + " usually has");
                    }
                }
            }

            // 2. The lead vocal against the rest of the mix (as measured while it sings), towards the style's level or
            //    the one you set, in one go (so opening the plan again after Apply finds nothing more to do).
            Group* vocal = groups.count (static_cast<int> (Role::vox)) ? &groups[static_cast<int> (Role::vox)] : nullptr;
            Group* adlibs = groups.count (static_cast<int> (Role::adlibs)) ? &groups[static_cast<int> (Role::adlibs)] : nullptr;
            if (vocal != nullptr && r.hasVocalRatio)
            {
                double restOld = 0.0, restNew = 0.0;
                bool restMovable = busActive;
                for (auto& [k, g] : groups)
                    if (isRest (g))
                    {
                        restOld += g.e;
                        restNew += g.e * toE (eff (g));
                        restMovable = restMovable || g.anyMovable();
                    }
                const double ratio = vocalRatio - (toDb (restNew) - toDb (restOld));
                double fullErr = r.vocalTarget - ratio;

                // Your ears per vocal track (a verse and a hook on their own tracks): each one moves to its own aim,
                // the band makes room for the average.
                std::vector<std::pair<Member*, double>> each;   // a vocal track and how far it is from its aim
                bool perTrack = false;
                for (auto& m : vocal->members) perTrack = perTrack || (m.track != nullptr && m.track->vocalOwn);
                double worst = 0.0;
                if (perTrack)
                {
                    const double restShift = (toDb (restNow) - toDb (restHeard)) + (toDb (restNew) - toDb (restOld));
                    double sum = 0.0;
                    for (auto& m : vocal->members)
                        if (m.track != nullptr && m.track->hasVocalRatio)
                        {
                            const double now = m.track->vocalRatio + movedSince (m.ch) - restShift;
                            each.push_back ({ &m, m.track->vocalTarget - now });
                            sum += each.back().second;
                            worst = std::max (worst, std::abs (each.back().second));
                        }
                    if (each.empty()) perTrack = false;
                    else fullErr = sum / static_cast<double> (each.size());
                }
                const double err = fullErr;
                if (perTrack ? worst > 1.5 : std::abs (fullErr) > 1.5)
                {
                    const bool vocalMovable = vocal->anyMovable();
                    double vm = 0.0, rm = 0.0;
                    if (vocalMovable && restMovable) { vm = err / 2.0; rm = -err / 2.0; }
                    else if (vocalMovable) vm = err;
                    else if (restMovable) rm = -err;
                    else plan.notes.push_back ("The vocal sits " + signedDb (ratio) + " against the rest (the aim is " + signedDb (r.vocalTarget)
                                               + "), but the channels that would move are protected or weren't read.");
                    // A vocal fader can't go past the top (+6 dB): the band takes what is left.
                    if (vm > 0.0)
                    {
                        double room = 1.0e9;
                        for (const auto& m : vocal->members)
                            if (movable (m.ch)) room = std::min (room, kMaxFaderDb - m.ch->faderDb - vocal->move);
                        if (vm > room)
                        {
                            if (restMovable) rm -= vm - std::max (0.0, room);
                            vm = std::max (0.0, room);
                        }
                    }
                    const std::string aim = r.style.vocalFromYou ? "you set about " + signedDb (r.vocalTarget)
                                                                 : r.style.name + " usually has about " + signedDb (r.vocalTarget);
                    const std::string where = (ratio < r.vocalTarget ? "sits low: " : "sits high: ") + signedDb (ratio)
                                              + " against the rest while it sings, " + aim;
                    if (std::abs (fullErr) > 6.0 && ! r.style.vocalFromYou)
                        plan.notes.push_back ("The vocal is " + fmt1 (std::abs (fullErr)) + " dB from " + r.style.name
                                              + "'s usual level. If it sounds right to you, press 'Right' above instead: Mixology keeps it where it is.");
                    if (perTrack && vocalMovable)
                    {
                        // Each track's own part of the move: from the average to its own aim.
                        for (auto& [m, e] : each)
                        {
                            m->extra = e - err;
                            const double now = m->track->vocalTarget - e;
                            m->why = (std::abs (e) <= 1.5 ? "'" + m->name + "' is where you like it: " + signedDb (now)
                                      : "'" + m->name + (e > 0.0 ? "' sits low: " : "' sits high: ") + signedDb (now))
                                     + " against the rest while it sings, " + (m->track->vocalOwn ? "you set about " + signedDb (m->track->vocalTarget) + " for it"
                                                                              : r.style.vocalFromYou ? "you set about " + signedDb (m->track->vocalTarget)
                                                                              : r.style.name + " usually has about " + signedDb (m->track->vocalTarget));
                        }
                    }
                    if (vm != 0.0 || (perTrack && vocalMovable))
                    {
                        vocal->move += vm;
                        if (! perTrack) vocal->why.push_back ("the vocal " + where);
                        if (adlibs != nullptr && vm != 0.0) { adlibs->move += vm; adlibs->why.push_back ("follows the lead vocal"); }
                    }
                    if (rm != 0.0)
                    {
                        const std::string reason = rm < 0.0 ? "makes room for the vocal" : "the vocal sits too far on top";
                        if (busActive) { busMove += rm; busWhy.push_back (reason); }
                        for (auto& [k, g] : groups)
                            if (isRest (g) && ! g.viaBus)
                            {
                                g.move += rm;
                                g.why.push_back (reason);
                            }
                    }
                }
            }
            if (vocal != nullptr && adlibs != nullptr)
            {
                const double dev = (adlibs->level() + adlibs->move) - (vocal->level() + vocal->move + w.adlibs);
                if (std::abs (dev) > 3.0)
                {
                    adlibs->move -= dev - std::copysign (1.5, dev);
                    adlibs->why.push_back (dev > 0.0 ? "ad-libs compete with the lead vocal" : "ad-libs are hard to hear under the lead vocal");
                }
            }

            // 3. The same overall loudness as before (louder always sounds "better", so a fair before / after needs this).
            //    Only when the difference would be heard (1 dB or more), and not around protected channels (moving
            //    everything else back would undo what was asked of them).
            //    With a music group, its fader carries the instruments' share.
            {
                double total = 0.0, after = 0.0;
                bool anyLocked = false;
                for (auto& [k, g] : groups)
                {
                    total += g.e;
                    after += g.eAfter (eff (g));
                    if (! g.viaBus) anyLocked = anyLocked || ! g.anyMovable();
                }
                double c = std::clamp (toDb (total) - toDb (after), -6.0, 6.0);
                if (c > 0.0)   // never past the top of a fader (that would undo the balance)
                {
                    for (auto& [k, g] : groups)
                        if (! g.viaBus)
                            for (const auto& m : g.members)
                                if (movable (m.ch)) c = std::min (c, std::max (0.0, kMaxFaderDb - m.ch->faderDb - g.move - m.extra));
                    if (busActive) c = std::min (c, std::max (0.0, kMaxFaderDb - bus->faderDb - busMove));
                }
                if (! anyLocked && std::abs (c) >= 1.0)
                {
                    for (auto& [k, g] : groups) if (! g.viaBus) g.move += c;
                    if (busActive) busMove += c;
                }
            }

            // 4. Room for Masterology: offered separately (you tick it). One gain on the whole mix (planRoom), worked out
            //    with this plan's level moves in.
            {
                double before = 0.0, after = 0.0;
                for (auto& [k, g] : groups) { before += g.e; after += g.eAfter (eff (g)); }
                planRoom (r, mixPeak, toDb (after) - toDb (before), mix, plan);
            }

            // Changes, track by track (a kind's tracks all move the same).
            std::vector<std::pair<double, std::string>> moved;
            for (auto& [k, g] : groups)
            {
                if (std::abs (g.move) < 0.5 && ! g.anyExtra()) continue;
                if (! g.anyExtra()) moved.push_back ({ g.move, kindWords (g) });
                for (const auto& m : g.members)
                {
                    const double move = g.move + m.extra;
                    if (std::abs (move) < 0.5) continue;
                    if (g.anyExtra()) moved.push_back ({ move, quoted (m.name) });
                    if (! movable (m.ch))
                    {
                        if (m.ch != nullptr && m.ch->sharedName) continue;   // said above
                        plan.notes.push_back (quoted (m.name) + (m.ch == nullptr ? ": {daw} hasn't shown its fader (press Analyze Mix again)"
                                                                 : m.ch->locked ? ": protected (padlock)" : ": its fader wasn't read")
                                              + ", so it stays where it is (it would have moved " + signedDb (move) + ").");
                        continue;
                    }
                    Change c;
                    c.hash = m.ch->hash;
                    c.name = m.name;
                    c.role = g.role;
                    c.kind = "level";
                    c.from = m.ch->faderDb;
                    c.wanted = std::min (c.from + move, kMaxFaderDb);
                    c.to = std::min (c.from + std::clamp (move, -kMaxMoveDb, kMaxMoveDb), kMaxFaderDb);
                    c.big = std::abs (move) > kMaxMoveDb + 0.05;
                    auto whys = g.why;
                    if (! m.why.empty()) whys.insert (whys.begin(), m.why);
                    c.why = joinWords (whys);
                    if (c.why.empty()) c.why = "keeps the mix as loud as before";
                    if (std::abs (c.to - c.from) < 0.05)
                    {
                        plan.notes.push_back (quoted (m.name) + " is already at the top of its fader (+6 dB): raise it with Mixology Link's Trim instead.");
                        continue;
                    }
                    plan.changes.push_back (c);
                }
            }
            if (busActive && std::abs (busMove) >= 0.5)
            {
                moved.push_back ({ busMove, "the music group" });
                Change c;
                c.hash = bus->hash;
                c.name = bus->name;
                c.role = Role::other;
                c.kind = "level";
                c.from = bus->faderDb;
                c.wanted = std::min (c.from + busMove, kMaxFaderDb);
                c.to = std::min (c.from + std::clamp (busMove, -kMaxMoveDb, kMaxMoveDb), kMaxFaderDb);
                c.big = std::abs (busMove) > kMaxMoveDb + 0.05;
                c.why = "the music group (everything but the vocals plays through it)" + (busWhy.empty() ? std::string (": keeps the mix as loud as before") : ": " + joinWords (busWhy));
                if (std::abs (c.to - c.from) >= 0.05) plan.changes.push_back (c);
                else plan.notes.push_back (quoted (bus->name) + " (the music group) is already at the top of its fader (+6 dB).");
            }
            else if (bus != nullptr && ! busActive)
                plan.notes.push_back (quoted (bus->name) + " (the music group) is protected or its fader wasn't read: the instruments move one by one instead.");
            if (moved.empty()) return "levels already fit " + (r.style.vocalFromYou ? std::string ("your setting") : r.style.name);
            std::sort (moved.begin(), moved.end(), [] (auto& a, auto& b) { return std::abs (a.first) > std::abs (b.first); });
            std::vector<std::string> parts;
            for (std::size_t i = 0; i < moved.size() && i < 4; ++i) parts.push_back (moved[i].second + " " + signedDb (moved[i].first));
            return "levels: " + joinWords (parts, ", ") + " (same overall loudness)";
        }

        // ---------------------------------------------------------------------------------------------
        // Stereo Architect: pans

        double panBalanceDb (double pan)   // left minus right of a mono source through an equal-power panner
        {
            const double th = (std::clamp (pan, -100.0, 100.0) + 100.0) / 200.0 * 1.5707963267948966;
            const double l = std::cos (th), rr = std::sin (th);
            if (rr < 1.0e-6) return 40.0;
            if (l < 1.0e-6) return -40.0;
            return std::clamp (20.0 * std::log10 (l / rr), -40.0, 40.0);
        }

        double spreadFor (Role r)
        {
            switch (r)
            {
                case Role::guitar: return 85.0;
                case Role::synth: return 60.0;
                case Role::adlibs: return 50.0;
                case Role::keys: return 45.0;
                case Role::perc: return 40.0;
                case Role::vox: return 35.0;   // backing doubles of the lead
                default: return 50.0;
            }
        }

        // ---- name pairs: a double only by ear (Report::pairs) ----------------------------------------------
        // The names say who MIGHT pair ("Vox Chorus 1" / "Vox Chorus 2", "Gtr" / "Gtr (D)"); a pair is a double (spread
        // left / right) only when Analyze Mix heard both play at the same time with their levels moving together. Two
        // parts of a chorus, a call and its response, a lead and its ad-libs are not. Sides you typed (L / R, Left /
        // Right) count by name.
        bool sidesByName (const PairName& a, const PairName& b) { return a.side != 0 && b.side != 0 && a.side != b.side; }

        bool playsAsDouble (const Report& r, const TrackReport& a, const TrackReport& b)
        {
            const auto pa = pairName (a.name), pb = pairName (b.name);
            if (pa.base.empty() || pa.base != pb.base) return false;
            if (sidesByName (pa, pb)) return true;
            const auto* c = pairCheck (r, a.hash, b.hash);
            return c != nullptr && c->verdict == PairCheck::Verdict::together;
        }

        // A double of another track (not the original take): a side in its name, or heard as the later take of a double.
        bool doubleTake (const Report& r, const TrackReport& t)
        {
            const auto pt = pairName (t.name);
            if (pt.side != 0) return true;
            for (const auto& o : r.tracks)
            {
                if (o.hash == t.hash || o.role != t.role || ! counts (o) || ! playsAsDouble (r, t, o)) continue;
                const auto po = pairName (o.name);
                if (pt.order > po.order || (pt.order == po.order && pt.explicitPair && ! po.explicitPair)) return true;
            }
            return false;
        }

        // A vocal part whose name partners (counted, same role) all sing at other times: a lead part of its own.
        bool takesTurns (const Report& r, const TrackReport& t)
        {
            const auto pt = pairName (t.name);
            bool any = false;
            for (const auto& o : r.tracks)
            {
                if (o.hash == t.hash || o.role != t.role || ! counts (o) || pairName (o.name).base != pt.base) continue;
                const auto* c = pairCheck (r, t.hash, o.hash);
                if (c == nullptr || c->verdict != PairCheck::Verdict::apart) return false;
                any = true;
            }
            return any;
        }

        // Why a name pair stays as it is, in a few words (for the plan's notes).
        std::string pairNote (const PairCheck* c, const std::string& a, const std::string& b, Role role)
        {
            const bool vocal = role == Role::vox || role == Role::adlibs;
            const std::string verb = vocal ? "sing" : "play";
            const auto pa = pairName (a), pb = pairName (b);   // in their order: "... 1" before "... 2", the original first
            const bool swap = pa.order != pb.order ? pa.order > pb.order : pa.side != pb.side ? pa.side > pb.side : b < a;
            const std::string both = quoted (swap ? b : a) + " and " + quoted (swap ? a : b) + " look like a pair by name";
            using V = PairCheck::Verdict;
            if (c != nullptr && c->verdict == V::apart)
                return both + ", but they " + verb + " at different times (different parts), so their pans stay.";
            if (c != nullptr && c->verdict == V::unlike)
                return both + " and " + verb + " at the same time, but not the same " + (vocal ? "line" : "part")
                       + " (their levels don't move together), so their pans stay.";
            return both + ", but Mixology didn't hear them " + verb + " together: play the part where both " + verb
                   + " and Analyze Mix again (their pans stay until then).";
        }

        std::string planStereo (const Report& r, const Lookup& look, Plan& plan)
        {
            struct T
            {
                const TrackReport* t = nullptr;
                const ChannelNow* ch = nullptr;
                double pan = 0.0, to = 0.0;
                bool fixed = false;   // already decided by a rule (or not ours to move)
                std::string why;
            };
            std::vector<T> ts;
            std::vector<std::string> stereoPanners, stereoTracks;
            for (const auto& t : r.tracks)
            {
                if (! counts (t)) continue;
                const auto* ch = look.of (t);
                if (ch == nullptr || ! ch->hasPan) continue;
                if (ch->panUnknown)
                {
                    // Counted as centred (it plays its own stereo image), never moved.
                    ts.push_back ({ &t, ch, 0.0, 0.0, true, {} });
                    stereoPanners.push_back (t.name);
                    continue;
                }
                if (ch->channels != 1)
                {
                    // Stereo (or unknown) track: Cubase may use the Combined Panner, whose position Mixology can't
                    // read; it is counted where Cubase says, never moved.
                    ts.push_back ({ &t, ch, ch->pan, ch->pan, true, {} });
                    stereoTracks.push_back (t.name);
                    continue;
                }
                ts.push_back ({ &t, ch, ch->pan, ch->pan, false, {} });
            }
            if (ts.empty()) return {};
            if (r.monoMix)
            {
                plan.notes.push_back ("Your mix is mono at the {daw:master} channel (left and right are the same), so panning changes nothing: "
                                      "a mono group or output is on the way. Stereo Architect leaves the pans alone (see 'Your mix is mono' in the Mix Assistant).");
                return "pans have no effect (mono mix)";
            }
            if (! stereoPanners.empty())
                plan.notes.push_back (joinNames (stereoPanners) + (stereoPanners.size() == 1 ? " uses" : " use")
                                      + " Cubase's Stereo Combined Panner (Cubase only tells Mixology its left edge): pan "
                                      + (stereoPanners.size() == 1 ? "it" : "them") + " in Cubase if needed.");
            if (! stereoTracks.empty())
                plan.notes.push_back (joinNames (stereoTracks) + (stereoTracks.size() == 1 ? " is a stereo track" : " are stereo tracks")
                                      + ": Mixology only pans mono tracks ({daw} doesn't tell it where a stereo panner really sits).");

            // The pans Cubase reports must agree with what the Stereo Out hears. A stereo track with Cubase's Combined
            // Panner reports one side of it (e.g. "L100" for a full-width track): then the numbers can't be trusted.
            {
                double l = 0.0, rgt = 0.0;
                for (const auto& x : ts)
                {
                    const double e = toE (x.t->loudness) * std::max (0.05, x.t->activity);
                    const double q = toE (std::clamp (x.t->balance + panBalanceDb (x.pan), -40.0, 40.0));
                    l += 2.0 * e * q / (1.0 + q);
                    rgt += 2.0 * e / (1.0 + q);
                }
                if (r.unheardLoudness > -100.0) { l += toE (r.unheardLoudness); rgt += toE (r.unheardLoudness); }
                const double expected = toDb (l) - toDb (rgt);
                if (std::abs (expected - r.mixBalance) > 3.0)
                {
                    std::vector<std::string> hard;
                    for (const auto& x : ts) if (std::abs (x.pan) >= 60.0) hard.push_back (x.t->name);
                    plan.notes.push_back ("Cubase shows " + (hard.empty() ? std::string ("some tracks") : joinNames (hard)) + " panned well off-centre, but your mix "
                                          + (std::abs (r.mixBalance) < 1.5 ? std::string ("doesn't lean") : "leans " + fmt1 (std::abs (r.mixBalance)) + " dB")
                                          + ": they're probably stereo tracks with Cubase's Combined Panner, where Mixology only sees one side. "
                                            "Stereo Architect leaves the pans alone: check those panners in Cubase.");
                    return "pans left alone";
                }
            }

            // 1. Low end and the lead vocal in the middle.
            for (auto& x : ts)
            {
                if (x.ch->panUnknown || x.ch->channels != 1) continue;
                const auto role = x.t->role;
                const auto pn = pairName (x.t->name);
                // The lead vocal: the original take, or a part that sings while its name partners don't ("Vox Chorus 2"
                // after "Vox Chorus 1"); never a double heard as one.
                const bool lead = role == Role::vox && ! doubleTake (r, *x.t) && ((! pn.explicitPair && pn.order == 0) || takesTurns (r, *x.t));
                const double limit = role == Role::kick || role == Role::bass || role == Role::beat ? 5.0 : role == Role::snare ? 25.0 : lead ? 15.0 : 1.0e9;
                if (std::abs (x.pan) > limit)
                {
                    x.to = 0.0;
                    x.why = role == Role::vox ? (pn.order > 0 || pn.explicitPair ? "a lead vocal part (its name partners sing at other times): the centre of the song"
                                                                                 : "the lead vocal is the centre of the song")
                                              : std::string (roleWords (role)) + " belong" + (role == Role::snare || role == Role::beat ? "s" : "") + " in the middle: stronger on every speaker and in mono";
                }
                if (role == Role::kick || role == Role::bass || role == Role::snare || role == Role::beat || lead) x.fixed = true;
            }

            // 2. Doubles: spread left / right. A name pair only when Analyze Mix heard both play at the same time, moving
            //    together (or the names say left and right); the others keep their pans, and a note says why.
            std::map<std::string, std::vector<T*>> pairs;
            for (auto& x : ts)
            {
                if (x.fixed) continue;
                const auto pn = pairName (x.t->name);
                if (pn.base.empty()) continue;
                const auto role = x.t->role;
                if (role == Role::kick || role == Role::bass || role == Role::snare || role == Role::fx || role == Role::beat) continue;
                pairs[pn.base + "|" + roleKey (role)].push_back (&x);
            }
            std::vector<std::string> pairNotes;
            int morePairs = 0;
            auto addPairNote = [&] (const std::string& note) { if (pairNotes.size() < 3) pairNotes.push_back (note); else ++morePairs; };
            auto mayPair = [] (Role role) { return role != Role::kick && role != Role::bass && role != Role::snare && role != Role::fx && role != Role::beat; };
            auto leadTake = [] (Role role, const PairName& pn) { return role == Role::vox && ! pn.explicitPair && pn.order == 0; };
            // Name pairs that don't play as doubles keep their pans: one note each (the original lead take stays in the
            // middle anyway, so a pair with it says nothing).
            for (std::size_t i = 0; i < ts.size(); ++i)
                for (std::size_t j = i + 1; j < ts.size(); ++j)
                {
                    const auto& x = ts[i];
                    const auto& y = ts[j];
                    if (x.ch->panUnknown || x.ch->channels != 1 || y.ch->panUnknown || y.ch->channels != 1) continue;
                    const auto role = x.t->role;
                    const auto px = pairName (x.t->name), py = pairName (y.t->name);
                    if (y.t->role != role || ! mayPair (role) || px.base.empty() || px.base != py.base) continue;
                    if (leadTake (role, px) || leadTake (role, py)) continue;
                    if (role == Role::other && ! px.explicitPair && ! py.explicitPair) continue;   // "Audio 01", "Audio 02"
                    if (playsAsDouble (r, *x.t, *y.t)) continue;
                    if (std::abs (x.to - x.pan) >= 3.0 || std::abs (y.to - y.pan) >= 3.0) continue;   // a lead part centred: its change says why
                    addPairNote (pairNote (pairCheck (r, x.t->hash, y.t->hash), x.t->name, y.t->name, role));
                }
            for (const auto& c : r.pairs)   // a partner that didn't play while Analyze Mix listened (a chorus while it heard a verse)
            {
                const auto ia = std::find_if (r.tracks.begin(), r.tracks.end(), [&c] (const TrackReport& t) { return t.hash == c.a; });
                const auto ib = std::find_if (r.tracks.begin(), r.tracks.end(), [&c] (const TrackReport& t) { return t.hash == c.b; });
                if (ia == r.tracks.end() || ib == r.tracks.end() || ia->role != ib->role || counts (*ia) == counts (*ib)) continue;
                const auto role = ia->role;
                const auto pa = pairName (ia->name), pb = pairName (ib->name);
                if (! mayPair (role) || leadTake (role, pa) || leadTake (role, pb) || sidesByName (pa, pb)) continue;
                if (role == Role::other && ! pa.explicitPair && ! pb.explicitPair) continue;
                const auto* heard = look.of (counts (*ia) ? *ia : *ib);
                if (heard == nullptr || ! heard->hasPan || heard->channels != 1) continue;   // its pan isn't Mixology's to move anyway
                addPairNote (pairNote (&c, c.nameA, c.nameB, role));
            }
            for (auto& [key, members] : pairs)
            {
                if (members.size() < 2) continue;
                const auto role = members.front()->t->role;
                bool anyExplicit = false;
                for (auto* m : members) anyExplicit = anyExplicit || pairName (m->t->name).explicitPair;
                if (role == Role::other && ! anyExplicit) continue;   // "Audio 01", "Audio 02": not a pair
                // Only the ones that play as doubles (by ear, or by their sides).
                std::vector<T*> spread;
                for (auto* m : members)
                    if (std::any_of (members.begin(), members.end(), [&] (T* o) { return o != m && playsAsDouble (r, *m->t, *o->t); }))
                        spread.push_back (m);
                if (role == Role::vox)
                {
                    // The original take is the lead (centre); spread its doubles only when there are two or more.
                    spread.erase (std::remove_if (spread.begin(), spread.end(), [] (T* m) { const auto pn = pairName (m->t->name); return ! pn.explicitPair && pn.order == 0; }),
                                  spread.end());
                }
                if (spread.size() < 2) continue;
                std::sort (spread.begin(), spread.end(), [] (T* a, T* b) {
                    const auto pa = pairName (a->t->name), pb = pairName (b->t->name);
                    return pa.side != pb.side ? pa.side < pb.side : pa.order < pb.order;
                });
                const double s = spreadFor (role);
                const auto n = spread.size();
                double lo = 1.0e9, hi = -1.0e9;
                for (auto* m : spread) { lo = std::min (lo, m->pan); hi = std::max (hi, m->pan); }
                const bool alreadySpread = n == 2 ? (lo <= -20.0 && hi >= 20.0) : (hi - lo >= s);
                if (alreadySpread) continue;
                // A pair with one side already well out (e.g. both hard left): that one stays, the other mirrors it.
                T* anchor = nullptr;
                if (n == 2)
                {
                    auto* a = std::abs (spread[0]->pan) >= std::abs (spread[1]->pan) ? spread[0] : spread[1];
                    if (std::abs (a->pan) >= 0.6 * s) anchor = a;
                }
                for (std::size_t i = 0; i < n; ++i)
                {
                    auto* m = spread[i];
                    if (anchor != nullptr) m->to = m == anchor ? m->pan : -anchor->pan;
                    else m->to = -s + 2.0 * s * static_cast<double> (i) / static_cast<double> (n - 1);
                    m->fixed = true;
                    std::vector<std::string> names;
                    for (auto* o : spread) if (o != m) names.push_back (o->t->name);
                    bool byName = true;
                    for (auto* o : spread) if (o != m) byName = byName && sidesByName (pairName (m->t->name), pairName (o->t->name));
                    m->why = "pairs with " + joinNames (names) + (byName ? " (left and right in their names)" : " (a double: Mixology heard them play together)")
                             + ": one left, one right sounds wide and keeps the middle free for the vocal";
                }
            }
            for (const auto& n : pairNotes) plan.notes.push_back (n);
            if (morePairs > 0)
                plan.notes.push_back (std::to_string (morePairs) + " more name pair" + (morePairs == 1 ? "" : "s")
                                      + " keep their pans the same way (not heard as doubles).");

            // 3. A lopsided mix: even it out with the loudest tracks pulling it to one side.
            auto predict = [&] () {
                double l = 0.0, rgt = 0.0;
                for (const auto& x : ts)
                {
                    const double e = toE (x.t->loudness) * std::max (0.05, x.t->activity);
                    // The Link sits before the panner (an insert): what it hears plus where the pan puts it.
                    const double bal = std::clamp (x.t->balance + panBalanceDb (x.to), -40.0, 40.0);
                    const double q = toE (bal);
                    l += 2.0 * e * q / (1.0 + q);
                    rgt += 2.0 * e / (1.0 + q);
                }
                if (r.unheardLoudness > -100.0) { l += toE (r.unheardLoudness); rgt += toE (r.unheardLoudness); }
                return toDb (l) - toDb (rgt);
            };
            const double bias = r.mixBalance - predict();   // what the estimate misses (stereo stems, effects...)
            for (int round = 0; round < 4; ++round)
            {
                const double lean = predict() + bias;
                if (std::abs (lean) <= 1.5) break;
                T* best = nullptr;
                double bestLean = std::abs (lean), bestTo = 0.0;
                for (auto& x : ts)
                {
                    if (x.fixed || (lean > 0.0 ? x.pan > -30.0 : x.pan < 30.0)) continue;
                    for (double option : { -x.pan, x.pan * 0.5, 0.0 })
                    {
                        const double keep = x.to;
                        x.to = option;
                        const double l2 = std::abs (predict() + bias);
                        x.to = keep;
                        if (l2 < bestLean - 0.3) { best = &x; bestLean = l2; bestTo = option; }
                    }
                }
                if (best == nullptr)
                {
                    plan.notes.push_back ("The mix leans " + std::string (lean > 0.0 ? "left" : "right") + " (" + fmt1 (std::abs (lean))
                                          + " dB) from tracks Mixology can't pan (stereo stems or channels without a Link): check those in {daw}.");
                    break;
                }
                best->to = bestTo;
                best->fixed = true;
                best->why = "the mix leans " + std::string (lean > 0.0 ? "left" : "right") + " by " + fmt1 (std::abs (lean)) + " dB: this evens it out";
            }

            int count = 0;
            for (const auto& x : ts)
            {
                const double to = std::round (x.to);
                if (x.ch->panUnknown || x.ch->channels != 1 || std::abs (to - x.pan) < 3.0) continue;
                if (x.ch->locked)
                {
                    plan.notes.push_back (quoted (x.t->name) + ": protected (padlock), so its pan stays " + panText (x.pan) + " (it would have gone " + panText (to) + ").");
                    continue;
                }
                if (x.ch->sharedName)
                {
                    noteShared (plan, x.t->name);
                    continue;
                }
                Change c;
                c.hash = x.ch->hash;
                c.name = x.t->name;
                c.role = x.t->role;
                c.kind = "pan";
                c.from = x.pan;
                c.to = c.wanted = to;
                c.why = x.why;
                plan.changes.push_back (c);
                ++count;
            }
            if (count == 0) return "pans already work";
            return "pans: " + std::to_string (count) + " track" + (count == 1 ? "" : "s");
        }

        // Polish's helpers (below), which Clean Up and Vocal Pocket use to count what a track's EQ already does.
        enum class Job { lowCut, low, mud, mid, presence, air };
        float pv (const proc::Values& v, int id);
        const proc::Values& atAnalysis (const ChannelNow& ch);
        int shapeOf (const proc::Values& v, int b);
        double eqSinceDb (const proc::Values& now, const proc::Values& at, Job job, double hz, int skipBand = -1);
        double eqCutSince (const proc::Values& now, const proc::Values& at, Job job);

        // ---------------------------------------------------------------------------------------------
        // Clean Up: low cuts and mud dips (Assist EQ)

        double lowCutFor (Role r)
        {
            switch (r)
            {
                case Role::vox: return 90.0;
                case Role::adlibs: return 100.0;
                case Role::hats: return 250.0;
                case Role::perc: return 120.0;
                case Role::snare: return 70.0;
                case Role::guitar: return 80.0;
                case Role::keys: return 50.0;
                case Role::synth: return 50.0;
                default: return 60.0;
            }
        }

        const ChannelNow* eqChannel (const TrackReport& t, const Lookup& look, Plan& plan, const char* what)
        {
            const auto* ch = look.of (t);
            if (ch != nullptr && ch->sharedName)
            {
                noteShared (plan, t.name);
                return nullptr;
            }
            if (ch == nullptr || ! ch->hasLink)
            {
                plan.notes.push_back (quoted (t.name) + " has no Mixology Link Mixology can reach: " + what + " skipped.");
                return nullptr;
            }
            if (ch->locked)
            {
                plan.notes.push_back (quoted (t.name) + " is protected (padlock): " + what + " skipped.");
                return nullptr;
            }
            return ch;
        }

        std::string planCleanUp (const Report& r, const Lookup& look, Plan& plan)
        {
            int cuts = 0, dips = 0;
            for (const auto& t : r.tracks)
            {
                if (! counts (t) || t.role == Role::kick || t.role == Role::bass || t.role == Role::fx || t.role == Role::beat) continue;
                const double all = rangeE (t.bands, 0, kBands - 1);
                if (all <= 0.0) continue;
                const double share = rangeE (t.bands, 0, lowCutTopBand (t.role)) / all;
                const bool vocal = t.role == Role::vox || t.role == Role::adlibs;
                if (! t.rumble && ! (vocal && share >= 0.03)) continue;
                const double hz = lowCutFor (t.role);
                const auto* ch = eqChannel (t, look, plan, "low cut");
                if (ch == nullptr) continue;
                const double now = ch->assist.lowCutOn() ? ch->assist.lowCutHz : 0.0;
                if (now >= hz - 5.0) continue;
                // A low cut on its EQ page (yours, or AUTO's) does it already: never a second one.
                if (ch->hasProc && pv (ch->proc, proc::chainOn) > 0.5f && pv (ch->proc, proc::eqOn) > 0.5f)
                {
                    bool eqCut = false;
                    for (int b = 0; b < proc::kEqBands; ++b)
                        eqCut = eqCut || (shapeOf (ch->proc, b) == 3 && pv (ch->proc, proc::eqFreq (b)) >= hz - 5.0);
                    if (eqCut) continue;
                }
                Change c;
                c.hash = ch->hash;
                c.name = t.name;
                c.role = t.role;
                c.kind = "lowcut";
                c.from = now;
                c.to = c.wanted = hz;
                c.hz = hz;
                c.why = t.rumble ? fmt0 (share * 100.0) + "% of its energy is below " + std::to_string (lowCutTopHz (t.role)) + " Hz, where it only muddies the kick and bass"
                                 : "vocals have nothing useful below " + fmt0 (hz) + " Hz: a low cut keeps rumble and pops out";
                plan.changes.push_back (c);
                ++cuts;
            }

            // Mud: where the mix has clearly more 200 - 500 Hz than the style.
            if (r.mixBands.size() == kBands && r.targetBands.size() == kBands)
            {
                double dev = 0.0, worst = -1.0e9;
                int worstBand = 12;
                for (int b = 10; b <= 14; ++b)
                {
                    const double d = r.mixBands[static_cast<std::size_t> (b)] - r.targetBands[static_cast<std::size_t> (b)];
                    dev += d / 5.0;
                    if (d > worst) { worst = d; worstBand = b; }
                }
                if (dev > 2.5)
                {
                    const double hz = bandHz (worstBand);
                    std::vector<std::pair<double, const TrackReport*>> v;
                    for (const auto& t : r.tracks)
                        if (counts (t) && t.role != Role::kick && t.role != Role::fx) v.push_back ({ rangeE (t.bands, 10, 14) * t.activity, &t });
                    std::sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.first > b.first; });
                    // Bass instruments only when nothing else carries the mud.
                    const bool othersCarry = std::any_of (v.begin(), v.end(), [&] (auto& p) { return p.second->role != Role::bass && p.first > 0.25 * v.front().first; });
                    const double depth = std::clamp (std::round (dev * 0.7 * 2.0) / 2.0, 1.5, 4.0);
                    int taken = 0;
                    for (const auto& [e, t] : v)
                    {
                        if (taken >= 3 || e < 0.1 * v.front().first) break;
                        if (othersCarry && t->role == Role::bass) continue;
                        const double d = std::max (1.0, std::round (depth * (taken == 0 ? 1.0 : taken == 1 ? 0.8 : 0.6) * 2.0) / 2.0);
                        ++taken;
                        const auto* ch = eqChannel (*t, look, plan, "mud dip");
                        if (ch == nullptr) continue;
                        if (ch->assist.mudDb <= -d + 0.25) continue;
                        // Its EQ page cuts its mud since Analyze Mix listened (AUTO, or you): that track's mud is dealt with,
                        // never cut twice.
                        if (ch->hasProc && eqCutSince (ch->proc, atAnalysis (*ch), Job::mud) <= -1.5) continue;
                        Change c;
                        c.hash = ch->hash;
                        c.name = t->name;
                        c.role = t->role;
                        c.kind = "mud";
                        c.from = ch->assist.mudDb;
                        c.fromHz = ch->assist.mudHz;
                        c.to = c.wanted = -d;
                        // A mud dip it already has stays where it is, only deeper (as Vocal Pocket's): once it is in, the
                        // mix's worst band moves, and following it would take the old cut away.
                        c.hz = ch->assist.mudDb < -0.05f && ch->assist.mudHz >= 150.0f && ch->assist.mudHz <= 600.0f ? static_cast<double> (ch->assist.mudHz) : hz;
                        c.why = "the mix has " + fmt1 (dev) + " dB more 200 - 500 Hz than " + r.style.name + " usually does; this is one of the tracks carrying it";
                        plan.changes.push_back (c);
                        ++dips;
                    }
                }
            }
            if (cuts + dips == 0) return "nothing to clean up";
            std::vector<std::string> parts;
            if (cuts > 0) parts.push_back (std::to_string (cuts) + " low cut" + (cuts == 1 ? "" : "s"));
            if (dips > 0) parts.push_back (std::to_string (dips) + " mud dip" + (dips == 1 ? "" : "s"));
            return joinWords (parts, ", ");
        }

        // ---------------------------------------------------------------------------------------------
        // Vocal Pocket: dips where instruments cover the vocal's words (Assist EQ)

        constexpr int kPocketFrom = 19, kPocketTo = 23;   // 1.6 - 4 kHz, as Analyze Mix measures it
        constexpr double kMaxPocketDb = 6.0;              // deeper takes the instrument's own presence away

        std::string planPocket (const Report& r, const Lookup& look, Plan& plan)
        {
            std::vector<double> vocal (kBands, -120.0);
            bool any = false;
            for (const auto& t : r.tracks)
                if (counts (t) && t.role == Role::vox && t.bands.size() == kBands)
                {
                    any = true;
                    for (int b = 0; b < kBands; ++b)
                        vocal[static_cast<std::size_t> (b)] = toDb (toE (vocal[static_cast<std::size_t> (b)]) + toE (t.bands[static_cast<std::size_t> (b)]));
                }
            if (! any)
            {
                plan.notes.push_back ("No lead vocal found: set your vocal's role to Lead Vox (Your tracks), then try again.");
                return "no lead vocal";
            }
            double vocalMoved = 0.0;
            for (const auto& t : r.tracks)
                if (counts (t) && t.role == Role::vox) { vocalMoved = movedSince (look.of (t)); break; }
            int dips = 0, inPlace = 0;
            for (const auto& t : r.tracks)
            {
                const double mask = t.maskDb + movedSince (look.of (t)) - vocalMoved;
                if (! counts (t) || t.role == Role::vox || t.role == Role::adlibs || t.maskDb <= -100.0 || mask <= -6.0 || t.bands.size() != kBands) continue;
                const double d = mask >= 0.0 ? 4.0 : mask >= -3.0 ? 3.0 : 2.0;
                // Where it covers the vocal most: the band with the biggest overlap, within 1.6 - 4 kHz (the range the
                // advice measures: a dip outside it hardly uncovers the words).
                int best = 21;
                double bestOverlap = -1.0e9;
                for (int b = kPocketFrom; b <= kPocketTo; ++b)
                {
                    const double o = std::min (vocal[static_cast<std::size_t> (b)], t.bands[static_cast<std::size_t> (b)]);
                    if (o > bestOverlap) { bestOverlap = o; best = b; }
                }
                const auto* ch = eqChannel (t, look, plan, "vocal pocket");
                if (ch == nullptr) continue;
                // The dip Mixology heard it with (Analyze Mix measured the track with it): what's still covered needs
                // more on top. A dip made since Mixology listened (a plan applied) already counts. Advice from before
                // 0.16 didn't keep the Assist EQ it heard: then it heard the one there now.
                const auto& heard = ch->assistAtAnalysisKnown ? ch->assistAtAnalysis : ch->assist;
                // A dip it already has where the words are stays where it is and only ever gets deeper. Once it is in,
                // the overlap is biggest somewhere else, and moving it there took the old cut away (it went back and
                // forth between Auto Mix runs, sometimes shallower).
                auto inWords = [] (const AssistSettings& a) { return a.pocketDb < -0.05 && a.pocketHz >= 1500.0f && a.pocketHz <= 4300.0f; };
                const double hz = inWords (ch->assist) ? static_cast<double> (ch->assist.pocketHz)
                                : inWords (heard)      ? static_cast<double> (heard.pocketHz)
                                                       : bandHz (best);
                // A dip counts when it sits where the words are (1.6 - 4 kHz) and within half an octave of this one.
                auto dipCounts = [&] (const AssistSettings& a) {
                    return inWords (a) && std::abs (std::log2 (std::max (static_cast<double> (a.pocketHz), 1.0) / hz)) <= 0.5;
                };
                const double heardDip = dipCounts (heard) ? heard.pocketDb : 0.0;
                const double wanted = std::max (-kMaxPocketDb, heardDip - d);
                if (heardDip <= -kMaxPocketDb + 0.25)
                {
                    plan.notes.push_back (quoted (t.name) + " still covers the vocal with a " + fmt0 (kMaxPocketDb)
                                          + " dB pocket dip: turn it down a little or pan it off-centre instead.");
                    continue;
                }
                if (ch->assist.pocketDb <= wanted + 0.25 && dipCounts (ch->assist)) { ++inPlace; continue; }   // already there
                // A dip its EQ page made there since Mixology listened (AUTO, or you) counts too: never cut twice.
                {
                    const double eqDip = ch->hasProc ? std::min (0.0, eqSinceDb (ch->proc, atAnalysis (*ch), Job::presence, hz)) : 0.0;
                    const double dip = (dipCounts (ch->assist) ? static_cast<double> (ch->assist.pocketDb) : heardDip) + eqDip;
                    if (eqDip < -0.5 && dip <= wanted + 0.25) { ++inPlace; continue; }
                }
                Change c;
                c.hash = ch->hash;
                c.name = t.name;
                c.role = t.role;
                c.kind = "pocket";
                c.from = ch->assist.pocketDb;
                c.fromHz = ch->assist.pocketHz;
                c.to = c.wanted = wanted;
                c.hz = hz;
                c.why = "at 1.6 - 4 kHz it is " + (mask >= 0.0 ? fmt1 (mask) + " dB louder than the vocal while it sings, covering its words"
                                                                : "only " + fmt1 (-mask) + " dB under the vocal while it sings: close enough to cover its words")
                        + (heardDip < 0.0 ? " (with the " + fmt1 (-heardDip) + " dB dip it already has: a deeper one)" : "");
                plan.changes.push_back (c);
                ++dips;
            }
            if (dips == 0 && inPlace > 0) return "the dips are in since Mixology listened (it hears them when the song plays again)";
            if (dips == 0) return "nothing covers the vocal";
            return std::to_string (dips) + " pocket dip" + (dips == 1 ? "" : "s");
        }
        // ---------------------------------------------------------------------------------------------
        // Polish (Milestone 5, round 3): processing in each track's Mixology Link, and the Full Mix's tone

        float pv (const proc::Values& v, int id) { return v[static_cast<std::size_t> (id)]; }

        /** A band range's level, tilted +4.5 dB per octave (a balanced sound reads about flat). */
        double tilted (const std::vector<double>& bands, int from, int to)
        {
            double e = 0.0;
            int n = 0;
            for (int b = from; b <= to && b < static_cast<int> (bands.size()); ++b)
            {
                e += toE (bands[static_cast<std::size_t> (b)] + 4.5 * std::log2 (bandHz (b) / 1000.0));
                ++n;
            }
            return n > 0 ? toDb (e / n) : -120.0;
        }

        double plainLevel (const std::vector<double>& bands, int from, int to)
        {
            const int n = std::max (1, to - from + 1);
            return toDb (rangeE (bands, from, to) / n);
        }

        int loudestBand (const std::vector<double>& bands, int from, int to)
        {
            int best = from;
            for (int b = from; b <= to && b < static_cast<int> (bands.size()); ++b)
                if (bands[static_cast<std::size_t> (b)] > bands[static_cast<std::size_t> (best)]) best = b;
            return best;
        }

        std::string hzText (double hz)
        {
            if (hz < 1000.0) return fmt0 (hz) + " Hz";
            std::string k = fmt1 (hz / 1000.0);
            if (k.size() > 2 && k.compare (k.size() - 2, 2, ".0") == 0) k.resize (k.size() - 2);
            return k + " kHz";
        }

        double roundHalf (double v) { return std::round (v * 2.0) / 2.0; }

        bool looksLikeFxChannel (const std::string& name)
        {
            static const std::regex fx (R"(^fx\s*\d|\b(verbs?|reverbs?|delays?|echo|returns?|aux|sends?|plate|hall)\b)", std::regex::icase);
            return std::regex_search (name, fx);
        }

        /** An FX channel Polish may put the vocal's reverb on: one already set up as a return, one you or its name
            call an FX return, or one named like a return that plays no instrument or vocal ("Space Pad" is a pad). */
        bool isFxReturn (const ChannelNow& c, const Report& r)
        {
            if (c.hasProc && pv (c.proc, proc::spRoute) > 0.5f) return true;
            const TrackReport* t = nullptr;
            for (const auto& x : r.tracks) if ((c.hash != 0 && x.hash == c.hash) || (c.hash == 0 && x.name == c.name)) t = &x;
            if (t != nullptr && t->role == Role::fx) return true;
            return looksLikeFxChannel (c.name) && (t == nullptr || t->role == Role::other);
        }

        /** The Link that carries a track's processing, or nullptr (with a note why). */
        const ChannelNow* procChannel (const TrackReport& t, const Lookup& look, Plan& plan, const char* what)
        {
            const auto* ch = look.of (t);
            if (ch != nullptr && ch->sharedName)
            {
                noteShared (plan, t.name);
                return nullptr;
            }
            if (ch == nullptr || ! ch->hasLink || ! ch->hasProc) return nullptr;   // Clean Up already says so
            if (ch->locked)
            {
                plan.notes.push_back (quoted (t.name) + " is protected (padlock): " + what + " skipped.");
                return nullptr;
            }
            if (pv (ch->proc, proc::chainOn) < 0.5f)
            {
                plan.notes.push_back (quoted (t.name) + ": its Link's processing is switched off (On / Off in its window): " + what + " skipped.");
                return nullptr;
            }
            return ch;
        }

        const proc::Values& atAnalysis (const ChannelNow& ch) { return ch.procAtAnalysisKnown ? ch.procAtAnalysis : ch.proc; }

        std::string bandText (const proc::Values& v, int band)
        {
            const float g = pv (v, proc::eqGain (band));
            const int type = static_cast<int> (std::lround (pv (v, proc::eqType (band))));
            if (pv (v, proc::eqOn) < 0.5f) return "Flat";
            if (type == 3) return "Low cut " + hzText (pv (v, proc::eqFreq (band)));
            if (std::abs (g) < 0.05f) return "Flat";
            return signedDb (g) + " at " + hzText (pv (v, proc::eqFreq (band))) + (type == 2 ? " (shelf)" : "");
        }

        int shapeOf (const proc::Values& v, int b) { return static_cast<int> (std::lround (pv (v, proc::eqType (b)))); }
        bool flatBand (const proc::Values& v, int b) { return std::abs (pv (v, proc::eqGain (b))) < 0.05f && shapeOf (v, b) != 3 && shapeOf (v, b) != 4; }
        bool bandDoing (const proc::Values& v, int b, int type, double hz)
        {
            return ! flatBand (v, b) && shapeOf (v, b) == type && std::abs (std::log2 (pv (v, proc::eqFreq (b)) / hz)) < 0.4;
        }

        /** The EQ band for a move: one already doing this job when Analyze Mix listened (same shape, near the
            frequency: it is adjusted, never doubled), else one doing it now (set since: by you, or by AUTO), else the
            preferred band if it is flat (then and now), else another flat one. -1: all in use. */
        int pickBand (const proc::Values& at, const proc::Values& now, int preferred, int type, double hz)
        {
            for (int b = 0; b < proc::kEqBands; ++b)
                if (bandDoing (at, b, type, hz)) return b;
            for (int b = 0; b < proc::kEqBands; ++b)
                if (bandDoing (now, b, type, hz)) return b;
            auto free = [&] (int b) { return flatBand (at, b) && flatBand (now, b); };
            if (free (preferred)) return preferred;
            for (int b = 1; b <= 4; ++b)
                if (free (b)) return b;
            return -1;
        }

        // ---- what a track's tone controls do, by job: the controls doing the same job count against each other ----

        constexpr double kRate = 48000.0;
        constexpr double kEqLimit = 4.0;     // AUTO's EQ moves: usually 1.5 - 4 dB (never more than 6)
        constexpr double kToneLimit = 3.0;   // Tone Balance: a broad tilt, at most about 3 dB

        /** The job an EQ band does (by its shape and where it sits). */
        std::optional<Job> jobOf (const proc::Values& v, int b)
        {
            const int type = shapeOf (v, b);
            const double hz = pv (v, proc::eqFreq (b));
            if (type == 3) return Job::lowCut;
            if (type == 4) return std::nullopt;   // a high cut: none of AUTO's jobs
            if (type == 1) return Job::low;
            if (type == 2) return hz >= 4000.0 ? Job::air : Job::presence;
            if (hz < 150.0) return Job::low;
            if (hz < 650.0) return Job::mud;
            if (hz < 1800.0) return Job::mid;
            if (hz < 6500.0) return Job::presence;
            return Job::air;
        }

        /** One EQ band's response (dB) as it sounds with these settings (0 while the EQ or the processing is off). */
        double bandDb (const proc::Values& v, int b, double hz)
        {
            if (pv (v, proc::chainOn) < 0.5f || pv (v, proc::eqOn) < 0.5f) return 0.0;
            const auto d = proc::Chain::designBand (v, b, kRate);
            return d.active ? d.stages * dsp::magnitudeDb (d.k, hz, kRate) : 0.0;
        }

        /** A band of this shape alone, on top of `like`'s EQ character: its average response over bands from..to. */
        double bandRangeDb (const proc::Values& like, int type, double hz, double q, double gain, int from, int to)
        {
            auto v = like;
            v[proc::chainOn] = 1.0f;
            v[proc::eqOn] = 1.0f;
            v[static_cast<std::size_t> (proc::eqType (0))] = static_cast<float> (type);
            v[static_cast<std::size_t> (proc::eqFreq (0))] = static_cast<float> (hz);
            v[static_cast<std::size_t> (proc::eqQ (0))] = static_cast<float> (q);
            v[static_cast<std::size_t> (proc::eqGain (0))] = static_cast<float> (gain);
            double sum = 0.0;
            int n = 0;
            for (int b = from; b <= to; ++b, ++n) sum += bandDb (v, 0, bandHz (b));
            return n > 0 ? sum / n : 0.0;
        }

        /** Tone Balance's knob for a job (-1: none), and its shape (the engine's: ProcChain tone()). */
        int toneKnob (Job j) { return j == Job::low ? proc::tbWarmth : j == Job::presence ? proc::tbPresence : j == Job::air ? proc::tbAir : -1; }
        double toneKnobDb (int knob, double gain, double hz)
        {
            if (std::abs (gain) < 1.0e-4) return 0.0;
            const auto k = knob == proc::tbWarmth ? dsp::design (dsp::Shape::lowShelf, 220.0, gain, 0.6, kRate)
                         : knob == proc::tbPresence ? dsp::design (dsp::Shape::bell, 3200.0, gain, 0.8, kRate)
                                                    : dsp::design (dsp::Shape::highShelf, 11000.0, gain, 0.6, kRate);
            return dsp::magnitudeDb (k, hz, kRate);
        }
        double toneRangeDb (int knob, double gain, int from, int to)
        {
            double sum = 0.0;
            int n = 0;
            for (int b = from; b <= to; ++b, ++n) sum += toneKnobDb (knob, gain, bandHz (b));
            return n > 0 ? sum / n : 0.0;
        }
        double toneDb (const proc::Values& v, Job j, double hz)
        {
            const int knob = toneKnob (j);
            if (knob < 0 || pv (v, proc::chainOn) < 0.5f || pv (v, proc::tbOn) < 0.5f) return 0.0;
            return toneKnobDb (knob, pv (v, knob), hz);
        }

        /** The Assist EQ's stage for a job (Clean Up's low cut and mud dip, Vocal Pocket's dip). */
        double assistDb (const AssistSettings& a, Job j, double hz)
        {
            AssistSettings one;
            if (j == Job::lowCut) one.lowCutHz = a.lowCutHz;
            else if (j == Job::mud) { one.mudHz = a.mudHz; one.mudDb = a.mudDb; }
            else if (j == Job::presence) { one.pocketHz = a.pocketHz; one.pocketDb = a.pocketDb; }
            else return 0.0;
            return AssistEq::responseDb (one, hz, kRate);
        }

        /** The deepest cut an EQ bell doing this job got since Analyze Mix listened (dB, 0 = none). */
        double eqCutSince (const proc::Values& now, const proc::Values& at, Job job)
        {
            if (pv (now, proc::chainOn) < 0.5f || pv (now, proc::eqOn) < 0.5f) return 0.0;
            double deepest = 0.0;
            for (int b = 0; b < proc::kEqBands; ++b)
            {
                if (shapeOf (now, b) != 0 || jobOf (now, b) != job) continue;
                const bool wasOn = pv (at, proc::chainOn) > 0.5f && pv (at, proc::eqOn) > 0.5f && shapeOf (at, b) == 0;
                deepest = std::min (deepest, static_cast<double> (pv (now, proc::eqGain (b))) - (wasOn ? pv (at, proc::eqGain (b)) : 0.0));
            }
            return deepest;
        }

        /** How much the EQ's bands doing this job changed (dB) at a frequency since Analyze Mix listened. */
        double eqSinceDb (const proc::Values& now, const proc::Values& at, Job job, double hz, int skipBand)
        {
            double d = 0.0;
            for (int b = 0; b < proc::kEqBands; ++b)
            {
                if (b == skipBand) continue;
                if (jobOf (now, b) == job) d += bandDb (now, b, hz);
                if (jobOf (at, b) == job) d -= bandDb (at, b, hz);
            }
            return d;
        }

        struct SpaceSound
        {
            int type = 0;
            std::vector<std::pair<int, float>> params;
            std::string words;
        };

        SpaceSound vocalReverb (const std::string& styleKey, bool onFxChannel)
        {
            const auto genre = styleKey.substr (0, styleKey.find ('.'));
            SpaceSound s;
            if (genre == "gospel" || genre == "worship" || genre == "cinematic")
            {
                s.type = static_cast<int> (proc::SpaceType::hall);
                s.params = { { proc::hallPre, 40.0f }, { proc::hallDecay, 2.6f }, { proc::hallSize, 80.0f }, { proc::hallDamp, 5000.0f },
                             { proc::hallMix, onFxChannel ? 100.0f : 10.0f } };
                s.words = "a hall (2.6 s)";
            }
            else
            {
                const bool urban = genre == "hiphop" || genre == "trap" || genre == "rnb" || genre == "afro" || genre == "latin";
                const float decay = urban ? 1.4f : 1.8f;
                s.type = static_cast<int> (proc::SpaceType::reverb);
                s.params = { { proc::revPre, 30.0f }, { proc::revDecay, decay }, { proc::revDamp, 6500.0f }, { proc::revWidth, 100.0f },
                             { proc::revMix, onFxChannel ? 100.0f : 12.0f } };
                s.words = std::string ("a vocal plate (") + fmt1 (decay) + " s)";
            }
            return s;
        }

        /** Where the lead vocal's reverb sits under the vocal (dB, loudness while each plays): drier in the urban
            styles, wetter in gospel / worship / cinematic. */
        double reverbUnderVocal (const std::string& styleKey)
        {
            const auto genre = styleKey.substr (0, styleKey.find ('.'));
            if (genre == "hiphop" || genre == "trap" || genre == "rnb" || genre == "afro" || genre == "latin") return -20.0;
            if (genre == "gospel" || genre == "worship" || genre == "cinematic" || genre == "jazz") return -14.0;
            return -17.0;
        }

        /** How loud each effect comes out against what goes in (measured on vocal-like sound, wet EQ at its start). */
        double wetGainDb (int type)
        {
            switch (static_cast<proc::SpaceType> (type))
            {
                case proc::SpaceType::reverb: return -5.0;
                case proc::SpaceType::hall: return -6.0;
                case proc::SpaceType::room: return -7.0;
                case proc::SpaceType::chorus: return -6.0;
                case proc::SpaceType::phaser: return -4.0;
                case proc::SpaceType::special: return -3.0;
                case proc::SpaceType::delay: return 0.0;
            }
            return -5.0;
        }

        /** The Space effects a channel's Link plays now (bit per proc::SpaceType; Space layers: up to three). */
        std::uint32_t spaceNow (const ChannelNow& c) { return c.hasProc ? proc::spaceEffects (c.proc) : 0u; }
        std::uint32_t reverbsOf (std::uint32_t bits)
        {
            std::uint32_t out = 0;
            for (auto t : proc::kSpaceOrder)
                if ((bits & proc::bitOf (t)) != 0 && proc::isReverbType (t)) out |= proc::bitOf (t);
            return out;
        }
        std::string lowerNames (std::uint32_t bits)
        {
            auto s = proc::effectNames (bits, " + ");
            for (auto& ch : s) ch = static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
            return s;
        }
        /** A Space change that makes exactly this effect play there (every effect's switch set: nothing switched on
            before comes back with it), plus its settings. */
        std::vector<std::pair<int, float>> spaceAlone (proc::SpaceType t, std::vector<std::pair<int, float>> more)
        {
            std::vector<std::pair<int, float>> out;
            for (const auto& c : proc::spaceControls (proc::bitOf (t), t)) out.push_back (c);
            out.insert (out.end(), more.begin(), more.end());
            return out;
        }

        const TrackReport* reportOf (const Report& r, std::uint32_t hash)
        {
            for (const auto& t : r.tracks)
                if (t.hash == hash) return &t;
            return nullptr;
        }

        // ---------------------------------------------------------------------------------------------
        // The per-track rules: ONE set for Polish, Auto Mix and each processing page's AUTO button.
        //
        // Every rule works from what Analyze Mix heard of the track (its spectrum and levels, with the processing it had
        // then), sets its page's controls on top of what they were then (so it gives the same answer however often it
        // runs), and counts what the track's other controls doing the same job changed since then: Tone Balance's air
        // against the EQ's air shelf, the Assist EQ's mud and pocket dips against the EQ's bells, an EQ band you set
        // since. So the pages never double or undo each other: AUTO on one page then Auto Mix, or the other way round,
        // finds nothing more to do.

        struct Rules
        {
            const Report& r;
            const Lookup& look;
            const std::vector<ChannelNow>& channels;
            Plan& plan;
            bool joint;          // Polish / Auto Mix: every page at once; a fix two pages can make goes to the one it suits
            std::string genre;
            std::map<std::uint32_t, proc::Values> planned;          // each Link's processing with this plan's changes so far
            std::map<std::uint32_t, AssistSettings> plannedAssist;  // ... and its Assist EQ (Clean Up / Vocal Pocket before)
            const TrackReport* lead = nullptr;                      // the lead vocal (its reverb)
            int adlibDelays = 0;

            Rules (const Report& rr, const Lookup& l, const std::vector<ChannelNow>& ch, Plan& p, bool j)
                : r (rr), look (l), channels (ch), plan (p), joint (j), genre (rr.style.key.substr (0, rr.style.key.find ('.')))
            {
                for (const auto& c : channels)
                {
                    if (c.hasProc) planned[c.hash] = c.proc;
                    if (c.hasLink) plannedAssist[c.hash] = c.assist;
                }
                for (const auto& c : plan.changes)   // what Clean Up and Vocal Pocket propose in this plan already
                {
                    if (c.fullMix || c.hash == 0) continue;
                    if (c.kind == "lowcut") plannedAssist[c.hash].lowCutHz = static_cast<float> (c.to);
                    else if (c.kind == "mud") { plannedAssist[c.hash].mudDb = static_cast<float> (c.to); plannedAssist[c.hash].mudHz = static_cast<float> (c.hz); }
                    else if (c.kind == "pocket") { plannedAssist[c.hash].pocketDb = static_cast<float> (c.to); plannedAssist[c.hash].pocketHz = static_cast<float> (c.hz); }
                    else if (auto it = planned.find (c.hash); it != planned.end())
                        for (const auto& [id, v] : c.proc) it->second[static_cast<std::size_t> (id)] = v;
                }
                for (const auto& t : r.tracks)   // the lead vocal: the loudest one that sings most (not a double of it)
                {
                    if (! counts (t) || t.role != Role::vox || doubleTake (r, t)) continue;
                    auto weight = [] (const TrackReport& x) { return x.loudness + 10.0 * std::log10 (std::max (0.01, x.activity)); };
                    if (lead == nullptr || weight (t) > weight (*lead)) lead = &t;
                }
            }
            const proc::Values& procOf (const ChannelNow& c) const
            {
                const auto it = planned.find (c.hash);
                return it != planned.end() ? it->second : c.proc;
            }
            const AssistSettings& assistOf (const ChannelNow& c) const
            {
                const auto it = plannedAssist.find (c.hash);
                return it != plannedAssist.end() ? it->second : c.assist;
            }
            void push (Change c)
            {
                if (! c.fullMix && c.hash != 0)
                {
                    auto it = planned.find (c.hash);
                    if (it == planned.end()) it = planned.emplace (c.hash, proc::defaults()).first;
                    for (const auto& [id, v] : c.proc) it->second[static_cast<std::size_t> (id)] = v;
                }
                plan.changes.push_back (std::move (c));
            }
            void note (const std::string& n) { plan.notes.push_back (n); }
            bool urban() const { return genre == "hiphop" || genre == "trap" || genre == "rnb" || genre == "afro" || genre == "latin"; }
        };

        /** What the track's controls doing this job changed since Analyze Mix listened (dB, averaged over bands
            from..to): its EQ bands (not `skipBand`, the one being set), Tone Balance's knob (unless `skipTone`), the
            Assist EQ's stage, as this plan leaves them. */
        double since (const Rules& R, const ChannelNow& ch, Job job, int from, int to, int skipBand, bool skipTone)
        {
            const auto& now = R.procOf (ch);
            const auto& at = atAnalysis (ch);
            const auto& an = R.assistOf (ch);
            const auto& aa = ch.assistAtAnalysisKnown ? ch.assistAtAnalysis : ch.assist;
            double sum = 0.0;
            int n = 0;
            for (int b = from; b <= to; ++b, ++n)
            {
                const double hz = bandHz (b);
                double d = eqSinceDb (now, at, job, hz, skipBand);
                if (! skipTone) d += toneDb (now, job, hz) - toneDb (at, job, hz);
                d += assistDb (an, job, hz) - assistDb (aa, job, hz);
                sum += d;
            }
            return n > 0 ? sum / n : 0.0;
        }

        std::string withDb (std::string text, double db)
        {
            const auto at = text.find ("{db}");
            if (at != std::string::npos) text.replace (at, 4, signedDb (db));
            return text;
        }

        /** One tonal fix a track needs: the job, the range it is about, and the correction (dB) a band of the preferred
            EQ shape would get for it. Pages that can make it: the EQ (a band) and / or Tone Balance (its knob). */
        struct Fix
        {
            Job job = Job::air;
            int from = 26, to = 28;      // the range (bands) it is measured over
            Page pref = Page::eq;        // the page it suits best: Polish / Auto Mix make it there
            bool onEq = true, onTone = false;
            int band = 5, type = 2;      // the EQ realisation: preferred band, shape, where, how wide
            double hz = 10000.0, q = 0.7;
            double gain = 0.0;           // the correction (dB, as that EQ band alone would get it)
            double limit = kEqLimit;
            std::string why, didEq, didTone, heard, heardPhrase;
            bool byStyle = false;
        };

        /** The range's correction in dB (averaged over its bands) that a fix stands for. */
        double fixRangeDb (const Fix& f, const proc::Values& like) { return bandRangeDb (like, f.type, f.hz, f.q, f.gain, f.from, f.to); }

        /** The fix on the EQ page: a band set to what is still to do after the other controls doing this job. */
        bool eqFix (Rules& R, const TrackReport& t, const ChannelNow& ch, const Fix& f)
        {
            const auto& at = atAnalysis (ch);
            const auto& now = R.procOf (ch);
            // EQ switched off with settings of yours in it: switching it on would bring those back too.
            if (pv (now, proc::eqOn) < 0.5f)
                for (int b = 0; b < proc::kEqBands; ++b)
                    if (std::abs (pv (now, proc::eqGain (b))) >= 0.05f)
                    {
                        R.note (quoted (t.name) + ": its EQ is switched off with settings in it: EQ left alone.");
                        return false;
                    }
            const int band = pickBand (at, now, f.band, f.type, f.hz);
            if (band < 0)
            {
                R.note (quoted (t.name) + ": all its EQ bands are in use: " + f.why + " (do it on the EQ page).");
                return false;
            }
            const bool sameAt = bandDoing (at, band, f.type, f.hz);
            const bool sameNow = ! sameAt && bandDoing (now, band, f.type, f.hz);
            const double base = sameAt ? pv (at, proc::eqGain (band)) : 0.0;
            const double useHz = sameAt ? pv (at, proc::eqFreq (band)) : sameNow ? pv (now, proc::eqFreq (band)) : f.hz;
            const double useQ = sameAt ? pv (at, proc::eqQ (band)) : sameNow ? pv (now, proc::eqQ (band)) : f.q;
            // What is still to do in the range after the other controls doing this job (Tone Balance, the Assist EQ,
            // another band set since), and what this band needs for it.
            const double want = fixRangeDb (f, at);
            const double others = since (R, ch, f.job, f.from, f.to, band, false);
            double rest = want - others;
            if (rest * f.gain <= 0.0 || (std::abs (others) > 0.05 && std::abs (rest) < 0.5)) rest = 0.0;   // the others do it all (or more)
            const double per = bandRangeDb (at, f.type, useHz, useQ, f.gain, f.from, f.to) / f.gain;   // this band's share per dB
            const double corr = per > 0.02 ? rest / per : 0.0;
            const double gain = std::clamp (roundHalf (base + corr), -f.limit, f.limit);
            const bool on = pv (now, proc::eqOn) > 0.5f;
            const bool already = on && shapeOf (now, band) == f.type && std::abs (std::log2 (pv (now, proc::eqFreq (band)) / useHz)) < 0.05
                                 && std::abs (pv (now, proc::eqGain (band)) - gain) < 0.5;
            if (already || std::abs (gain - (on && shapeOf (now, band) == f.type ? pv (now, proc::eqGain (band)) : 0.0)) < 0.5)
            {
                if (rest == 0.0 && std::abs (base - gain) < 0.5 && std::abs (want) >= 0.5)
                    R.note (quoted (t.name) + ": " + f.why + ", but " + (f.onTone ? "Tone Balance or another EQ band" : "the Assist EQ or another EQ band")
                            + " does that already: nothing to add.");
                return false;
            }
            Change c;
            c.hash = ch.hash;
            c.name = t.name;
            c.role = t.role;
            c.kind = "eq";
            c.page = pageKey (Page::eq);
            c.from = pv (now, proc::eqGain (band));
            c.to = gain;
            c.hz = useHz;
            c.proc = { { proc::eqOn, 1.0f }, { proc::eqType (band), static_cast<float> (f.type) }, { proc::eqFreq (band), static_cast<float> (useHz) },
                       { proc::eqQ (band), static_cast<float> (useQ) }, { proc::eqGain (band), static_cast<float> (gain) } };
            c.fromText = bandText (now, band);
            c.toText = signedDb (gain) + " at " + hzText (useHz) + (f.type == 2 ? " (shelf)" : "");
            c.why = f.why;
            c.did = withDb (f.didEq, gain - base);
            c.heard = f.heard;
            c.heardPhrase = f.heardPhrase;
            c.byStyle = f.byStyle;
            R.push (std::move (c));
            return true;
        }

        const char* toneLabel (int knob) { return knob == proc::tbWarmth ? "Warmth " : knob == proc::tbPresence ? "Presence " : "Air "; }

        /** The fix on the Tone page: Tone Balance's knob for the job, set to what is still to do after the EQ and the
            Assist EQ (their bands doing this job, as this plan leaves them). */
        bool toneFix (Rules& R, const TrackReport& t, const ChannelNow& ch, const Fix& f)
        {
            const int knob = toneKnob (f.job);
            if (knob < 0) return false;
            const auto& at = atAnalysis (ch);
            const auto& now = R.procOf (ch);
            const bool onAt = pv (at, proc::chainOn) > 0.5f && pv (at, proc::tbOn) > 0.5f, onNow = pv (now, proc::tbOn) > 0.5f;
            const double base = onAt ? pv (at, knob) : 0.0;
            const double want = fixRangeDb (f, at);
            const double others = since (R, ch, f.job, f.from, f.to, -1, true);
            double rest = want - others;
            if (rest * f.gain <= 0.0 || (std::abs (others) > 0.05 && std::abs (rest) < 0.5)) rest = 0.0;
            const double per = toneRangeDb (knob, f.gain, f.from, f.to) / f.gain;
            const double value = std::clamp (roundHalf (base + (per > 0.02 ? rest / per : 0.0)), -kToneLimit, kToneLimit);
            const double nowV = onNow ? pv (now, knob) : 0.0;
            if (std::abs (value - nowV) < 0.5)
            {
                if (rest == 0.0 && std::abs (want) >= 0.5 && std::abs (value - base) < 0.5)
                    R.note (quoted (t.name) + ": " + f.why + ", but its EQ (or the Assist EQ) does that already: nothing to add.");
                return false;
            }
            Change c;
            c.hash = ch.hash;
            c.name = t.name;
            c.role = t.role;
            c.kind = "tone";
            c.page = pageKey (Page::tone);
            c.from = nowV;
            c.to = value;
            c.proc = { { proc::tbOn, 1.0f } };
            if (! onNow)   // Tone Balance switches on: what it had (and doesn't use here) starts from neutral
                for (int id : { proc::tbWarmth, proc::tbPresence, proc::tbAir, proc::tbGlue, proc::tbPunch })
                    if (id != knob) c.proc.push_back ({ id, 0.0f });
            c.proc.push_back ({ knob, static_cast<float> (value) });
            c.fromText = onNow ? toneLabel (knob) + signedDb (nowV) : std::string ("Tone Balance off");
            c.toText = toneLabel (knob) + signedDb (value);
            c.why = f.why;
            c.did = withDb (f.didTone, value - base);
            c.heard = f.heard;
            c.heardPhrase = f.heardPhrase;
            c.byStyle = f.byStyle;
            R.push (std::move (c));
            return true;
        }

        /** A range's level (energy average of its bands), tilted +1.5 dB per octave like the style's target curve (a
            balanced mix reads about flat). */
        double level15 (const std::vector<double>& bands, int from, int to)
        {
            double e = 0.0;
            int n = 0;
            for (int b = from; b <= to && b < static_cast<int> (bands.size()); ++b, ++n)
                e += toE (bands[static_cast<std::size_t> (b)] + 1.5 * std::log2 (bandHz (b) / 1000.0));
            return n > 0 ? toDb (e / n) : -120.0;
        }
        /** The same range of the style's target curve (the Tone page's red line). */
        double targetLevel (const Style& s, int from, int to)
        {
            double e = 0.0;
            int n = 0;
            for (int b = from; b <= to; ++b, ++n) e += toE (Analysis::targetDb (s, bandHz (b)));
            return n > 0 ? toDb (e / n) : -120.0;
        }

        /** Where a kind of track's top end (8 - 12.5 kHz) and low end (63 - 125 Hz) usually sit against its mids
            (630 Hz - 2 kHz), in the mix target's tilt: below `dull` it sounds dull, above `bright` sharp, low end above
            `boomy` boomy. First guesses from typical sounds, tested with test spectra; to tune with real sessions. */
        struct ToneNorm { bool top = false, low = false; double dull = -99.0, bright = 99.0, boomy = 99.0; Page topPage = Page::tone; };
        ToneNorm toneNorm (Role role)
        {
            switch (role)
            {
                case Role::keys: return { true, true, -21.0, -3.0, 6.0, Page::eq };
                case Role::guitar: return { true, true, -24.0, -2.0, 4.0, Page::tone };
                case Role::synth: return { true, true, -20.0, 1.0, 6.0, Page::tone };
                case Role::perc: return { true, true, -14.0, 18.0, 5.0, Page::tone };   // shakers are bright by nature
                case Role::hats: return { true, false, -2.0, 99.0, 99.0, Page::tone };   // bright is their job; a harsh peak is the EQ's
                case Role::other: return { true, true, -22.0, 2.0, 6.0, Page::tone };
                default: return {};
            }
        }

        /** The tonal fixes a track needs, worked out once from what Analyze Mix heard: the EQ page and the Tone page
            both use this list, each making a fix its own way, and each counting what the other already does. */
        std::vector<Fix> tonalFixes (const Rules& R, const TrackReport& t)
        {
            std::vector<Fix> out;
            if (t.bands.size() < static_cast<std::size_t> (kBands)) return out;
            const auto& s = R.r.style;
            if (t.role == Role::vox)
            {
                // Body 200 - 800 Hz, presence 2 - 5 kHz, air 8 - 12.5 kHz, tilted 4.5 dB / octave (a clear vocal reads level).
                const double body = tilted (t.bands, 10, 16), pres = tilted (t.bands, 20, 24), air = tilted (t.bands, 26, 28);
                const double clarity = pres - body;
                if (clarity < -2.5)
                {
                    Fix f;
                    f.job = Job::presence; f.from = 20; f.to = 24; f.band = 3; f.type = 0; f.hz = 3500.0; f.q = 1.0; f.onTone = true;
                    f.gain = std::clamp ((-1.0 - clarity) * 0.6, 1.0, 3.0);
                    f.why = "the vocal is dull: its presence (2 - 5 kHz) is " + fmt1 (-clarity) + " dB under its body; a little lift makes every word clear";
                    f.didEq = "lifted its presence ({db} at 3.5 kHz)";
                    f.didTone = "lifted its presence (Presence {db})";
                    f.heard = "dull";
                    out.push_back (f);
                }
                const double airGap = air - pres;
                if (airGap < -7.0)
                {
                    Fix f;
                    f.job = Job::air; f.from = 26; f.to = 28; f.band = 5; f.type = 2; f.hz = 10000.0; f.q = 0.7; f.onTone = true;
                    f.gain = std::clamp ((-4.0 - airGap) * 0.5, 1.0, 3.0);
                    f.why = "the vocal has little air (8 - 12 kHz is " + fmt1 (-airGap) + " dB under its presence): a gentle shelf opens it up";
                    f.didEq = "opened the top a little ({db} air)";
                    f.didTone = "opened the top a little (Air {db})";
                    f.heard = "dull";
                    out.push_back (f);
                }
                return out;
            }
            const double mid = level15 (t.bands, 15, 20);
            if (t.role == Role::snare)
            {
                // Its crack (2.5 - 5 kHz) against its body (160 - 250 Hz).
                const double crack = level15 (t.bands, 21, 24) - level15 (t.bands, 9, 11);
                const double thr = -10.0 + (s.air - 2.0) * 0.5;
                if (crack < thr)
                {
                    Fix f;
                    f.job = Job::presence; f.from = 21; f.to = 24; f.band = 3; f.type = 0; f.hz = 3500.0; f.q = 1.0; f.onTone = true;
                    f.gain = std::clamp (0.5 * (thr - crack) + 1.5, 1.5, 3.0);
                    f.why = "the snare is dull: its crack (2.5 - 5 kHz) is " + fmt1 (std::abs (crack)) + " dB under its body; a lift brings its snap back";
                    f.didEq = "brought out its crack ({db} at 3.5 kHz)";
                    f.didTone = "brought out its crack (Presence {db})";
                    f.heard = "dull";
                    f.byStyle = true;
                    out.push_back (f);
                }
                return out;
            }
            auto topFix = [&] (double gain, Page pref, const std::string& why, bool lift, bool byStyle) {
                Fix f;
                f.job = Job::air; f.from = 26; f.to = 28; f.band = 5; f.type = 2; f.hz = 10000.0; f.q = 0.7; f.onTone = true; f.pref = pref;
                f.gain = gain;
                f.why = why;
                f.didEq = lift ? "opened the top ({db} air)" : "softened the top ({db} air)";
                f.didTone = lift ? "opened the top (Air {db})" : "softened the top (Air {db})";
                f.heard = lift ? "dull" : "sharp";
                f.byStyle = byStyle;
                out.push_back (f);
            };
            auto lowFix = [&] (double gain, const std::string& why, bool byStyle) {
                Fix f;
                f.job = Job::low; f.from = 5; f.to = 8; f.band = 0; f.type = 1; f.hz = 120.0; f.q = 0.7; f.onTone = true; f.pref = Page::tone;
                f.gain = gain;
                f.why = why;
                f.didEq = gain < 0.0 ? "took some boom out ({db} under 120 Hz)" : "added some weight ({db} under 120 Hz)";
                f.didTone = gain < 0.0 ? "took some boom out (Warmth {db})" : "added some warmth (Warmth {db})";
                f.heard = gain < 0.0 ? "boomy" : "thin";
                f.byStyle = byStyle;
                out.push_back (f);
            };
            if (t.role == Role::beat)
            {
                // A whole beat is a mix of its own: its tone against the style's target curve, like the Full Mix's.
                const double top = (level15 (t.bands, 26, 28) - mid) - (targetLevel (s, 26, 28) - targetLevel (s, 15, 20));
                const double low = (level15 (t.bands, 5, 8) - mid) - (targetLevel (s, 5, 8) - targetLevel (s, 15, 20));
                if (top < -4.0) topFix (std::clamp (0.5 * (-4.0 - top) + 1.0, 1.0, 3.0), Page::tone, "the beat's top end is " + fmt1 (-top) + " dB under " + s.name + "'s", true, true);
                else if (top > 4.0) topFix (-std::clamp (0.5 * (top - 4.0) + 1.0, 1.0, 3.0), Page::tone, "the beat's top end is " + fmt1 (top) + " dB over " + s.name + "'s", false, true);
                if (low < -4.0) lowFix (std::clamp (0.5 * (-4.0 - low) + 1.0, 1.0, 3.0), "the beat's low end is " + fmt1 (-low) + " dB under " + s.name + "'s", true);
                else if (low > 4.0) lowFix (-std::clamp (0.5 * (low - 4.0) + 1.0, 1.0, 3.0), "the beat's low end is " + fmt1 (low) + " dB over " + s.name + "'s", true);
                return out;
            }
            const auto n = toneNorm (t.role);
            const double shift = (s.air - 2.0) * 0.5;   // brighter styles want more top
            if (n.top)
            {
                const double top = level15 (t.bands, 26, 28) - mid;
                const std::string what = "its top end (8 - 12 kHz) is ";
                if (top < n.dull + shift)
                    topFix (std::clamp (0.5 * (n.dull + shift - top) + 1.0, 1.0, 3.0), n.topPage, what + fmt1 (std::abs (top)) + " dB under its mids", true, true);
                else if (top > n.bright + shift)
                    topFix (-std::clamp (0.5 * (top - n.bright - shift) + 1.0, 1.0, 3.0), n.topPage, what + overUnder (top) + " its mids: bright for " + std::string (roleWords (t.role)), false, true);
            }
            if (n.low)
            {
                const double low = level15 (t.bands, 5, 8) - mid;
                const double thr = n.boomy + (s.bass - 4.0) * 0.5;   // bass-heavy styles take more of it
                if (low > thr) lowFix (-std::clamp (0.5 * (low - thr) + 1.0, 1.0, 3.0), "its low end (63 - 125 Hz) is " + overUnder (low) + " its mids: boomy for " + std::string (roleWords (t.role)), true);
            }
            return out;
        }

        /** A low cut on the EQ page (band 1: the one that can be a low cut), unless the Assist EQ's (Clean Up) or the
            EQ's own low cut does it already. */
        bool lowCutFix (Rules& R, const TrackReport& t, const ChannelNow& ch, double hz, const std::string& why, const std::string& heardPhrase)
        {
            const auto& now = R.procOf (ch);
            const auto& a = R.assistOf (ch);
            if (a.lowCutOn() && a.lowCutHz >= hz - 5.0) return false;   // the Assist EQ's low cut (Clean Up) does it
            const bool on = pv (now, proc::eqOn) > 0.5f;
            if (! on)
                for (int b = 0; b < proc::kEqBands; ++b)
                    if (std::abs (pv (now, proc::eqGain (b))) >= 0.05f)
                    {
                        R.note (quoted (t.name) + ": its EQ is switched off with settings in it: EQ left alone.");
                        return false;
                    }
            int band = -1;
            for (int b = 0; b < proc::kEqBands && band < 0; ++b)
                if (shapeOf (now, b) == 3) band = b;   // a low cut there already: moved, never a second one
            if (band >= 0 && on && pv (now, proc::eqFreq (band)) >= hz - 5.0) return false;
            if (band < 0)
            {
                if (! flatBand (now, 0) || ! flatBand (atAnalysis (ch), 0))
                {
                    R.note (quoted (t.name) + ": its EQ band 1 is in use: " + why + " (a low cut at " + hzText (hz) + " there, on the EQ page).");
                    return false;
                }
                band = 0;
            }
            Change c;
            c.hash = ch.hash;
            c.name = t.name;
            c.role = t.role;
            c.kind = "eq";
            c.page = pageKey (Page::eq);
            c.from = on && shapeOf (now, band) == 3 ? pv (now, proc::eqFreq (band)) : 0.0;
            c.to = hz;
            c.hz = hz;
            c.proc = { { proc::eqOn, 1.0f }, { proc::eqType (band), 3.0f }, { proc::eqFreq (band), static_cast<float> (hz) }, { proc::eqQ (band), 0.7f } };
            c.fromText = on && shapeOf (now, band) == 3 ? "Low cut " + hzText (pv (now, proc::eqFreq (band))) : std::string ("Off");
            c.toText = "Low cut " + hzText (hz);
            c.why = why;
            c.did = "added a low cut at " + hzText (hz);
            c.heardPhrase = heardPhrase;
            R.push (std::move (c));
            return true;
        }

        // ---- EQ page ----

        void eqRules (Rules& R, const TrackReport& t, const ChannelNow& ch)
        {
            const auto role = t.role;
            if (role == Role::fx)
            {
                R.note (quoted (t.name) + " is an effect track: AUTO leaves its tone to you.");
                return;
            }
            const bool vocal = role == Role::vox || role == Role::adlibs;
            if (vocal && ch.voxology.on && ch.voxology.eq)
            {
                R.note (quoted (t.name) + ": Voxology's Tone EQ shapes this vocal, so Mixology leaves its EQ to Voxology (make tone changes there).");
                return;
            }
            const auto& s = R.r.style;
            const double all = rangeE (t.bands, 0, kBands - 1);

            // 1. A low cut by role: rumble it doesn't need (as Clean Up hears it), vocals always below 90 - 100 Hz; a kick,
            //    a bass / 808 and a whole beat only for rumble under their lowest notes (below about 30 Hz).
            if (role == Role::kick || role == Role::bass || role == Role::beat)
            {
                const auto& b = t.bands;
                if (b[0] > -100.0 && b[0] >= b[4] && b[0] >= b[2])   // rising towards 20 Hz: rumble, not a note
                {
                    const double hz = role == Role::kick ? 30.0 : 25.0;
                    lowCutFix (R, t, ch, hz, "there is rumble under its lowest notes (below 30 Hz): a low cut at " + hzText (hz) + " clears it without touching the notes",
                               "had rumble under its lowest notes");
                }
            }
            else if (all > 0.0)
            {
                const double share = rangeE (t.bands, 0, lowCutTopBand (role)) / all;
                const double hatsLow = rangeE (t.bands, 0, 9) / all;
                if (t.rumble || (vocal && share >= 0.03) || (role == Role::hats && hatsLow >= 0.03))
                {
                    const double hz = lowCutFor (role);
                    lowCutFix (R, t, ch, hz,
                               vocal ? "vocals have nothing useful below " + fmt0 (hz) + " Hz: a low cut keeps rumble and pops out"
                                     : fmt0 ((role == Role::hats ? hatsLow : share) * 100.0) + "% of its energy is below " + hzText (role == Role::hats ? 160.0 : lowCutTopHz (role))
                                           + ", where it only muddies the kick and bass",
                               "had rumble below " + hzText (hz));
                }
            }

            // 2. Mud: 250 - 500 Hz heavy for the kind of track and the style (a style with a deep low-mid scoop takes less).
            {
                const double mid = level15 (t.bands, 15, 20);
                double over = -99.0;
                std::string what;
                double cap = 4.0;
                if (role == Role::beat)
                {
                    over = (level15 (t.bands, 11, 14) - mid) - (targetLevel (s, 11, 14) - targetLevel (s, 15, 20)) - 3.0;
                    what = "the beat has " + fmt1 (over + 3.0) + " dB more 250 - 500 Hz than " + s.name + " usually does";
                    cap = 3.0;
                }
                else if (role == Role::bass)
                {
                    over = plainLevel (t.bands, 11, 14) - plainLevel (t.bands, 4, 8) + 6.0;
                    what = "its low mids (250 - 500 Hz) are nearly as loud as its low notes";
                    cap = 3.0;
                }
                else if (role == Role::keys || role == Role::guitar || role == Role::synth || role == Role::perc || role == Role::other || role == Role::adlibs)
                {
                    const double thr = (role == Role::keys ? 5.0 : role == Role::adlibs ? 7.0 : 6.0) - (s.scoop - 1.5) * 0.8;
                    const double mud = level15 (t.bands, 11, 14) - mid;
                    over = mud - thr;
                    what = "250 - 500 Hz is " + fmt1 (mud) + " dB over its mids: heavy for " + std::string (roleWords (role)) + " in " + s.name;
                }
                if (over > 0.0)
                {
                    const int b = loudestBand (t.bands, 11, 14);
                    Fix f;
                    f.job = Job::mud; f.from = 11; f.to = 14; f.band = 1; f.type = 0; f.hz = bandHz (b); f.q = 1.4;
                    f.gain = -std::clamp (0.5 * over + 1.5, 1.5, cap);
                    f.why = what + ": a dip there clears the mud";
                    f.didEq = "cut mud at " + hzText (bandHz (b)) + " ({db})";
                    f.heard = "muddy";
                    f.byStyle = role != Role::bass;
                    eqFix (R, t, ch, f);
                }
            }

            // 3. Box: a boxy kick (315 - 500 Hz close to its weight), a boxy snare (400 - 800 Hz over its body and crack).
            if (role == Role::kick && t.bands.size() == static_cast<std::size_t> (kBands))
            {
                // A typical kick sits ~15 dB lower there than at its weight (50 - 100 Hz).
                const double box = plainLevel (t.bands, 12, 14) - plainLevel (t.bands, 4, 7);
                if (box > -8.0)
                {
                    const int b = loudestBand (t.bands, 11, 14);
                    Fix f;
                    f.job = Job::mud; f.from = 11; f.to = 14; f.band = 2; f.type = 0; f.hz = bandHz (b); f.q = 1.4;
                    f.gain = -std::clamp ((box + 12.0) * 0.4, 1.5, 3.0);
                    f.why = "the kick sounds boxy (a lot of " + hzText (bandHz (b)) + "): a dip there lets its thump and click through";
                    f.didEq = "took the box out at " + hzText (bandHz (b)) + " ({db})";
                    f.heard = "boxy";
                    eqFix (R, t, ch, f);
                }
            }
            if (role == Role::snare && t.bands.size() == static_cast<std::size_t> (kBands))
            {
                const double box = plainLevel (t.bands, 13, 16) - std::max (plainLevel (t.bands, 9, 11), plainLevel (t.bands, 21, 23));
                if (box > -3.0)
                {
                    const int b = loudestBand (t.bands, 13, 16);
                    Fix f;
                    f.job = bandHz (b) < 650.0 ? Job::mud : Job::mid; f.from = 13; f.to = 16; f.band = 2; f.type = 0; f.hz = bandHz (b); f.q = 1.4;
                    f.gain = -std::clamp (0.5 * (box + 3.0) + 1.5, 1.5, 3.0);
                    f.why = "the snare sounds boxy (as much " + hzText (bandHz (b)) + " as its body and crack): a dip there opens it up";
                    f.didEq = "took the box out at " + hzText (bandHz (b)) + " ({db})";
                    f.heard = "boxy";
                    eqFix (R, t, ch, f);
                }
            }

            // 4. Harshness: a vocal whose presence stands far over its body; a sharp peak at 2.5 - 10 kHz on an instrument.
            if (role == Role::vox)
            {
                const double clarity = tilted (t.bands, 20, 24) - tilted (t.bands, 10, 16);
                if (clarity > 5.0)
                {
                    const int b = loudestBand (t.bands, 21, 24);
                    Fix f;
                    f.job = Job::presence; f.from = std::max (20, b - 1); f.to = std::min (24, b + 1); f.band = 3; f.type = 0; f.hz = bandHz (b); f.q = 2.0;
                    f.gain = -std::clamp ((clarity - 3.0) * 0.5, 1.0, 3.0);
                    f.why = "the vocal is harsh around " + hzText (bandHz (b)) + " (presence " + fmt1 (clarity) + " dB over its body): a narrow dip smooths it";
                    f.didEq = "softened a harsh spot at " + hzText (bandHz (b)) + " ({db})";
                    f.heard = "harsh";
                    eqFix (R, t, ch, f);
                }
            }
            else if (role != Role::kick && role != Role::bass && role != Role::beat && t.bands.size() == static_cast<std::size_t> (kBands))
            {
                double top = -200.0;
                for (int b = 15; b <= 29; ++b) top = std::max (top, t.bands[static_cast<std::size_t> (b)]);
                int peakBand = -1;
                double peak = 0.0;
                for (int b = 21; b <= 27; ++b)
                {
                    const auto& x = t.bands;
                    const double around = toDb ((toE (x[static_cast<std::size_t> (b - 2)]) + toE (x[static_cast<std::size_t> (b - 1)])
                                                 + toE (x[static_cast<std::size_t> (b + 1)]) + toE (x[static_cast<std::size_t> (b + 2)])) / 4.0);
                    const double d = x[static_cast<std::size_t> (b)] - around;
                    if (d > peak && x[static_cast<std::size_t> (b)] >= top - 6.0) { peak = d; peakBand = b; }
                }
                if (peakBand >= 0 && peak >= 5.0)
                {
                    const double hz = bandHz (peakBand);
                    Fix f;
                    f.job = hz < 6500.0 ? Job::presence : Job::air; f.from = peakBand - 1; f.to = peakBand + 1; f.band = hz < 6500.0 ? 3 : 4; f.type = 0; f.hz = hz; f.q = 2.0;
                    f.gain = -std::clamp (peak - 3.0, 2.0, 4.0);
                    f.why = std::string ("a sharp peak at ") + hzText (hz) + " stands " + fmt1 (peak) + " dB over the bands around it: a narrow dip smooths it";
                    f.didEq = "softened a harsh peak at " + hzText (hz) + " ({db})";
                    f.heard = "harsh";
                    eqFix (R, t, ch, f);
                }
            }

            // 5. A dull or bright top, a boomy low end, a dull vocal or snare: the fixes the Tone page can make too. Together
            //    (Polish, Auto Mix) the EQ makes the ones that suit it and leaves the others to Tone Balance.
            bool fits = true;
            for (const auto& f : tonalFixes (R, t))
            {
                fits = false;
                if (! f.onEq || (R.joint && f.pref != Page::eq && f.onTone)) continue;
                eqFix (R, t, ch, f);
            }
            if (role == Role::vox && fits)
            {
                const double body = tilted (t.bands, 10, 16), pres = tilted (t.bands, 20, 24), air = tilted (t.bands, 26, 28);
                if (pres - body <= 5.0)
                    R.note (quoted (t.name) + " already sounds balanced (presence " + signedDb (pres - body) + " against its body, air "
                            + signedDb (air - pres) + " against its presence): no EQ needed.");
            }
            if (role == Role::bass && plainLevel (t.bands, 8, 13) - plainLevel (t.bands, 2, 5) < -24.0)
                R.note (quoted (t.name) + " is almost all sub (under 60 Hz): on phones and small speakers it can disappear. Harmonics (Tone page) makes it "
                                          "heard there; AUTO leaves that to your taste.");
            (void) s;
        }

        // ---- Dynamics page (never the Limiter on a track) ----

        void dynRules (Rules& R, const TrackReport& t, const ChannelNow& ch)
        {
            const auto role = t.role;
            const bool vocal = role == Role::vox || role == Role::adlibs;
            const auto& now = R.procOf (ch);
            const auto& vx = ch.voxology;
            auto on = [&now] (int id) { return pv (now, id) > 0.5f; };

            // 1. A compressor where the level swings; Multiband for a bass whose low end jumps more than the rest.
            double swing = 99.0;
            if (vocal) swing = 8.0;
            else if (role == Role::bass) swing = 7.0;
            else if (role == Role::keys || role == Role::guitar || role == Role::synth) swing = 9.0;
            else if (role == Role::beat) swing = 10.0;
            const bool lowJumps = role == Role::bass && t.lowRange >= 9.0 && t.lowRange >= t.levelRange + 2.0;
            if (vocal && vx.on && vx.compressor)
                R.note (quoted (t.name) + ": Voxology compresses it already" + (vx.compressionDb >= 0.5 ? " (about " + fmt0 (vx.compressionDb) + " dB lately)" : std::string())
                        + ", so Mixology adds no second compressor.");
            else if (lowJumps && on (proc::multiOn) && std::abs (pv (now, proc::multiLow) - 45.0f) < 1.0f && std::abs (pv (now, proc::multiX1) - 120.0f) < 1.0f) {}   // in place
            else if (lowJumps && ! on (proc::multiOn) && ! on (proc::compOn))
            {
                Change c;
                c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "multi"; c.page = pageKey (Page::dynamics);
                c.proc = { { proc::multiOn, 1.0f }, { proc::multiBands, 0.0f }, { proc::multiX1, 120.0f }, { proc::multiX2, 2500.0f },
                           { proc::multiLow, 45.0f }, { proc::multiMid, 10.0f }, { proc::multiHigh, 0.0f } };
                c.fromText = "Off";
                c.toText = "Lows held 45 %";
                c.why = "its low end swings " + fmt0 (t.lowRange) + " dB from note to note (more than the rest of it): Multiband holds the lows steady and leaves the top alone";
                c.did = "held its low end steady (Multiband)";
                c.heardPhrase = "jumped " + fmt0 (t.lowRange) + " dB in the lows from note to note";
                R.push (std::move (c));
            }
            else if (t.levelRange >= swing && t.loudPeak > -60.0)
            {
                struct C { double ratio, below, att, rel, makeup; };
                const C k = vocal ? C { 3.0, 6.0, 8.0, 90.0, 2.0 } : role == Role::bass ? C { 4.0, 5.0, 25.0, 150.0, 2.0 }
                          : role == Role::beat ? C { 1.5, 4.0, 30.0, 200.0, 0.5 } : C { 2.5, 5.0, 15.0, 140.0, 1.5 };
                const double thr = std::clamp (std::round (t.loudPeak - k.below), -50.0, -3.0);
                const bool same = on (proc::compOn) && std::abs (pv (now, proc::compThr) - thr) < 0.5 && std::abs (pv (now, proc::compRatio) - k.ratio) < 0.05;
                if (same) {}   // in place (AUTO's own)
                else if (on (proc::compOn) || on (proc::multiOn))
                    R.note (quoted (t.name) + " swings " + fmt0 (t.levelRange) + " dB but has a compressor on already: yours stays as it is.");
                else
                {
                    Change c;
                    c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "comp"; c.page = pageKey (Page::dynamics);
                    c.proc = { { proc::compOn, 1.0f }, { proc::compThr, static_cast<float> (thr) }, { proc::compRatio, static_cast<float> (k.ratio) },
                               { proc::compAtt, static_cast<float> (k.att) }, { proc::compRel, static_cast<float> (k.rel) }, { proc::compMakeup, static_cast<float> (k.makeup) },
                               { proc::compKnee, 0.0f } };
                    c.fromText = "Off";
                    c.toText = (k.ratio < 2.0 ? fmt1 (k.ratio) : fmt0 (k.ratio)) + ":1 from " + signedDb (thr);
                    c.why = vocal ? "its level swings " + fmt0 (t.levelRange) + " dB between quiet and loud lines: a compressor (about 4 dB on the loud parts) keeps every word in front"
                          : role == Role::bass ? "its level swings " + fmt0 (t.levelRange) + " dB from note to note: a compressor keeps the low end steady under the song"
                          : role == Role::beat ? "the beat swings " + fmt0 (t.levelRange) + " dB between its parts: a gentle glue (1.5:1) evens it a little"
                                               : "its level swings " + fmt0 (t.levelRange) + " dB between its parts: a gentle compressor keeps it steady in the mix";
                    c.did = role == Role::beat ? "added a gentle glue (1.5:1)" : "added a gentle compressor (" + c.toText + ")";
                    c.heardPhrase = "swung " + fmt0 (t.levelRange) + " dB in level";
                    R.push (std::move (c));
                }
            }

            // 2. The Transient shaper on a kick or snare whose hits are squashed (low punch) or very spiky.
            if (role == Role::kick || role == Role::snare)
            {
                const double low = role == Role::kick ? 7.5 : 8.0, high = role == Role::kick ? 19.0 : 20.0;
                const bool squashed = t.crest > 0.0 && t.crest < low, spiky = t.crest > high;
                const float attack = squashed ? 25.0f : -20.0f;
                if ((squashed || spiky) && on (proc::transOn) && std::abs (pv (now, proc::transAttack) - attack) < 0.5f) {}   // in place
                else if ((squashed || spiky) && on (proc::transOn))
                    R.note (quoted (t.name) + ": its Transient shaper is on already: yours stays as it is.");
                else if (squashed || spiky)
                {
                    Change c;
                    c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "trans"; c.page = pageKey (Page::dynamics);
                    c.proc = { { proc::transOn, 1.0f }, { proc::transAttack, attack }, { proc::transSustain, 0.0f }, { proc::transSpeed, 10.0f },
                               { proc::transOut, 0.0f }, { proc::transMix, 100.0f }, { proc::transFocus, 0.0f } };
                    c.fromText = "Off";
                    c.toText = std::string ("Attack ") + (squashed ? "+25 %" : "-20 %");
                    c.why = squashed ? "its hits are squashed (only " + fmt1 (t.crest) + " dB from peak to body): more attack brings the punch back"
                                     : "its hits are very spiky (" + fmt1 (t.crest) + " dB from peak to body): a little less attack keeps them from poking out";
                    c.did = squashed ? "brought back the front of each hit (Attack +25 %)" : "softened the front of each hit (Attack -20 %)";
                    c.heard = squashed ? "squashed" : "spiky";
                    R.push (std::move (c));
                }
            }

            // 3. A gate (drums) or a gentle expander (vocals, guitar) only where Analyze Mix heard a steady noise floor
            //    between the phrases (hiss, hum, bleed); a track that is silent between them never gets one.
            const bool gateable = vocal || role == Role::guitar || role == Role::kick || role == Role::snare || role == Role::perc;
            if (gateable && ! (vocal && vx.on) && t.floorMoments >= 10 && t.floorSpread <= 6.0 && t.floorRms > -90.0 && t.floorRms <= -45.0
                && t.activeRms - t.floorRms >= 30.0)
            {
                const bool drums = role == Role::kick || role == Role::snare || role == Role::perc;
                const double thr = std::clamp (std::round (std::min (t.floorPeak + 6.0, t.activeRms - 20.0)), -80.0, -20.0);
                if (on (proc::gateOn) && std::abs (pv (now, proc::gateThr) - thr) < 0.5) {}   // in place
                else if (on (proc::gateOn)) R.note (quoted (t.name) + ": its gate is on already: yours stays as it is.");
                else
                {
                    Change c;
                    c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "gate"; c.page = pageKey (Page::dynamics);
                    c.proc = { { proc::gateOn, 1.0f }, { proc::gateMode, drums ? 0.0f : 1.0f }, { proc::gateThr, static_cast<float> (thr) },
                               { proc::gateRange, drums ? 18.0f : 12.0f }, { proc::gateAtt, drums ? 0.5f : 1.0f }, { proc::gateHold, drums ? 40.0f : 60.0f },
                               { proc::gateRel, drums ? 120.0f : 200.0f } };
                    c.fromText = "Off";
                    c.toText = std::string (drums ? "Gate" : "Expander") + " from " + signedDb (thr);
                    c.why = "between its " + std::string (drums ? "hits" : "phrases") + " Mixology heard a steady noise floor about " + fmt0 (t.activeRms - t.floorRms)
                            + " dB under it (hiss, hum or bleed): " + (drums ? "a gate" : "a gentle expander") + " turns it down there";
                    c.did = drums ? "added a gate between hits" : "added a gentle expander between phrases";
                    c.heardPhrase = "had noise between its " + std::string (drums ? "hits" : "phrases");
                    R.push (std::move (c));
                }
            }
        }

        // ---- Space page ----

        /** The lead vocal's reverb (the rule Polish has had since 0.7): on an FX channel with a Link (100 % wet, its level
            under the vocal, ducking while it sings), else a little on the vocal itself. onlyOn != 0: only a change on
            that channel counts (AUTO on that FX channel's Space page). */
        void leadSpace (Rules& R, std::uint32_t onlyOn)
        {
            const auto& r = R.r;
            const auto* lead = R.lead;
            if (lead == nullptr) return;
            const auto* leadCh = R.look.of (*lead);
            auto spaceOn = [&R] (const ChannelNow& c) { return c.hasProc && proc::spaceEffects (R.procOf (c)) != 0; };
            std::vector<const ChannelNow*> fx;
            for (const auto& c : R.channels)
                if (c.hasLink && c.hasProc && ! c.locked && ! c.sharedName && c.hash != lead->hash && isFxReturn (c, r)) fx.push_back (&c);
            const ChannelNow* busy = nullptr;
            for (const auto* c : fx) if (spaceOn (*c)) busy = c;
            const double under = reverbUnderVocal (r.style.key);
            const bool leadHeard = counts (*lead);
            const bool voxReverb = leadCh != nullptr && leadCh->voxology.on && leadCh->voxology.reverb;
            const auto leadSpaceNow = leadCh != nullptr && leadCh->hasProc ? proc::spaceEffects (R.procOf (*leadCh)) : 0u;
            const auto busyReverbs = busy != nullptr ? reverbsOf (proc::spaceEffects (R.procOf (*busy))) : 0u;
            auto mine = [onlyOn] (std::uint32_t hash) { return onlyOn == 0 || onlyOn == hash; };
            if (leadSpaceNow != 0 && voxReverb && reverbsOf (leadSpaceNow) != 0)
                R.note (quoted (lead->name) + " gets reverb from Voxology and from its own Space (" + lowerNames (reverbsOf (leadSpaceNow))
                        + "): one space is usually enough. Switch one of them off.");
            else if (leadSpaceNow != 0)
                R.note (quoted (lead->name) + " has its own Space already (" + lowerNames (leadSpaceNow) + "): Mixology leaves it.");
            else if (voxReverb && busyReverbs != 0)
                R.note (quoted (lead->name) + " gets reverb from Voxology and from the FX channel " + quoted (busy->name)
                        + " (" + lowerNames (busyReverbs) + "): one space is usually enough. Switch one of them off.");
            else if (voxReverb)
                R.note (quoted (lead->name) + " has Voxology's reverb: Mixology adds no second reverb.");
            else if (busy != nullptr)
            {
                // An FX channel effect already: its kind stays yours, but its level is checked against the vocal.
                const auto* fxT = reportOf (r, busy->hash);
                const auto& at = atAnalysis (*busy);
                const bool fxRoute = pv (at, proc::spRoute) > 0.5f && pv (at, proc::spOn) > 0.5f && pv (at, proc::chainOn) > 0.5f;
                if (fxRoute && leadHeard && fxT != nullptr && counts (*fxT) && ! busy->locked)
                {
                    const double now = fxT->loudness - lead->loudness;   // how far the effect sits under the vocal
                    const double levelAt = pv (at, proc::spLevel);
                    const double want = std::clamp (std::round (levelAt + (under - now)), -30.0, 6.0);
                    if (std::abs (now - under) > 3.0 && std::abs (want - pv (R.procOf (*busy), proc::spLevel)) >= 1.0)
                    {
                        if (! mine (busy->hash)) return;
                        Change c;
                        c.hash = busy->hash;
                        c.name = busy->name;
                        c.role = Role::fx;
                        c.kind = "space";
                        c.page = pageKey (Page::space);
                        c.proc = { { proc::spLevel, static_cast<float> (want) } };
                        c.fromText = "Level " + signedDb (pv (R.procOf (*busy), proc::spLevel));
                        c.toText = "Level " + signedDb (want);
                        c.why = "the effect comes back " + fmt0 (std::abs (now)) + " dB " + (now > 0.0 ? "over" : "under") + " " + quoted (lead->name)
                                + "; for " + r.style.name + " a vocal's reverb usually sits about " + fmt0 (-under) + " dB under it";
                        c.did = "set the vocal reverb on " + quoted (busy->name) + " (the FX channel) to " + fmt0 (-under) + " dB under " + quoted (lead->name);
                        c.heardPhrase = std::string ("had its reverb too ") + (now > under ? "loud" : "quiet") + " (" + fmt0 (std::abs (now)) + " dB "
                                        + (now > 0.0 ? "over" : "under") + " the vocal)";
                        c.byStyle = true;
                        R.push (std::move (c));
                    }
                    else
                        R.note ("The FX channel " + quoted (busy->name) + " has its Space already (" + lowerNames (proc::spaceEffects (R.procOf (*busy)))
                                + "), at a good level under the vocal: Mixology leaves it.");
                }
                else
                    R.note ("The FX channel " + quoted (busy->name) + " has its Space already (" + lowerNames (proc::spaceEffects (R.procOf (*busy)))
                            + "): Mixology leaves your effects alone.");
            }
            else if (! fx.empty())
            {
                const ChannelNow* target = fx.front();
                for (const auto* c : fx)
                    if (std::regex_search (c->name, std::regex (R"(verb|plate|hall)", std::regex::icase))) { target = c; break; }
                if (! mine (target->hash))
                {
                    R.note ("The lead vocal's reverb goes on " + quoted (target->name) + " (AUTO there, or on " + quoted (lead->name) + "'s Space page).");
                    return;
                }
                const auto snd = vocalReverb (r.style.key, true);
                Change c;
                c.hash = target->hash;
                c.name = target->name;
                c.role = Role::fx;
                c.kind = "space";
                c.page = pageKey (Page::space);
                c.proc = spaceAlone (static_cast<proc::SpaceType> (snd.type),
                                     { { proc::spRoute, 1.0f }, { proc::spLowCut, 200.0f }, { proc::spHighCut, 9000.0f }, { proc::duckAmount, 6.0f } });
                c.proc.insert (c.proc.end(), snd.params.begin(), snd.params.end());
                c.setDuck = true;
                c.duck = { lead->hash };
                // Its level: what the FX channel passed while Mixology listened (the vocal's send, dry, its effect
                // still off) tells how much of the vocal goes in; else a send at 0 dB is assumed.
                const auto* fxT = reportOf (r, target->hash);
                const bool sendHeard = fxT != nullptr && counts (*fxT) && leadHeard && pv (atAnalysis (*target), proc::spOn) < 0.5f;
                const double sendDb = sendHeard ? fxT->loudness - lead->loudness : 0.0;
                const double level = std::clamp (std::round (under - (sendDb + wetGainDb (snd.type))), -30.0, 6.0);
                c.proc.push_back ({ proc::spLevel, static_cast<float> (level) });
                c.fromText = "Off";
                c.toText = std::string (snd.type == static_cast<int> (proc::SpaceType::hall) ? "Hall" : "Vocal Plate") + ", level " + signedDb (level);
                c.why = "the lead vocal's reverb: " + snd.words + " on this FX channel, about " + fmt0 (-under) + " dB under " + quoted (lead->name)
                        + ", ducking 6 dB while it sings so the words stay clear";
                c.did = "set up " + snd.words + " on " + quoted (target->name) + " (the FX channel), ducking under " + quoted (lead->name)
                        + (sendHeard ? std::string() : " (send " + quoted (lead->name) + " to it in {daw}: {daw:sendHow})");
                c.heardPhrase = "was dry";
                c.byStyle = true;
                R.push (std::move (c));
                if (! sendHeard)
                    R.note ("Send " + quoted (lead->name) + " to " + quoted (target->name) + " ({daw:sendHow}): Mixology can't add sends. "
                            "The effect's LEVEL (Space page) sets how much comes back.");
            }
            else if (leadCh != nullptr && leadCh->hasLink && leadCh->hasProc && ! leadCh->locked && ! leadCh->sharedName && pv (R.procOf (*leadCh), proc::chainOn) > 0.5f)
            {
                if (! mine (leadCh->hash)) return;
                const auto snd = vocalReverb (r.style.key, false);
                Change c;
                c.hash = leadCh->hash;
                c.name = lead->name;
                c.role = lead->role;
                c.kind = "space";
                c.page = pageKey (Page::space);
                c.proc = spaceAlone (static_cast<proc::SpaceType> (snd.type), { { proc::spRoute, 0.0f }, { proc::spLowCut, 200.0f }, { proc::spHighCut, 9000.0f } });
                c.proc.insert (c.proc.end(), snd.params.begin(), snd.params.end());
                c.fromText = "Dry";
                c.toText = snd.type == static_cast<int> (proc::SpaceType::hall) ? "Hall, mix 10 %" : "Vocal Plate, mix 12 %";
                c.why = snd.words + " at a low mix puts the vocal in the same room as the beat";
                c.did = "added " + snd.words + " at a low mix";
                c.heardPhrase = "was dry";
                c.byStyle = true;
                R.push (std::move (c));
                R.note ("For the cleanest vocal reverb, use an FX channel with Mixology Link and a Send (manual: Space); Auto Mix then sets it up there.");
            }
        }

        /** A small space for an instrument, by the style: trap keeps it all dry, hip hop nearly dry, pop / R&B / worship
            wetter, rock rooms. Nothing (and why) for the kinds that stay dry. */
        struct Ambience { proc::SpaceType type = proc::SpaceType::room; double mix = 0.0, decay = 0.0; std::string words; };
        std::optional<Ambience> ambienceFor (Role role, const std::string& genre, std::string& whyNot)
        {
            using T = proc::SpaceType;
            switch (role)
            {
                case Role::kick: whyNot = "a kick stays dry: reverb blurs its punch and muddies the low end"; return std::nullopt;
                case Role::bass: whyNot = "the bass / 808 stays dry: reverb on the low end turns it to mud"; return std::nullopt;
                case Role::hats: whyNot = "hats stay dry: reverb on them washes the groove out"; return std::nullopt;
                case Role::beat: whyNot = "a whole beat comes mixed with its own space: AUTO adds none on top"; return std::nullopt;
                case Role::other: whyNot = "Mixology doesn't know what it plays: set its role on the Tracks page and AUTO can give it a space"; return std::nullopt;
                default: break;
            }
            if (genre == "trap") { whyNot = "trap keeps the beat dry and close (the vocal's reverb gives the space)"; return std::nullopt; }
            auto room = [] (double mix, double decay) { return Ambience { T::room, mix, decay, "a small room" }; };
            auto plate = [] (double mix, double decay) { return Ambience { T::reverb, mix, decay, "a short plate" }; };
            auto hall = [] (double mix, double decay) { return Ambience { T::hall, mix, decay, "a hall" }; };
            const bool wet = genre == "gospel" || genre == "worship" || genre == "cinematic";
            const bool band = genre == "rock";
            const bool acoustic = genre == "country" || genre == "folk" || genre == "jazz";
            switch (role)
            {
                case Role::snare:
                    if (genre == "hiphop") return plate (6.0, 0.9);
                    if (band) return room (14.0, 0.8);
                    if (acoustic) return room (12.0, 0.8);
                    if (wet) return plate (12.0, 1.4);
                    return plate (genre == "electronic" ? 8.0 : 10.0, 1.2);
                case Role::keys:
                    if (genre == "hiphop") return room (8.0, 0.6);
                    if (band) return room (12.0, 0.9);
                    if (acoustic) return room (12.0, 1.0);
                    if (wet) return hall (12.0, 2.4);
                    return plate (12.0, 1.5);
                case Role::guitar:
                    if (genre == "hiphop") return room (8.0, 0.6);
                    if (band || acoustic) return room (10.0, 0.8);
                    if (wet) return hall (10.0, 2.2);
                    return plate (10.0, 1.4);
                case Role::synth:
                    if (genre == "hiphop") { whyNot = "in hip hop the synths usually bring their own space"; return std::nullopt; }
                    if (band) return room (8.0, 0.9);
                    if (acoustic) return plate (8.0, 1.4);
                    if (wet) return hall (12.0, 2.6);
                    return plate (10.0, 1.5);
                case Role::perc:
                    if (genre == "hiphop" || genre == "rnb" || genre == "pop" || genre == "electronic") { whyNot = "percussion stays dry and tight in " + genre + " styles"; return std::nullopt; }
                    return room (wet ? 8.0 : 10.0, 0.7);
                default: break;
            }
            return std::nullopt;
        }

        void spaceRules (Rules& R, const TrackReport& t, const ChannelNow& ch)
        {
            const auto role = t.role;
            const auto& now = R.procOf (ch);
            const auto effects = proc::spaceEffects (now);
            if (R.lead != nullptr && R.lead->hash == t.hash) { leadSpace (R, 0); return; }
            if (role == Role::fx || isFxReturn (ch, R.r))
            {
                if (R.lead != nullptr) leadSpace (R, ch.hash);
                else R.note (quoted (t.name) + " is an FX channel: it plays what you send to it. With no lead vocal heard, AUTO sets nothing here.");
                return;
            }
            if (role == Role::vox)
            {
                R.note (quoted (t.name) + " sings with the lead: send it to the lead's reverb (its FX channel) so they share one room.");
                return;
            }
            if (role == Role::adlibs)
            {
                const bool delays = R.urban() || R.genre == "pop" || R.genre == "electronic";
                if (effects != 0)
                {
                    // Its own Space already (yours): left alone. A delay there too while Voxology adds one: two delays.
                    if (ch.voxology.on && ch.voxology.delay && (effects & proc::bitOf (proc::SpaceType::delay)) != 0)
                        R.note (quoted (t.name) + " gets a delay from Voxology and from its own Space: one is usually enough. Switch one of them off.");
                    else R.note (quoted (t.name) + " has its own Space already (" + lowerNames (effects) + "): Mixology leaves it.");
                    return;
                }
                if (! delays)
                {
                    R.note (quoted (t.name) + ": in " + R.r.style.name + " ad-libs and backing vocals usually share the lead's reverb: send them to its FX channel.");
                    return;
                }
                if (ch.voxology.on && ch.voxology.delay)
                {
                    R.note (quoted (t.name) + " has Voxology's delay already: Mixology adds no second one.");
                    return;
                }
                if (R.joint && R.adlibDelays >= 3) return;
                Change c;
                c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "space"; c.page = pageKey (Page::space);
                c.proc = spaceAlone (proc::SpaceType::delay, { { proc::spRoute, 0.0f }, { proc::spLowCut, 300.0f }, { proc::spHighCut, 8000.0f },
                                                               { proc::dlyTime, 3.0f }, { proc::dlyFeedback, 30.0f }, { proc::dlyTone, 5000.0f },
                                                               { proc::dlyPingPong, 100.0f }, { proc::dlyMix, 15.0f } });
                c.fromText = "Dry";
                c.toText = "Ping-pong 1/8, mix 15 %";
                c.why = "ad-libs that echo left and right on the beat fill the gaps around the lead without covering it";
                c.did = "added a ping-pong delay on the 1/8 (mix 15 %)";
                c.heardPhrase = "was dry";
                c.byStyle = true;
                R.push (std::move (c));
                ++R.adlibDelays;
                return;
            }
            std::string whyNot;
            const auto amb = ambienceFor (role, R.genre, whyNot);
            if (! amb)
            {
                R.note (quoted (t.name) + ": " + whyNot + ".");
                return;
            }
            if (effects != 0)
            {
                R.note (quoted (t.name) + " has its own Space already (" + lowerNames (effects) + "): Mixology leaves it.");
                return;
            }
            const auto& a = *amb;
            std::vector<std::pair<int, float>> params { { proc::spRoute, 0.0f }, { proc::spLowCut, 250.0f }, { proc::spHighCut, 9000.0f } };
            const auto mix = static_cast<float> (a.mix);
            const auto decay = static_cast<float> (a.decay);
            if (a.type == proc::SpaceType::room)
                params.insert (params.end(), { { proc::roomPre, 6.0f }, { proc::roomDecay, decay }, { proc::roomSize, 40.0f }, { proc::roomLate, 55.0f }, { proc::roomMix, mix } });
            else if (a.type == proc::SpaceType::hall)
                params.insert (params.end(), { { proc::hallPre, 25.0f }, { proc::hallDecay, decay }, { proc::hallSize, 70.0f }, { proc::hallDamp, 5500.0f }, { proc::hallMix, mix } });
            else
                params.insert (params.end(), { { proc::revPre, 15.0f }, { proc::revDecay, decay }, { proc::revDamp, 7000.0f }, { proc::revWidth, 100.0f }, { proc::revMix, mix } });
            Change c;
            c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "space"; c.page = pageKey (Page::space);
            c.proc = spaceAlone (a.type, params);
            c.fromText = "Dry";
            const std::string kind = a.type == proc::SpaceType::room ? "Room" : a.type == proc::SpaceType::hall ? "Hall" : "Plate";
            c.toText = kind + " " + fmt1 (a.decay) + " s, mix " + fmt0 (a.mix) + " %";
            c.why = a.words + " (" + fmt1 (a.decay) + " s, mix " + fmt0 (a.mix) + " %) puts " + std::string (roleWords (role)) + " in the same space as the song, as "
                    + R.r.style.name + " usually has it";
            c.did = "gave it " + a.words + " (" + fmt0 (a.mix) + " % mix)";
            c.heardPhrase = "was dry";
            c.byStyle = true;
            R.push (std::move (c));
        }

        // ---- Tone page: Tone Balance only (Warmth, Presence, Air); never Harmonics, Analog, Clip or Loudness ----

        void toneRules (Rules& R, const TrackReport& t, const ChannelNow& ch)
        {
            const auto role = t.role;
            if (role == Role::fx) { R.note (quoted (t.name) + " is an effect track: AUTO leaves its tone to you."); return; }
            if ((role == Role::vox || role == Role::adlibs) && ch.voxology.on && ch.voxology.eq)
            {
                R.note (quoted (t.name) + ": Voxology's Tone EQ shapes this vocal, so Mixology leaves its tone to Voxology.");
                return;
            }
            if (role == Role::kick) { R.note (quoted (t.name) + ": a kick's tone is its EQ (weight and click) and its Transient: Tone Balance has nothing to add."); return; }
            if (role == Role::bass)
            {
                if (plainLevel (t.bands, 8, 13) - plainLevel (t.bands, 2, 5) < -24.0)
                    R.note (quoted (t.name) + " is almost all sub (under 60 Hz): on phones it can disappear. Harmonics (on this page) makes it heard there; "
                                              "AUTO leaves that to your taste.");
                else R.note (quoted (t.name) + ": a bass's tone is its notes: Tone Balance has nothing to add.");
                return;
            }
            for (const auto& f : tonalFixes (R, t))
                if (f.onTone) toneFix (R, t, ch, f);
        }

        // ---- Stereo page: the track's Stereo module (never pans: Stereo Architect places the whole mix) ----

        void stereoRules (Rules& R, const TrackReport& t, const ChannelNow& ch)
        {
            const auto role = t.role;
            const auto& now = R.procOf (ch);
            const bool on = pv (now, proc::stOn) > 0.5f;
            if (ch.channels == 1)
            {
                R.note (quoted (t.name) + " is mono: widening can't open it. Use Stereo Architect (Overview) to place it.");
                return;
            }
            if (role == Role::vox) { R.note (quoted (t.name) + ": the lead vocal stays in the middle, never widened."); return; }
            if (role == Role::fx) { R.note (quoted (t.name) + " is an effect track: AUTO leaves its width to you."); return; }
            // The low end of a stereo kick, bass / 808 or a whole beat in the middle (it hits harder, stays solid in mono).
            if (role == Role::kick || role == Role::bass || role == Role::beat)
            {
                if (t.lowWidth <= 0.12) return;
                if (on && pv (now, proc::stLow) <= 5.0f) return;   // yours, with the lows in the middle already
                Change c;
                c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "width"; c.page = pageKey (Page::stereo);
                if (on) c.proc = { { proc::stLow, 0.0f } };   // your Stereo settings stay: only its Low Width
                else c.proc = { { proc::stOn, 1.0f }, { proc::stLow, 0.0f }, { proc::stLowX, role == Role::beat ? 120.0f : 150.0f } };
                c.fromText = "Lows " + fmt0 (t.lowWidth * 100.0) + " % on the sides";
                c.toText = "Lows in the middle";
                c.why = "its low end is spread in stereo: in the middle it hits harder and stays solid in mono and on club systems";
                c.did = "put its low end in the middle";
                c.heardPhrase = "had its lows spread wide";
                R.push (std::move (c));
                return;
            }
            const bool widen = role == Role::keys || role == Role::synth || role == Role::guitar || role == Role::adlibs || role == Role::other;
            // Very wide and falling apart in mono: a little narrower.
            if (t.width > 0.45 && t.correlation < 0.15 && role != Role::adlibs)
            {
                if (on && std::abs (pv (now, proc::stWidth) - 85.0f) < 2.5f) return;   // in place
                if (on) { R.note (quoted (t.name) + " is very wide (it falls apart in mono), but its Stereo module is yours: try Width 85 % there."); return; }
                Change c;
                c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "width"; c.page = pageKey (Page::stereo);
                c.proc = { { proc::stOn, 1.0f }, { proc::stWidth, 85.0f }, { proc::stLow, 100.0f }, { proc::stMid, 100.0f }, { proc::stHigh, 100.0f } };
                c.fromText = "Width 100 %";
                c.toText = "Width 85 %";
                c.why = "it is very wide (left and right hardly agree): a little narrower and it holds together in mono";
                c.did = "narrowed it a little (85 %)";
                c.heardPhrase = "was so wide it falls apart in mono";
                R.push (std::move (c));
                return;
            }
            if (! widen) return;
            if (t.width < 0.03)
            {
                R.note (quoted (t.name) + " plays the same left and right: widening can't open it. A stereo Chorus (Space page) can, or Stereo Architect can place it.");
                return;
            }
            if (t.width >= 0.10) return;
            // Its sides up so they make about 16 % of it, between 115 and 140 %; the lows stay as they are.
            const double w = t.width;
            const double factor = std::clamp (std::sqrt (0.16 * (1.0 - w) / (w * 0.84)), 1.15, 1.4);
            const double width = std::round (factor * 20.0) * 5.0;
            const double low = std::round (100.0 / (width / 100.0));
            if (on && std::abs (pv (now, proc::stWidth) - width) < 2.5) return;   // in place
            if (on) { R.note (quoted (t.name) + " is quite narrow, but its Stereo module is yours: AUTO leaves it."); return; }
            Change c;
            c.hash = ch.hash; c.name = t.name; c.role = role; c.kind = "width"; c.page = pageKey (Page::stereo);
            c.proc = { { proc::stOn, 1.0f }, { proc::stWidth, static_cast<float> (width) }, { proc::stLow, static_cast<float> (low) },
                       { proc::stMid, 100.0f }, { proc::stHigh, 100.0f } };
            c.fromText = "Width 100 %";
            c.toText = "Width " + fmt0 (width) + " %";
            c.why = "it is stereo but narrow (" + fmt0 (w * 100.0) + " % on the sides): a little wider gives the vocal more room in the middle";
            c.did = "opened it a little wider (" + fmt0 (width) + " %)";
            c.heard = "narrow";
            R.push (std::move (c));
        }

        void runPage (Rules& R, Page page, const TrackReport& t, const ChannelNow& ch)
        {
            switch (page)
            {
                case Page::eq: eqRules (R, t, ch); break;
                case Page::dynamics: dynRules (R, t, ch); break;
                case Page::space: spaceRules (R, t, ch); break;
                case Page::tone: toneRules (R, t, ch); break;
                case Page::stereo: stereoRules (R, t, ch); break;
            }
        }

        std::string planPolish (const Report& r, const Lookup& look, const std::vector<ChannelNow>& channels, const MixNow& mix, Plan& plan)
        {
            const auto first = plan.changes.size();
            Rules R (r, look, channels, plan, true);

            // ---- every track, every page: the same rules each page's AUTO uses
            for (const auto& t : r.tracks)
            {
                if (! counts (t) || t.bands.size() < static_cast<std::size_t> (kBands)) continue;
                const auto* ch = procChannel (t, look, plan, "its processing");
                if (ch == nullptr)
                {
                    if (R.lead != nullptr && R.lead->hash == t.hash) leadSpace (R, 0);   // its reverb can still go on an FX channel
                    continue;
                }
                for (Page p : { Page::eq, Page::dynamics, Page::space, Page::tone, Page::stereo })
                    if (p != Page::space || ! (t.role == Role::fx || isFxReturn (*ch, r))) runPage (R, p, t, *ch);
            }
            const bool leadSpaced = std::any_of (plan.changes.begin() + static_cast<std::ptrdiff_t> (first), plan.changes.end(), [&] (const Change& c) {
                return c.kind == "space" && R.lead != nullptr && (c.hash == R.lead->hash || c.role == Role::fx);
            });
            const bool leadVoxology = R.lead != nullptr && look.of (*R.lead) != nullptr && look.of (*R.lead)->voxology.on;
            if (leadSpaced && ! leadVoxology)   // Voxology tells Mixology what it does; other plug-ins don't
                plan.notes.push_back ("If another plug-in already gives the vocal its reverb, untick Mixology's: one space is enough.");

            // ---- Space layers: two reverb types on one track (Reverb, Hall, Room, Special FX) can wash it out
            {
                int shown = 0, more = 0;
                for (const auto& ch : channels)
                {
                    const auto rv = reverbsOf (spaceNow (ch));
                    if (! ch.hasLink || ch.sharedName || proc::effectCount (rv) < 2) continue;
                    if (shown++ >= 3) { ++more; continue; }
                    plan.notes.push_back ("Tip: " + quoted (ch.name) + " plays " + lowerNames (rv) + " together: stacking "
                                          + (proc::effectCount (rv) > 2 ? "three" : "two") + " spaces can wash the sound out. One is usually enough.");
                }
                if (more > 0) plan.notes.push_back (std::to_string (more) + (more == 1 ? " more track stacks" : " more tracks stack") + " spaces the same way.");
            }

            // ---- the Full Mix's tone against the style (Tone Balance on Mixology itself)
            if (const auto tone = toneAgainstStyle (r); mix.known && tone.valid)
            {
                const double lows = tone.low, lowMids = tone.lowMid, pres = tone.presence, air = tone.air;
                const auto& at = mix.atAnalysisKnown ? mix.atAnalysis : mix.now;
                struct Knob { int id; double diff; const char* word; };
                std::vector<std::pair<int, float>> set;
                std::vector<std::string> what, from, to;
                const bool tbOnAt = pv (at, proc::tbOn) > 0.5f, tbOnNow = pv (mix.now, proc::tbOn) > 0.5f;
                for (const auto& k : { Knob { proc::tbWarmth, (std::abs (lowMids) < 1.0 || (lows > 0) == (lowMids > 0)) ? lows : 0.0, "low end" },
                                       Knob { proc::tbPresence, pres, "presence" }, Knob { proc::tbAir, air, "air" } })
                {
                    if (std::abs (k.diff) <= 2.5) continue;
                    const double base = tbOnAt ? pv (at, k.id) : 0.0;
                    const double want = std::clamp (roundHalf (base - std::clamp (k.diff * 0.5, -2.0, 2.0)), -6.0, 6.0);
                    const double nowV = tbOnNow ? pv (mix.now, k.id) : 0.0;
                    if (std::abs (want - nowV) < 0.5) continue;
                    set.push_back ({ k.id, static_cast<float> (want) });
                    what.push_back (std::string (k.diff < 0 ? "short of " : "too much ") + k.word + " (" + fmt1 (std::abs (k.diff)) + " dB)");
                    from.push_back (toneLabel (k.id) + signedDb (nowV));
                    to.push_back (toneLabel (k.id) + signedDb (want));
                }
                if (! set.empty())
                {
                    Change c;
                    c.hash = 0;
                    c.name = "Full Mix";
                    c.role = Role::other;
                    c.kind = "tone";
                    c.fullMix = true;
                    c.proc = { { proc::tbOn, 1.0f } };
                    if (! tbOnNow)   // the knobs it doesn't move start from flat
                        for (int id : { proc::tbWarmth, proc::tbPresence, proc::tbAir })
                            if (std::none_of (set.begin(), set.end(), [id] (const auto& p) { return p.first == id; })) c.proc.push_back ({ id, 0.0f });
                    c.proc.insert (c.proc.end(), set.begin(), set.end());
                    c.fromText = joinWords (from, ", ");
                    c.toText = joinWords (to, ", ");
                    c.why = "against " + r.style.name + "'s tone your mix is " + joinWords (what, ", ") + ": a gentle Tone Balance on the whole mix (before Masterology)";
                    plan.changes.push_back (c);
                }
            }

            std::map<std::string, int> n;
            for (auto it = plan.changes.begin() + static_cast<std::ptrdiff_t> (first); it != plan.changes.end(); ++it) ++n[it->fullMix && it->kind == "tone" ? "mix" : it->kind];
            std::vector<std::string> parts;
            if (n["eq"]) parts.push_back ("EQ: " + std::to_string (n["eq"]));
            const int dyn = n["comp"] + n["multi"];
            if (dyn) parts.push_back (std::to_string (dyn) + " compressor" + (dyn == 1 ? "" : "s"));
            if (n["trans"]) parts.push_back ("transients: " + std::to_string (n["trans"]));
            if (n["gate"]) parts.push_back ("gates: " + std::to_string (n["gate"]));
            if (n["width"]) parts.push_back ("stereo: " + std::to_string (n["width"]));
            if (n["space"]) parts.push_back ("space: " + std::to_string (n["space"]));
            if (n["tone"]) parts.push_back ("Tone Balance: " + std::to_string (n["tone"]));
            if (n["mix"]) parts.push_back ("the mix's tone");
            if (parts.empty()) return "processing already fits";
            return joinWords (parts, ", ");
        }

        std::string joinAnd (const std::vector<std::string>& parts)
        {
            std::string out;
            for (std::size_t i = 0; i < parts.size(); ++i)
                out += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
            return out;
        }

        /** AUTO's one line: what it changed and what it heard, or why there is nothing to change. */
        std::string autoLine (const Plan& plan, const std::string& name, const Style& style)
        {
            if (plan.changes.empty())
            {
                // A reason about this track (Voxology, its own settings, why it stays dry...), else: it fits.
                for (const auto& n : plan.notes)
                    if (n.find (quoted (name)) != std::string::npos)
                        return n + (n.empty() || n.back() == '.' ? " " : ". ") + "Nothing to change.";
                return quoted (name) + " already fits: nothing to change.";
            }
            std::vector<std::string> did, adjectives, phrases;
            bool byStyle = false;
            auto add = [] (std::vector<std::string>& v, const std::string& s) { if (! s.empty() && std::find (v.begin(), v.end(), s) == v.end()) v.push_back (s); };
            for (const auto& c : plan.changes)
            {
                add (did, c.did.empty() ? c.why : c.did);
                add (adjectives, c.heard);
                add (phrases, c.heardPhrase);
                byStyle = byStyle || c.byStyle;
            }
            std::vector<std::string> heard;
            if (! adjectives.empty()) heard.push_back ("sounded " + joinAnd (adjectives));
            for (const auto& p : phrases) heard.push_back (p);
            std::string line = "AUTO: " + joinAnd (did);
            if (! heard.empty())
            {
                line += ": " + quoted (name) + " " + joinAnd (heard) + (byStyle ? " for " + style.name : "");
            }
            return line;
        }
    }

    // ---------------------------------------------------------------------------------------------

    const char* toolKey (Tool t)
    {
        switch (t)
        {
            case Tool::autoMix: return "auto";
            case Tool::balance: return "balance";
            case Tool::stereo: return "stereo";
            case Tool::cleanUp: return "clean";
            case Tool::pocket: return "pocket";
            case Tool::polish: return "polish";
        }
        return "auto";
    }

    const char* toolName (Tool t)
    {
        switch (t)
        {
            case Tool::autoMix: return "Auto Mix";
            case Tool::balance: return "Balance";
            case Tool::stereo: return "Stereo Architect";
            case Tool::cleanUp: return "Clean Up";
            case Tool::pocket: return "Vocal Pocket";
            case Tool::polish: return "Polish";
        }
        return "Auto Mix";
    }

    const char* pageKey (Page p)
    {
        switch (p)
        {
            case Page::eq: return "eq";
            case Page::dynamics: return "dynamics";
            case Page::space: return "space";
            case Page::tone: return "tone";
            case Page::stereo: return "stereo";
        }
        return "eq";
    }

    const char* pageName (Page p)
    {
        switch (p)
        {
            case Page::eq: return "EQ";
            case Page::dynamics: return "Dynamics";
            case Page::space: return "Space";
            case Page::tone: return "Tone";
            case Page::stereo: return "Stereo";
        }
        return "EQ";
    }

    std::optional<Page> pageFromKey (const std::string& k)
    {
        for (auto p : { Page::eq, Page::dynamics, Page::space, Page::tone, Page::stereo })
            if (k == pageKey (p)) return p;
        return std::nullopt;
    }

    bool autoListensFirst (Page page, std::uint32_t hash, const Report& r, const std::vector<ChannelNow>& channels)
    {
        if (heardEnough (r, hash)) return false;
        // An FX channel's Space works from the lead vocal (what goes into it), not from hearing the channel itself.
        if (page == Page::space && r.valid)
            for (const auto& c : channels)
                if (c.hash == hash && c.hasProc)
                {
                    const auto* t = trackIn (r, hash);
                    if (isFxReturn (c, r) || (t != nullptr && t->role == Role::fx)) return false;
                }
        return true;
    }

    Plan makeTrackPlan (Page page, std::uint32_t hash, const Report& r, const std::vector<ChannelNow>& channels, const MixNow&)
    {
        Plan plan;
        plan.tool = Tool::polish;
        plan.title = std::string ("AUTO ") + pageName (page);
        auto stop = [&plan] (const std::string& line) { plan.line = plan.summary = line; return plan; };
        if (hash == 0) return stop ("Pick a track: AUTO works on one track at a time (Auto Mix on the Overview does the whole mix).");
        const ChannelNow* ch = nullptr;
        for (const auto& c : channels)
            if (c.hash == hash) ch = &c;
        const auto* t = trackIn (r, hash);
        const std::string name = ch != nullptr ? ch->name : t != nullptr ? t->name : std::string ("This track");
        if (! r.valid) return stop ("Press Analyze Mix first and play the song: AUTO works from what it hears.");
        if (ch == nullptr || ! ch->hasLink) return stop (quoted (name) + " has no Mixology Link Mixology can reach: AUTO works in a track's Link.");
        if (ch->sharedName) return stop (quoted (name) + ": another track has the same name, so Mixology can't tell their Links apart. Rename one of them.");
        if (ch->locked) return stop (quoted (name) + " is protected (padlock): AUTO leaves it alone.");
        if (! ch->hasProc) return stop (quoted (name) + "'s Mixology Link has no processing (update Mixology Link).");
        if (pv (ch->proc, proc::chainOn) < 0.5f)
            return stop (quoted (name) + ": its Link's processing is switched off (On / Off in its window): switch it on, then press AUTO again.");
        const Lookup look (channels);
        Rules R (r, look, channels, plan, false);
        const bool fxReturn = isFxReturn (*ch, r) || (t != nullptr && t->role == Role::fx);
        if (page == Page::space && fxReturn)
        {
            plan.valid = true;
            if (R.lead != nullptr) leadSpace (R, hash);
            else R.note (quoted (name) + " is an FX channel: it plays what you send to it. With no lead vocal heard, AUTO sets nothing here.");
        }
        else
        {
            if (t == nullptr || ! counts (*t) || t->bands.size() < static_cast<std::size_t> (kBands))
                return stop ("Analyze Mix didn't hear " + quoted (name) + " play: play a part where it plays, then press AUTO again.");
            if (t->excluded)
                return stop (quoted (name) + (t->reference ? " is your reference song" : " is left out of Analyze Mix") + ": AUTO leaves it alone.");
            plan.valid = true;
            runPage (R, page, *t, *ch);
        }
        std::vector<std::string> unique;
        for (auto& n : plan.notes)
            if (std::find (unique.begin(), unique.end(), n) == unique.end()) unique.push_back (n);
        plan.notes = std::move (unique);
        plan.line = autoLine (plan, name, r.style);
        plan.summary = plan.line;
        return plan;
    }

    std::optional<Tool> toolFromKey (const std::string& k)
    {
        for (auto t : { Tool::autoMix, Tool::balance, Tool::stereo, Tool::cleanUp, Tool::pocket, Tool::polish })
            if (k == toolKey (t)) return t;
        return std::nullopt;
    }

    Plan makePlan (Tool tool, const Report& r, const std::vector<ChannelNow>& channels, const MixNow& mix)
    {
        Plan plan;
        plan.tool = tool;
        plan.title = toolName (tool);
        if (! r.valid)
        {
            plan.summary = "Press Analyze Mix first and play the song for 20 - 60 seconds: " + plan.title + " works from what it hears.";
            return plan;
        }
        plan.valid = true;
        const Lookup look (channels);
        if (look.musicBus == nullptr && (tool == Tool::autoMix || tool == Tool::balance)) plan.groupHints = r.groupHints;
        std::vector<std::string> parts;
        auto add = [&] (const std::string& s) { if (! s.empty()) parts.push_back (s); };
        if (tool == Tool::autoMix || tool == Tool::balance) add (planBalance (r, look, mix, plan));
        if (tool == Tool::autoMix || tool == Tool::stereo) add (planStereo (r, look, plan));
        if (tool == Tool::autoMix || tool == Tool::cleanUp) add (planCleanUp (r, look, plan));
        if (tool == Tool::autoMix || tool == Tool::pocket) add (planPocket (r, look, plan));
        if (tool == Tool::autoMix || tool == Tool::polish) add (planPolish (r, look, channels, mix, plan));

        // Fix first: a vocal Voxology heard recorded too hot. Lowering it at the source changes how loud it is, and
        // every level move here is worked out from how loud it is now.
        if (tool == Tool::autoMix || tool == Tool::balance || tool == Tool::polish || tool == Tool::pocket)
            for (const auto& c : channels)
            {
                const auto& v = c.voxology;
                if (! v.on || (! v.recordedHot && ! v.recordedClipped)) continue;
                std::string what;
                if (v.recordedHot && v.recordedPeakDb > -99.0) what = "it reaches " + signedDb (v.recordedPeakDb) + " coming in, over the top of the meter";
                if (v.recordedClipped && v.clippedSpots > 0)
                    what += (what.empty() ? "" : "; ") + std::to_string (v.clippedSpots) + " spot" + (v.clippedSpots == 1 ? "" : "s") + " clipped while it was recorded";
                if (what.empty()) what = v.recordedClipped ? "it clipped while it was recorded" : "it comes in over the top of the meter";
                plan.first.push_back ("Voxology says " + quoted (c.name) + " was recorded too hot (" + what + "). Lower its clip gain first, "
                                      "until its loudest words peak around -6 dB. In {daw}: {daw:clipGain}. Then press AUTO-EDIT in Voxology again "
                                      "(its settings follow the level) and let Analyze Mix hear it again (Auto Analyze does it by itself): "
                                      "these moves are worked out from how loud it is now.");
            }

        // Notes once each, in order.
        std::vector<std::string> unique;
        for (auto& n : plan.notes)
            if (std::find (unique.begin(), unique.end(), n) == unique.end()) unique.push_back (n);
        plan.notes = std::move (unique);

        if (plan.changes.empty())
            plan.summary = "Nothing to change for " + r.style.name + (parts.empty() ? std::string (".") : ": " + joinWords (parts, ", ") + ".");
        else
        {
            plan.summary = std::to_string (plan.changes.size()) + " change" + (plan.changes.size() == 1 ? "" : "s") + " for " + r.style.name + ": "
                           + joinWords (parts, "; ") + ".";
        }
        return plan;
    }
}
