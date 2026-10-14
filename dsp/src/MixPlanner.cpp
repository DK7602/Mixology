#include "mixology/MixPlanner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <regex>

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

        bool movable (const ChannelNow* c) { return c != nullptr && c->faderKnown && ! c->locked && c->faderDb > -100.0; }

        // How far its fader moved since Analyze Mix listened (dB): the measurements are corrected by this.
        double movedSince (const ChannelNow* c)
        {
            if (c == nullptr || ! c->faderKnown || ! c->hasFaderAtAnalysis) return 0.0;
            if (c->faderDb <= -100.0 || c->faderAtAnalysis <= -100.0) return 0.0;
            return c->faderDb - c->faderAtAnalysis;
        }

        // ---------------------------------------------------------------------------------------------
        // Balance: fader levels

        std::string planBalance (const Report& r, const Lookup& look, Plan& plan)
        {
            struct Member { std::string name; const ChannelNow* ch = nullptr; };
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
                const double since = movedSince (ch) + (bus != nullptr && viaMusicBus (t.role) ? busSince : 0.0);
                if (std::abs (since) > 0.05) ++movedChannels;
                g.e += toE (t.loudness + since);
                heardE[static_cast<int> (t.role)] += toE (t.loudness);
                g.members.push_back ({ t.name, ch });
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
            double linkedInstruments = 0.0;
            for (auto& [k, g] : groups) if (isInstrument (g)) linkedInstruments += g.e;
            const bool bigUnheard = groups.count (100) && groups[100].e > 0.25 * linkedInstruments;
            if (bigUnheard)
                plan.notes.push_back (joinNames (r.unheard) + (r.unheard.size() == 1 ? " has" : " have")
                                      + " no Mixology Link and carr" + (r.unheard.size() == 1 ? "ies" : "y") + " a big part of the band, so Mixology doesn't judge "
                                        "the instruments against each other (only the vocal against everything). Insert Mixology Link there to change that.");
            if (! bigUnheard)
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
                const double fullErr = r.vocalTarget - ratio;
                const double err = fullErr;
                if (std::abs (fullErr) > 1.5)
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
                    if (vm != 0.0)
                    {
                        vocal->move += vm;
                        vocal->why.push_back ("the vocal " + where);
                        if (adlibs != nullptr) { adlibs->move += vm; adlibs->why.push_back ("follows the lead vocal"); }
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
                    after += g.e * toE (eff (g));
                    if (! g.viaBus) anyLocked = anyLocked || ! g.anyMovable();
                }
                double c = std::clamp (toDb (total) - toDb (after), -6.0, 6.0);
                if (c > 0.0)   // never past the top of a fader (that would undo the balance)
                {
                    for (auto& [k, g] : groups)
                        if (! g.viaBus)
                            for (const auto& m : g.members)
                                if (movable (m.ch)) c = std::min (c, std::max (0.0, kMaxFaderDb - m.ch->faderDb - g.move));
                    if (busActive) c = std::min (c, std::max (0.0, kMaxFaderDb - bus->faderDb - busMove));
                }
                if (! anyLocked && std::abs (c) >= 1.0)
                {
                    for (auto& [k, g] : groups) if (! g.viaBus) g.move += c;
                    if (busActive) busMove += c;
                }
            }

            // 4. Room for Masterology: offered separately (you tick it). Every fader that plays goes down together, so the
            //    balance stays; the mix gets quieter and Masterology brings the level back at the end.
            {
                double before = 0.0, after = 0.0;
                for (auto& [k, g] : groups) { before += g.e; after += g.e * toE (eff (g)); }
                const double peak = mixPeak + (toDb (after) - toDb (before));
                if (r.mixPeak > -100.0 && peak > -5.0)
                {
                    plan.headroomDb = std::round (-(peak + 6.0) * 10.0) / 10.0;
                    plan.headroomWhy = "Your mix peaks at " + signedDb (peak) + "TP. Masterology works best with peaks around -6: every fader goes down "
                                       + fmt1 (-plan.headroomDb) + " dB together. The balance stays the same, the mix just gets quieter (Masterology brings the level back).";
                    std::vector<std::string> stuck;
                    if (busActive) plan.headroom.push_back ({ bus->hash, bus->name, bus->faderDb, std::min (bus->faderDb + busMove, kMaxFaderDb) });
                    for (auto& [k, g] : groups)
                    {
                        if (g.viaBus) continue;   // they go down with the music group
                        for (const auto& m : g.members)
                        {
                            if (! movable (m.ch)) { stuck.push_back (m.name); continue; }
                            plan.headroom.push_back ({ m.ch->hash, m.name, m.ch->faderDb, std::min (m.ch->faderDb + g.move, kMaxFaderDb) });
                        }
                    }
                    if (! stuck.empty())
                        plan.headroomWhy += " " + joinNames (stuck) + " can't move (protected or not read): it would get louder against the rest.";
                }
            }

            // Changes, track by track (a kind's tracks all move the same).
            std::vector<std::pair<double, std::string>> moved;
            for (auto& [k, g] : groups)
            {
                if (std::abs (g.move) < 0.5) continue;
                moved.push_back ({ g.move, kindWords (g) });
                for (const auto& m : g.members)
                {
                    if (! movable (m.ch))
                    {
                        plan.notes.push_back (quoted (m.name) + (m.ch == nullptr ? ": Cubase hasn't shown its fader (press Analyze Mix again)"
                                                                 : m.ch->locked ? ": protected (padlock)" : ": its fader wasn't read")
                                              + ", so it stays where it is (it would have moved " + signedDb (g.move) + ").");
                        continue;
                    }
                    Change c;
                    c.hash = m.ch->hash;
                    c.name = m.name;
                    c.role = g.role;
                    c.kind = "level";
                    c.from = m.ch->faderDb;
                    c.wanted = std::min (c.from + g.move, kMaxFaderDb);
                    c.to = std::min (c.from + std::clamp (g.move, -kMaxMoveDb, kMaxMoveDb), kMaxFaderDb);
                    c.big = std::abs (g.move) > kMaxMoveDb + 0.05;
                    c.why = joinWords (g.why);
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
            if (! stereoPanners.empty())
                plan.notes.push_back (joinNames (stereoPanners) + (stereoPanners.size() == 1 ? " uses" : " use")
                                      + " Cubase's Stereo Combined Panner (Cubase only tells Mixology its left edge): pan "
                                      + (stereoPanners.size() == 1 ? "it" : "them") + " in Cubase if needed.");
            if (! stereoTracks.empty())
                plan.notes.push_back (joinNames (stereoTracks) + (stereoTracks.size() == 1 ? " is a stereo track" : " are stereo tracks")
                                      + ": Mixology only pans mono tracks (Cubase doesn't tell it where a stereo panner really sits).");

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
                if (std::abs (expected - r.mixBalance) > 6.0)
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
                const bool lead = role == Role::vox && ! pn.explicitPair && pn.order == 0;
                const double limit = role == Role::kick || role == Role::bass ? 5.0 : role == Role::snare ? 25.0 : lead ? 15.0 : 1.0e9;
                if (std::abs (x.pan) > limit)
                {
                    x.to = 0.0;
                    x.why = role == Role::vox ? "the lead vocal is the centre of the song"
                                              : std::string (roleWords (role)) + " belong" + (role == Role::snare ? "s" : "") + " in the middle: stronger on every speaker and in mono";
                }
                if (role == Role::kick || role == Role::bass || role == Role::snare || lead) x.fixed = true;
            }

            // 2. Pairs and doubles: spread left / right.
            std::map<std::string, std::vector<T*>> pairs;
            for (auto& x : ts)
            {
                if (x.fixed) continue;
                const auto pn = pairName (x.t->name);
                if (pn.base.empty()) continue;
                const auto role = x.t->role;
                if (role == Role::kick || role == Role::bass || role == Role::snare || role == Role::fx) continue;
                pairs[pn.base + "|" + roleKey (role)].push_back (&x);
            }
            for (auto& [key, members] : pairs)
            {
                if (members.size() < 2) continue;
                const auto role = members.front()->t->role;
                bool anyExplicit = false;
                for (auto* m : members) anyExplicit = anyExplicit || pairName (m->t->name).explicitPair;
                if (role == Role::other && ! anyExplicit) continue;   // "Audio 01", "Audio 02": not a pair
                std::vector<T*> spread = members;
                if (role == Role::vox)
                {
                    // The original take is the lead (centre); spread its doubles only when there are two or more.
                    spread.erase (std::remove_if (spread.begin(), spread.end(), [] (T* m) { const auto pn = pairName (m->t->name); return ! pn.explicitPair && pn.order == 0; }),
                                  spread.end());
                    if (spread.size() < 2) continue;
                }
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
                    m->why = "pairs with " + joinNames (names) + ": one left, one right sounds wide and keeps the middle free for the vocal";
                }
            }

            // 3. A lopsided mix: even it out with the loudest tracks pulling it to one side.
            auto predict = [&] () {
                double l = 0.0, rgt = 0.0;
                for (const auto& x : ts)
                {
                    const double e = toE (x.t->loudness) * std::max (0.05, x.t->activity);
                    const double bal = std::clamp (x.t->balance + panBalanceDb (x.to) - panBalanceDb (x.pan), -40.0, 40.0);
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
                                          + " dB) from tracks Mixology can't pan (stereo stems or channels without a Link): check those in Cubase.");
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
                if (! counts (t) || t.role == Role::kick || t.role == Role::bass || t.role == Role::fx) continue;
                const double all = rangeE (t.bands, 0, kBands - 1);
                if (all <= 0.0) continue;
                const double share = rangeE (t.bands, 0, 7) / all;
                const bool vocal = t.role == Role::vox || t.role == Role::adlibs;
                if (! t.rumble && ! (vocal && share >= 0.03)) continue;
                const double hz = lowCutFor (t.role);
                const auto* ch = eqChannel (t, look, plan, "low cut");
                if (ch == nullptr) continue;
                const double now = ch->assist.lowCutOn() ? ch->assist.lowCutHz : 0.0;
                if (now >= hz - 5.0) continue;
                Change c;
                c.hash = ch->hash;
                c.name = t.name;
                c.role = t.role;
                c.kind = "lowcut";
                c.from = now;
                c.to = c.wanted = hz;
                c.hz = hz;
                c.why = t.rumble ? fmt0 (share * 100.0) + "% of its energy is below 100 Hz, where it only muddies the kick and bass"
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
                        Change c;
                        c.hash = ch->hash;
                        c.name = t->name;
                        c.role = t->role;
                        c.kind = "mud";
                        c.from = ch->assist.mudDb;
                        c.to = c.wanted = -d;
                        c.hz = hz;
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
            int dips = 0;
            for (const auto& t : r.tracks)
            {
                const double mask = t.maskDb + movedSince (look.of (t)) - vocalMoved;
                if (! counts (t) || t.role == Role::vox || t.role == Role::adlibs || t.maskDb <= -100.0 || mask <= -6.0 || t.bands.size() != kBands) continue;
                const double d = mask >= 0.0 ? 4.0 : mask >= -3.0 ? 3.0 : 2.0;
                // Where it covers the vocal most: the band (1.25 - 5 kHz) with the biggest overlap.
                int best = 21;
                double bestOverlap = -1.0e9;
                for (int b = 18; b <= 24; ++b)
                {
                    const double o = std::min (vocal[static_cast<std::size_t> (b)], t.bands[static_cast<std::size_t> (b)]);
                    if (o > bestOverlap) { bestOverlap = o; best = b; }
                }
                const auto* ch = eqChannel (t, look, plan, "vocal pocket");
                if (ch == nullptr) continue;
                if (ch->assist.pocketDb <= -d + 0.25) continue;
                Change c;
                c.hash = ch->hash;
                c.name = t.name;
                c.role = t.role;
                c.kind = "pocket";
                c.from = ch->assist.pocketDb;
                c.to = c.wanted = -d;
                c.hz = bandHz (best);
                c.why = "at 1.6 - 4 kHz it is " + (mask >= 0.0 ? fmt1 (mask) + " dB louder than" : fmt1 (-mask) + " dB under")
                        + " the vocal while it sings, covering its words";
                plan.changes.push_back (c);
                ++dips;
            }
            if (dips == 0) return "nothing covers the vocal";
            return std::to_string (dips) + " pocket dip" + (dips == 1 ? "" : "s");
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
        }
        return "Auto Mix";
    }

    std::optional<Tool> toolFromKey (const std::string& k)
    {
        for (auto t : { Tool::autoMix, Tool::balance, Tool::stereo, Tool::cleanUp, Tool::pocket })
            if (k == toolKey (t)) return t;
        return std::nullopt;
    }

    PairName pairName (const std::string& name)
    {
        PairName p;
        std::string s;
        for (char ch : name) s += static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
        while (! s.empty() && std::isspace (static_cast<unsigned char> (s.back()))) s.pop_back();

        static const std::regex suffix (R"(^(.*?)[\s_\-]*[\(\[]?\s*(l|r|left|right|lft|rt|d|dbl|double|dub|dup|\d{1,2})\s*[\)\]]?$)", std::regex::icase);
        std::smatch m;
        if (std::regex_match (s, m, suffix) && ! m[1].str().empty())
        {
            const auto tag = m[2].str();
            const bool separated = m[1].str().size() + tag.size() < s.size();   // "Gtr L", "Gtr (L)", not "Gtrl" or "Lead"
            if (separated)
            {
                p.base = m[1].str();
                while (! p.base.empty() && (std::isspace (static_cast<unsigned char> (p.base.back())) || p.base.back() == '-' || p.base.back() == '_')) p.base.pop_back();
                if (tag == "l" || tag == "left" || tag == "lft") { p.side = -1; p.explicitPair = true; }
                else if (tag == "r" || tag == "right" || tag == "rt") { p.side = 1; p.explicitPair = true; }
                else if (std::isdigit (static_cast<unsigned char> (tag[0]))) p.order = std::max (0, std::stoi (tag) - 1);
                else { p.order = 1; p.explicitPair = true; }   // a double
                return p;
            }
        }
        p.base = s;
        return p;
    }

    Plan makePlan (Tool tool, const Report& r, const std::vector<ChannelNow>& channels)
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
        if (tool == Tool::autoMix || tool == Tool::balance) add (planBalance (r, look, plan));
        if (tool == Tool::autoMix || tool == Tool::stereo) add (planStereo (r, look, plan));
        if (tool == Tool::autoMix || tool == Tool::cleanUp) add (planCleanUp (r, look, plan));
        if (tool == Tool::autoMix || tool == Tool::pocket) add (planPocket (r, look, plan));

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
