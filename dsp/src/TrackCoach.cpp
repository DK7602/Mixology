#include "mixology/TrackCoach.h"

#include "mixology/ProcChain.h"
#include "mixology/ProcDsp.h"
#include "mixology/TrackAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>

namespace mixology::mix::coach
{
    namespace
    {
        constexpr double kRate = 48000.0;
        constexpr int kMidFrom = 15, kMidTo = 20;   // 640 Hz - 2 kHz: the mids each region is read against
        constexpr int kGraphFrom = 3, kGraphTo = 29;   // 40 Hz - 16 kHz on the graph

        const char* const kMinus = "\xE2\x88\x92";   // U+2212

        std::string num (double v, int decimals = 1)
        {
            // Locale-free (a host that sets a comma-decimal LC_NUMERIC would make printf write "4,1": invalid JSON).
            if (! std::isfinite (v)) v = 0.0;
            static constexpr long long kScale[] = { 1, 10, 100, 1000 };
            decimals = std::clamp (decimals, 0, 3);
            const double scaled = std::round (std::abs (v) * static_cast<double> (kScale[decimals]));
            if (scaled > 9.0e15) return "0";   // (nothing near this size is ever shown)
            const auto n = static_cast<long long> (scaled);
            std::string s = std::to_string (n / kScale[decimals]);
            if (decimals > 0)
            {
                const auto frac = std::to_string (n % kScale[decimals]);
                s += "." + std::string (static_cast<std::size_t> (decimals) - frac.size(), '0') + frac;
            }
            return v < 0.0 && n != 0 ? "-" + s : s;
        }
        /** Text for people: a real minus sign. */
        std::string signedText (double v, int decimals = 1)
        {
            const auto s = num (std::abs (v), decimals);
            if (s == num (0.0, decimals)) return s;
            return (v < 0.0 ? std::string (kMinus) : std::string ("+")) + s;
        }
        std::string fmt0 (double v) { return num (v, 0); }

        std::string esc (const std::string& s)
        {
            std::string o;
            o.reserve (s.size() + 8);
            for (const char ch : s)
            {
                const auto c = static_cast<unsigned char> (ch);
                if (c == '"' || c == '\\') { o += '\\'; o += ch; }
                else if (c == '\n') o += "\\n";
                else if (c < 0x20) { char b[8]; std::snprintf (b, sizeof (b), "\\u%04x", c); o += b; }
                else o += ch;
            }
            return o;
        }
        std::string str (const std::string& s) { return "\"" + esc (s) + "\""; }

        /** Cut to `n` bytes at a word (never inside a UTF-8 character), with an ellipsis. */
        std::string clip (const std::string& s, std::size_t n)
        {
            if (n == 0) return {};
            if (s.size() <= n) return s;
            std::size_t cut = n;
            while (cut > 0 && (static_cast<unsigned char> (s[cut]) & 0xC0) == 0x80) --cut;
            const auto space = s.rfind (' ', cut);
            if (space != std::string::npos && space > n / 2) cut = space;
            return s.substr (0, cut) + "\xE2\x80\xA6";
        }

        double level (const std::vector<double>& b, int from, int to)
        {
            double e = 0.0;
            int n = 0;
            for (int i = from; i <= to && i < static_cast<int> (b.size()); ++i, ++n)
                e += std::pow (10.0, (b[static_cast<std::size_t> (i)] + 1.5 * std::log2 (bandHz (i) / 1000.0)) / 10.0);
            return n > 0 && e > 0.0 ? 10.0 * std::log10 (e / n) : -120.0;
        }

        struct ModuleInfo { proc::Module module; const char* key; const char* name; const char* caption; };
        const ModuleInfo kModules[] = {
            { proc::Module::gate,   "mix.gate",   "Gate / Expand", "Clean the gaps" },
            { proc::Module::eq,     "mix.eq",     "EQ",            "Shape tone" },
            { proc::Module::comp,   "mix.comp",   "Compressor",    "Even levels" },
            { proc::Module::multi,  "mix.multi",  "Multiband",     "Control lows" },
            { proc::Module::trans,  "mix.trans",  "Transient",     "Shape hits" },
            { proc::Module::tone,   "mix.tone",   "Tone Balance",  "Tilt tone" },
            { proc::Module::stereo, "mix.stereo", "Width",         "Width" },
            { proc::Module::space,  "mix.space",  "Space",         "Add space" },
        };
        const ModuleInfo* moduleOf (const std::string& kind)
        {
            const char* key = kind == "eq" ? "mix.eq" : kind == "comp" ? "mix.comp" : kind == "multi" ? "mix.multi"
                            : kind == "trans" ? "mix.trans" : kind == "gate" ? "mix.gate" : kind == "tone" ? "mix.tone"
                            : kind == "width" ? "mix.stereo" : kind == "space" ? "mix.space" : nullptr;
            if (key == nullptr) return nullptr;
            for (const auto& m : kModules)
                if (std::string (m.key) == key) return &m;
            return nullptr;
        }

