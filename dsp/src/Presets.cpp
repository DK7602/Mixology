#include "mixology/Presets.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace mixology::presets
{
    namespace
    {
        using V = std::vector<std::pair<std::string, float>>;

        Preset make (const char* name, const char* about, Kind kind, const char* roles, V values)
        {
            Preset p;
            p.name = name;
            p.about = about;
            p.kind = kind;
            p.roles = roles;
            p.factory = true;
            p.values = std::move (values);
            return p;
        }

        // EQ bands: shape 0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut.
        V lowCut (int band, float hz) { const auto b = std::to_string (band); return { { "eq" + b + "_type", 3.0f }, { "eq" + b + "_freq", hz }, { "eq" + b + "_q", 0.7f } }; }
        V bell (int band, float hz, float db, float q) { const auto b = std::to_string (band); return { { "eq" + b + "_type", 0.0f }, { "eq" + b + "_freq", hz }, { "eq" + b + "_gain", db }, { "eq" + b + "_q", q } }; }
        V highShelf (int band, float hz, float db) { const auto b = std::to_string (band); return { { "eq" + b + "_type", 2.0f }, { "eq" + b + "_freq", hz }, { "eq" + b + "_gain", db }, { "eq" + b + "_q", 0.7f } }; }

        V join (std::initializer_list<V> parts)
        {
            V out;
            for (const auto& p : parts) out.insert (out.end(), p.begin(), p.end());
            return out;
        }

        std::string trim (const std::string& s)
        {
            const auto a = s.find_first_not_of (" \t\r\n");
            if (a == std::string::npos) return {};
            const auto b = s.find_last_not_of (" \t\r\n");
            return s.substr (a, b - a + 1);
        }

        // Numbers in the preset file: always with a '.' whatever the computer's language, the shortest text that
        // reads back as the same value (std::to_chars for floats needs macOS 13.3, so streams in the "C" locale).
        bool parseNumber (const std::string& s, float& v)
        {
            if (s.empty() || std::isspace (static_cast<unsigned char> (s.front()))) return false;
            std::istringstream in (s);
            in.imbue (std::locale::classic());
            float x = 0.0f;
            in >> x;
            if (in.fail() || ! in.eof() || ! std::isfinite (x)) return false;
            v = x;
            return true;
        }

        std::string number (float v)
        {
            for (int digits = 1; digits <= 9; ++digits)
            {
                std::ostringstream out;
                out.imbue (std::locale::classic());
                out << std::setprecision (digits) << v;
                float back = 0.0f;
                if (parseNumber (out.str(), back) && back == v) return out.str();
            }
            std::ostringstream out;
            out.imbue (std::locale::classic());
            out << std::setprecision (9) << v;
            return out.str();
        }
    }

    const char* kindKey (Kind k) { return k == Kind::fx ? "fx" : k == Kind::mix ? "mix" : "track"; }
    Kind kindFromKey (const std::string& s) { return s == "fx" ? Kind::fx : s == "mix" ? Kind::mix : Kind::track; }

    bool Preset::suits (const std::string& roleKey) const
    {
        if (roles.empty()) return true;
        std::stringstream in (roles);
        std::string r;
        while (std::getline (in, r, ','))
            if (trim (r) == roleKey) return true;
        return false;
    }

    const std::vector<Preset>& factory()
    {
        static const std::vector<Preset> list = [] {
            const V eq { { "eq_on", 1.0f } };
            const V vocalEq = join ({ eq, lowCut (1, 100.0f), bell (2, 300.0f, -1.5f, 1.2f), bell (4, 3200.0f, 1.5f, 1.0f), highShelf (6, 11000.0f, 1.5f) });
            std::vector<Preset> v;

            // ---- tracks
            v.push_back (make ("Lead Vocal: Sit in the Beat", "After Voxology (it already compresses): a low cut, a little less mud, presence and air "
                               "so the words sit on top of the beat. EQ only.", Kind::track, "vox", vocalEq));
            v.push_back (make ("Lead Vocal: Clear & Upfront", "For a vocal without Voxology: the same EQ, a bit brighter, plus a 3:1 compressor that keeps "
                               "every word in front.", Kind::track, "vox",
                               join ({ eq, lowCut (1, 100.0f), bell (2, 300.0f, -1.5f, 1.2f), bell (4, 3200.0f, 2.0f, 1.0f), highShelf (6, 11000.0f, 2.0f),
                                       { { "comp_on", 1.0f }, { "comp_thr", -20.0f }, { "comp_ratio", 3.0f }, { "comp_att", 8.0f }, { "comp_rel", 90.0f },
                                         { "comp_makeup", 3.0f } } })));
            v.push_back (make ("Backing Vocals: Soft Bed", "Doubles and harmonies under the lead: less low end and presence, gently compressed, in a "
                               "short room.", Kind::track, "vox",
                               join ({ eq, lowCut (1, 160.0f), bell (4, 3000.0f, -2.0f, 1.0f),
                                       { { "comp_on", 1.0f }, { "comp_thr", -24.0f }, { "comp_ratio", 2.5f }, { "comp_att", 15.0f }, { "comp_rel", 150.0f },
                                         { "comp_makeup", 2.0f }, { "sp_on", 1.0f }, { "sp_type", 5.0f }, { "room_mix", 18.0f } } })));
            v.push_back (make ("Ad-libs: Behind & Bouncing", "Thinner than the lead and echoing left and right on the 1/8: fills the gaps without covering "
                               "the lead.", Kind::track, "adlibs",
                               join ({ eq, lowCut (1, 220.0f), bell (4, 2500.0f, -1.5f, 1.0f), highShelf (6, 10000.0f, 1.0f),
                                       { { "sp_on", 1.0f }, { "sp_type", 1.0f }, { "dly_time", 3.0f }, { "dly_feedback", 30.0f }, { "dly_tone", 5000.0f },
                                         { "dly_pingpong", 100.0f }, { "dly_mix", 15.0f }, { "sp_lowcut", 300.0f }, { "sp_highcut", 8000.0f } } })));
            v.push_back (make ("Kick: Punch", "Weight at 60 Hz, the box at 400 Hz out of the way, a little click on top, a sharper hit.", Kind::track, "kick",
                               join ({ eq, bell (1, 60.0f, 2.0f, 1.0f), bell (2, 400.0f, -3.0f, 1.4f), bell (4, 4000.0f, 2.0f, 1.2f),
                                       { { "trans_on", 1.0f }, { "trans_attack", 30.0f }, { "trans_sustain", -10.0f } } })));
            v.push_back (make ("808: Solid & Mono", "An even 808 under the whole song, its lows in the middle, and harmonics so it still shows on "
                               "phones.", Kind::track, "bass",
                               join ({ eq, lowCut (1, 28.0f),
                                       { { "comp_on", 1.0f }, { "comp_thr", -18.0f }, { "comp_ratio", 4.0f }, { "comp_att", 25.0f }, { "comp_rel", 150.0f },
                                         { "comp_makeup", 2.0f }, { "st_on", 1.0f }, { "st_low", 0.0f }, { "st_lowx", 150.0f },
                                         { "harm_on", 1.0f }, { "harm_drive", 20.0f }, { "harm_mix", 30.0f } } })));
            v.push_back (make ("Snare: Crack", "Body at 200 Hz, less box, a brighter crack and a snappier hit.", Kind::track, "snare",
                               join ({ eq, lowCut (1, 90.0f), bell (2, 200.0f, 1.5f, 1.0f), bell (3, 900.0f, -2.0f, 1.4f), bell (5, 5000.0f, 2.0f, 1.0f),
                                       { { "trans_on", 1.0f }, { "trans_attack", 25.0f } } })));
            v.push_back (make ("Hats: Smooth Top", "No low rumble, the harsh 7 kHz tamed, a little air.", Kind::track, "hats",
                               join ({ eq, lowCut (1, 300.0f), bell (5, 7000.0f, -2.5f, 2.0f), highShelf (6, 13000.0f, 1.0f) })));
            v.push_back (make ("Percussion: Tight", "A low cut and a sharper attack, so the groove stays crisp.", Kind::track, "perc",
                               join ({ eq, lowCut (1, 150.0f), { { "trans_on", 1.0f }, { "trans_attack", 20.0f }, { "trans_sustain", -15.0f } } })));
            v.push_back (make ("Keys & Pads: Room for the Vocal", "Clears the low end and dips where the words are (2 - 3 kHz), a bit wider (stereo sounds).",
                               Kind::track, "keys,synth",
                               join ({ eq, lowCut (1, 120.0f), bell (4, 2500.0f, -2.5f, 0.9f), { { "st_on", 1.0f }, { "st_width", 125.0f } } })));
            v.push_back (make ("Guitar: Clean & Clear", "Less low end and mud, a touch of bite.", Kind::track, "guitar",
                               join ({ eq, lowCut (1, 100.0f), bell (2, 300.0f, -2.0f, 1.0f), bell (4, 3000.0f, 1.0f, 1.0f) })));

            // ---- FX channels (100 % wet: send the tracks to it)
            v.push_back (make ("FX Return: Vocal Plate", "A short, bright plate for the vocal on an FX channel. Send the vocals to it; LEVEL sets how much "
                               "comes back.", Kind::fx, "fx",
                               { { "sp_on", 1.0f }, { "sp_type", 0.0f }, { "rev_pre", 20.0f }, { "rev_decay", 1.6f }, { "rev_damp", 7000.0f },
                                 { "sp_lowcut", 250.0f }, { "sp_highcut", 9000.0f }, { "sp_level", -3.0f } }));
            v.push_back (make ("FX Return: Hall", "A big, smooth hall for slow songs and big hooks.", Kind::fx, "fx",
                               { { "sp_on", 1.0f }, { "sp_type", 4.0f }, { "hall_pre", 35.0f }, { "hall_decay", 3.0f }, { "hall_size", 80.0f },
                                 { "hall_damp", 5000.0f }, { "sp_lowcut", 250.0f }, { "sp_highcut", 8000.0f }, { "sp_level", -4.0f } }));
            v.push_back (make ("FX Return: 1/4 Echo", "Quarter-note echoes, darker each time, bouncing left and right.", Kind::fx, "fx",
                               { { "sp_on", 1.0f }, { "sp_type", 1.0f }, { "dly_time", 6.0f }, { "dly_feedback", 35.0f }, { "dly_tone", 4500.0f },
                                 { "dly_pingpong", 60.0f }, { "sp_lowcut", 300.0f }, { "sp_highcut", 7000.0f }, { "sp_level", -3.0f } }));

            // ---- the Full Mix (Mixology on the Stereo Out, before Masterology)
            v.push_back (make ("Mix Bus: Gentle Glue", "A little bus compression that makes the mix hang together. Nothing else.", Kind::mix, "",
                               { { "fin_on", 1.0f }, { "fin_glue", 30.0f } }));
            v.push_back (make ("Mix Bus: Warm & Wide", "A touch warmer, a touch more air, a bit wider, lightly glued.", Kind::mix, "",
                               { { "tb_on", 1.0f }, { "tb_warmth", 1.0f }, { "tb_air", 1.0f }, { "fin_on", 1.0f }, { "fin_width", 110.0f }, { "fin_glue", 20.0f } }));
            v.push_back (make ("Mix Bus: Bright & Modern", "More presence and air, a little less low-mid weight.", Kind::mix, "",
                               { { "tb_on", 1.0f }, { "tb_warmth", -0.5f }, { "tb_presence", 0.5f }, { "tb_air", 1.5f } }));
            return v;
        }();
        return list;
    }

    proc::Values valuesOf (const Preset& p)
    {
        auto v = proc::defaults();
        for (const auto& [id, value] : p.values)
            if (const int i = proc::indexOf (id); i >= 0) v[static_cast<std::size_t> (i)] = proc::sanitise (i, value);
        return v;
    }

    proc::Values forTarget (const Preset& p, const proc::Values& current)
    {
        auto v = valuesOf (p);
        v[proc::spRoute] = p.kind == Kind::fx ? 1.0f : current[proc::spRoute];
        return v;
    }

    Preset capture (const std::string& name, Kind kind, const std::string& roles, const proc::Values& v)
    {
        Preset p;
        p.name = cleanName (name);
        p.kind = kind;
        p.roles = roles;
        const auto def = proc::defaults();
        const auto& specs = proc::specs();
        for (int i = 0; i < proc::count; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            if (std::abs (v[k] - def[k]) > 1.0e-6f) p.values.push_back ({ specs[k].id, v[k] });
        }
        return p;
    }

    std::string serialise (const std::vector<Preset>& list)
    {
        std::string out = "Mixology presets 1\n";
        for (const auto& p : list)
        {
            if (p.factory) continue;
            out += "\n[preset]\nname=" + cleanName (p.name) + "\nkind=" + kindKey (p.kind) + "\nroles=" + p.roles + "\n";
            if (! p.about.empty()) out += "about=" + cleanName (p.about) + "\n";
            for (const auto& [id, value] : p.values) out += id + "=" + number (value) + "\n";
        }
        return out;
    }

    std::vector<Preset> parse (const std::string& text)
    {
        std::vector<Preset> out;
        std::stringstream in (text);
        std::string line;
        Preset* cur = nullptr;
        while (std::getline (in, line))
        {
            line = trim (line);
            if (line == "[preset]") { out.emplace_back(); cur = &out.back(); continue; }
            if (cur == nullptr || line.empty()) continue;
            const auto eq = line.find ('=');
            if (eq == std::string::npos) continue;
            const auto key = trim (line.substr (0, eq)), value = trim (line.substr (eq + 1));
            if (key == "name") cur->name = cleanName (value);
            else if (key == "kind") cur->kind = kindFromKey (value);
            else if (key == "roles") cur->roles = value;
            else if (key == "about") cur->about = value;
            else if (float f = 0.0f; proc::indexOf (key) >= 0 && parseNumber (value, f)) cur->values.push_back ({ key, f });
        }
        out.erase (std::remove_if (out.begin(), out.end(), [] (const Preset& p) { return p.name.empty(); }), out.end());
        return out;
    }

    std::string cleanName (const std::string& raw)
    {
        std::string s;
        for (char c : raw) s.push_back (c == '\n' || c == '\r' || c == '\t' ? ' ' : c);
        s = trim (s);
        if (s.size() > 60)
        {
            std::size_t n = 60;
            while (n > 0 && (static_cast<unsigned char> (s[n]) & 0xC0) == 0x80) --n;   // UTF-8 boundary
            s = trim (s.substr (0, n));
        }
        return s;
    }
}