        /** What a change heard, as a thing that stands out: name, where on the spectrum (0 = not a place), red or not. */
        struct Heard { std::string key, name; double fromHz = 0.0, toHz = 0.0; bool warn = false; };
        Heard heardOf (const Change& c)
        {
            const auto& w = c.heard;
            if (w == "muddy")  return { "mud", "Mud", 250.0, 500.0, true };
            if (w == "boxy")   return { "boxy", "Boxy", 315.0, 800.0, true };
            if (w == "harsh")  return { "harsh", "Harshness", 2000.0, 5000.0, true };
            if (w == "dull")   return { "dull", "Dull top", 8000.0, 16000.0, false };
            if (w == "bright") return { "bright", "Too bright", 6000.0, 16000.0, true };
            if (w == "boomy")  return { "boom", "Boom", 60.0, 150.0, true };
            if (w == "thin")   return { "thin", "Thin low end", 60.0, 150.0, false };
            if (w == "narrow") return { "narrow", "Narrow", 0.0, 0.0, false };
            const auto& p = c.heardPhrase;
            auto has = [&p] (const char* s) { return p.find (s) != std::string::npos; };
            if (has ("in level"))            return { "swing", "Uneven level", 0.0, 0.0, true };
            if (has ("in the lows"))         return { "lows", "Jumpy low end", 60.0, 160.0, true };
            if (has ("noise between"))       return { "noise", "Noise between parts", 0.0, 0.0, true };
            if (has ("was dry"))             return { "dry", "Dry", 0.0, 0.0, false };
            if (has ("reverb too"))          return { "reverb", "Reverb level", 0.0, 0.0, false };
            if (has ("lows spread"))         return { "widelows", "Wide low end", 40.0, 160.0, true };
            if (has ("apart in mono"))       return { "wide", "Too wide", 0.0, 0.0, true };
            if (c.kind == "eq" && c.hz > 0.0) return { "eq", "Tone", c.hz / 1.3, c.hz * 1.3, false };
            if (! w.empty())                 return { w, std::string (1, static_cast<char> (std::toupper (static_cast<unsigned char> (w[0])))) + w.substr (1), 0.0, 0.0, false };
            return {};
        }

        proc::Values planned (const ChannelNow& ch, const Plan& plan, std::uint32_t hash)
        {
            auto v = ch.proc;
            for (const auto& c : plan.changes)
                if (c.hash == hash && ! c.fullMix)
                    for (const auto& [id, value] : c.proc)
                        if (id >= 0 && id < proc::count) v[static_cast<std::size_t> (id)] = value;
            return v;
        }
    }

    const char* roleWord (Role r)
    {
        switch (r)
        {
            case Role::kick: return "Kick";
            case Role::snare: return "Snare";
            case Role::hats: return "Hats";
            case Role::perc: return "Percussion";
            case Role::bass: return "Bass / 808";
            case Role::keys: return "Keys";
            case Role::synth: return "Synth";
            case Role::guitar: return "Guitar";
            case Role::vox: return "Lead vocal";
            case Role::adlibs: return "Ad-libs";
            case Role::fx: return "FX return";
            case Role::beat: return "Beat";
            case Role::other: break;
        }
        return "Other";
    }

    const std::vector<Region>& regions()
    {
        static const std::vector<Region> r {
            { "low",   "Low end (63 - 160 Hz)",  5,  8 },
            { "mud",   "Mud (250 - 500 Hz)",    11, 14 },
            { "harsh", "Harsh (2.5 - 5 kHz)",   21, 24 },
            { "top",   "Top (8 - 13 kHz)",      26, 28 },
        };
        return r;
    }

    double regionDb (const std::vector<double>& bands, int from, int to)
    {
        if (bands.size() < static_cast<std::size_t> (kBands)) return 0.0;
        return level (bands, from, to) - level (bands, kMidFrom, kMidTo);
    }

    double toneResponseDb (const proc::Values& v, double hz)
    {
        double db = 0.0;
        if (proc::isOn (v, proc::Module::eq)) db += proc::Chain::eqResponseDb (v, hz, kRate);
        if (proc::isOn (v, proc::Module::tone))
        {
            auto shelf = [&] (dsp::Shape shape, double at, int id, double q) {
                const double g = v[static_cast<std::size_t> (id)];
                if (std::abs (g) >= 1.0e-4) db += dsp::magnitudeDb (dsp::design (shape, at, g, q, kRate), hz, kRate);
            };
            shelf (dsp::Shape::lowShelf, 220.0, proc::tbWarmth, 0.6);    // (ProcChain's tone())
            shelf (dsp::Shape::bell, 3200.0, proc::tbPresence, 0.8);
            shelf (dsp::Shape::highShelf, 11000.0, proc::tbAir, 0.6);
        }
        return db;
    }

    double loudnessGapDb (const Plan& plan, const Report& r, std::uint32_t hash, const std::vector<ChannelNow>& channels)
    {
        const ChannelNow* ch = nullptr;
        for (const auto& c : channels)
            if (c.hash == hash) ch = &c;
        const TrackReport* t = nullptr;
        for (const auto& x : r.tracks)
            if (x.hash == hash) t = &x;
        if (ch == nullptr || ! ch->hasProc || t == nullptr || t->bands.size() < static_cast<std::size_t> (kBands)) return 0.0;
        const auto atAnalysis = ch->procAtAnalysisKnown ? ch->procAtAnalysis : ch->proc;
        const auto then = planned (*ch, plan, hash);
        // BS.1770's K weighting (its shelf and high-pass), as a response.
        const auto shelf = dsp::design (dsp::Shape::highShelf, 1681.97, 4.0, 0.7072, kRate);
        const auto highPass = dsp::design (dsp::Shape::highPass, 38.135, 0.0, 0.5003, kRate);
        double yours = 0.0, planEnergy = 0.0;
        for (int i = 0; i < kBands; ++i)
        {
            const double hz = bandHz (i);
            const double band = t->bands[static_cast<std::size_t> (i)];
            if (! std::isfinite (band)) continue;
            const double heard = band - toneResponseDb (atAnalysis, hz)
                               + dsp::magnitudeDb (shelf, hz, kRate) + dsp::magnitudeDb (highPass, hz, kRate);
            yours += std::pow (10.0, (heard + toneResponseDb (ch->proc, hz)) / 10.0);
            planEnergy += std::pow (10.0, (heard + toneResponseDb (then, hz)) / 10.0);
        }
        if (! (yours > 0.0) || ! (planEnergy > 0.0) || ! std::isfinite (yours) || ! std::isfinite (planEnergy)) return 0.0;
        return std::clamp (10.0 * std::log10 (planEnergy / yours), -12.0, 12.0);
    }

    std::string planJson (const Plan& plan, const Report& r, std::uint32_t hash, const std::vector<ChannelNow>& channels, std::size_t maxBytes)
    {
        const ChannelNow* ch = nullptr;
        for (const auto& c : channels)
            if (c.hash == hash) ch = &c;
        const TrackReport* t = nullptr;
        for (const auto& x : r.tracks)
            if (x.hash == hash) t = &x;

        if (! plan.valid)
        {
            std::string o = "{\"ok\":false,\"kind\":\"mix\",\"summary\":" + str (clip (plan.line, 400))
                          + ",\"tipTitle\":\"The Coach can't build a plan here\",\"tip\":" + str (clip (plan.line, 300)) + "}";
            return o.size() <= maxBytes ? o : std::string ("{\"ok\":false,\"kind\":\"mix\",\"summary\":\"No plan.\"}");
        }

        // The tone: the spectrum Analyze Mix heard, with the EQ / Tone Balance as they are now and as the plan sets them
        // (each against how they were when it listened).
        std::vector<double> now, after;
        const bool tone = t != nullptr && t->bands.size() >= static_cast<std::size_t> (kBands) && ch != nullptr && ch->hasProc;
        if (tone)
        {
            const auto atAnalysis = ch->procAtAnalysisKnown ? ch->procAtAnalysis : ch->proc;
            const auto then = planned (*ch, plan, hash);
            now = after = t->bands;
            for (int i = 0; i < kBands; ++i)
            {
                const double hz = bandHz (i), was = toneResponseDb (atAnalysis, hz);
                now[static_cast<std::size_t> (i)] += toneResponseDb (ch->proc, hz) - was;
                after[static_cast<std::size_t> (i)] += toneResponseDb (then, hz) - was;
            }
        }

        // What it heard (facts from the listening), what stands out (what the rules found), the Link's modules.
        std::vector<std::string> heard;
        if (t != nullptr)
        {
            heard.push_back (std::string ("Heard as ") + roleWord (t->role)
                             + (t->roleFrom == "you" ? " (your choice)" : t->roleFrom == "name" ? " (from its name)" : " (from its sound)"));
            heard.push_back ("Played " + fmt0 (t->activeSeconds) + " s while Mixology listened");
            if (t->levelRange > 0.0) heard.push_back ("Loud and quiet parts " + num (t->levelRange) + " dB apart");
            heard.push_back (t->width >= 0.03 ? "Stereo: " + fmt0 (t->width * 100.0) + " % on the sides" : std::string ("Mono, or nearly"));
            if (t->maskDb > -6.0 && t->role != Role::vox)
                heard.push_back ("At 1.6 - 4 kHz it sits " + signedText (t->maskDb) + " dB against the lead vocal (it covers its words)");
            if (t->rumble) heard.push_back ("Rumble under 100 Hz it doesn't need");
            if (t->floorMoments > 0) heard.push_back ("Noise between its parts at about " + signedText (t->floorRms, 0) + " dBFS");
        }
        std::vector<Heard> issues;
        std::vector<std::string> issueText;
        for (const auto& c : plan.changes)
        {
            const auto h = heardOf (c);
            if (h.key.empty()) continue;
            if (std::any_of (issues.begin(), issues.end(), [&h] (const Heard& x) { return x.key == h.key; })) continue;
            issues.push_back (h);
            issueText.push_back (c.why);
        }
        // red first
        {
            std::vector<std::size_t> order (issues.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort (order.begin(), order.end(), [&] (std::size_t a, std::size_t b) { return issues[a].warn && ! issues[b].warn; });
            std::vector<Heard> i2;
            std::vector<std::string> t2;
            for (auto k : order) { i2.push_back (issues[k]); t2.push_back (issueText[k]); }
            issues = std::move (i2);
            issueText = std::move (t2);
        }

        const auto nowV = ch != nullptr ? ch->proc : proc::defaults();
        const auto thenV = ch != nullptr ? planned (*ch, plan, hash) : nowV;
        int modulesOn = 0;
        for (const auto& m : kModules)
            if (proc::isOn (thenV, m.module)) ++modulesOn;

        // The results: each tone region before and after, then every move in words.
        struct Result { std::string key, name; double before, after; };
        std::vector<Result> results;
        if (tone)
            for (const auto& g : regions())
            {
                const double b = regionDb (now, g.from, g.to), a = regionDb (after, g.from, g.to);
                if (std::abs (a - b) >= 0.3) results.push_back ({ g.key, g.name, b, a });
            }

        std::vector<std::string> first;
        for (const auto& f : plan.first) if (first.size() < 4) first.push_back (f);
        for (const auto& n : plan.notes) if (first.size() < 4) first.push_back (n);

        const std::size_t kWhy[] = { 240, 160, 90, 0 };
        std::string out;
        for (std::size_t pass = 0; pass < std::size (kWhy); ++pass)
        {
            const std::size_t why = kWhy[pass];
            std::string o = "{\"ok\":true,\"kind\":\"mix\",\"summary\":" + str (clip (plan.line, pass < 2 ? 400 : 200));
            if (t != nullptr) o += ",\"role\":" + str (roleWord (t->role));
            o += ",\"style\":" + str (r.style.name) + ",\"aim\":" + str (r.style.name);
            o += ",\"heard\":[";
            for (std::size_t i = 0; i < heard.size(); ++i) o += (i ? "," : "") + str (heard[i]);
            o += "],\"issues\":[";
            for (std::size_t i = 0; i < issues.size(); ++i)
            {
                const auto& h = issues[i];
                o += (i ? "," : "") + std::string ("{\"key\":") + str (h.key) + ",\"name\":" + str (h.name)
                   + ",\"from\":" + fmt0 (h.fromHz) + ",\"to\":" + fmt0 (h.toHz) + ",\"warn\":" + (h.warn ? "true" : "false")
                   + ",\"text\":" + str (clip (issueText[i], why)) + "}";
            }
            o += "],\"chain\":[";
            bool firstModule = true;
            for (const auto& m : kModules)
            {
                std::vector<const Change*> mine;
                for (const auto& c : plan.changes)
                    if (const auto* mi = moduleOf (c.kind); mi != nullptr && mi->module == m.module) mine.push_back (&c);
                const bool on = proc::isOn (thenV, m.module) || ! mine.empty();
                if (! on) continue;
                o += (firstModule ? "" : ",") + std::string ("{\"key\":") + str (m.key) + ",\"name\":" + str (m.name)
                   + ",\"on\":true,\"was\":" + (proc::isOn (nowV, m.module) ? "true" : "false")
                   + ",\"caption\":" + str (m.caption) + ",\"kept\":" + (mine.empty() ? "true" : "false") + ",\"set\":[";
                firstModule = false;
                for (std::size_t i = 0; i < mine.size(); ++i)
                {
                    const auto& c = *mine[i];
                    const std::string where = c.hash != hash ? " (on " + c.name + ")" : std::string();
                    o += (i ? "," : "") + std::string ("{\"control\":") + str (clip (c.toText.empty() ? c.did : c.toText, 80) + where)
                       + ",\"from\":" + str (clip (c.fromText, 60)) + ",\"value\":\"\",\"why\":" + str (clip (c.why, why)) + "}";
                }
                o += "]}";
            }
            o += "],\"changes\":" + std::to_string (plan.changes.size()) + ",\"modulesOn\":" + std::to_string (modulesOn)
               + ",\"upToDate\":" + (plan.changes.empty() ? "true" : "false");
            if (tone)
            {
                const double mids = level (now, kMidFrom, kMidTo);
                auto curve = [&] (const std::vector<double>& b) {
                    std::string s = "[";
                    for (int i = kGraphFrom; i <= kGraphTo; ++i)
                    {
                        const double v = std::clamp (b[static_cast<std::size_t> (i)] + 1.5 * std::log2 (bandHz (i) / 1000.0) - mids, -30.0, 24.0);
                        s += (i > kGraphFrom ? "," : "") + num (v);
                    }
                    return s + "]";
                };
                o += ",\"bands\":{\"hz\":[";
                for (int i = kGraphFrom; i <= kGraphTo; ++i) o += (i > kGraphFrom ? "," : "") + num (bandHz (i), 0);
                o += "],\"before\":" + curve (now) + ",\"after\":" + curve (after) + "}";
            }
            o += ",\"results\":[";
            for (std::size_t i = 0; i < results.size(); ++i)
            {
                const auto& x = results[i];
                o += (i ? "," : "") + std::string ("{\"key\":") + str (x.key) + ",\"name\":" + str (x.name) + ",\"before\":" + num (x.before)
                   + ",\"after\":" + num (x.after) + ",\"unit\":\"dB vs its mids\",\"better\":true}";
            }
            o += "],\"moves\":[";
            for (std::size_t i = 0; i < plan.changes.size(); ++i)
            {
                const auto& c = plan.changes[i];
                const auto* mi = moduleOf (c.kind);
                o += (i ? "," : "") + std::string ("{\"module\":") + str (mi != nullptr ? mi->name : c.kind) + ",\"from\":" + str (clip (c.fromText, 60))
                   + ",\"to\":" + str (clip (c.toText, 60)) + "}";
            }
            o += "],\"first\":[";
            for (std::size_t i = 0; i < first.size(); ++i) o += (i ? "," : "") + str (clip (first[i], why > 0 ? why : 120));
            o += "]}";
            out = std::move (o);
            if (out.size() <= maxBytes) return out;
        }
        // Still too long: the summary alone (valid JSON, and the APPLY still knows there is a plan).
        out = "{\"ok\":true,\"kind\":\"mix\",\"summary\":" + str (clip (plan.line, 200)) + ",\"changes\":" + std::to_string (plan.changes.size())
            + ",\"modulesOn\":" + std::to_string (modulesOn) + ",\"chain\":[],\"issues\":[],\"results\":[],\"moves\":[],\"heard\":[],\"first\":[]}";
        return out.size() <= maxBytes ? out : std::string ("{\"ok\":true,\"kind\":\"mix\",\"summary\":\"\"}");
    }
}
