#pragma once

#include "mixology/McuBridge.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/** A stand-in for Logic Pro or Cakewalk driving a Mackie Control surface, for tests: tracks with a fader, pan,
    mute and solo, a bank of 8 the surface looks at, the display (short names on top, the knob's value below),
    the lights, and the answers each DAW gives to the surface's buttons, knobs and faders, as their published
    source (Cakewalk) and captures (Logic) show them:
    - Logic asks "who are you?" (device query) until the surface answers the handshake; after it, it ignores
      buttons for 1.5 s. With its "show info when editing volume" help tag, a touched fader shows "Volume" in the
      name's place and its dB below. Pan text: -64 .. +63, "0" in the middle.
    - Cakewalk (handshake off, its default) starts over and sends everything after a wake-up; until then it
      sends nothing (it was running before Mixology). Its faders have 10 bits. Its knob accelerates, and shows
      its value in the name's place for 1.5 s. Below the names it shows "Pan" until NAME/VALUE is pressed.
    The fader curve differs a little from the translator's built-in one (as a real DAW's would). */
struct FakeMcuHost
{
    using Dialect = mixology::mcu::Dialect;

    struct Track
    {
        std::string name;
        int fader = 11000;      // pitch bend 0..16383 (Cakewalk: a multiple of 16, its 10 bits)
        double pan = 0.0;       // -1 .. 1
        bool mute = false, solo = false;
        int faderMoves = 0;
    };

    Dialect dialect = Dialect::logic;
    std::vector<Track> tracks;
    int offset = 0;
    bool fullLastBank = true;      // both DAWs: the last bank is full (19 strips: 9-16, then 12-19)
    bool showDbOnTouch = true;     // Logic's help tag (Cakewalk shows nothing on touch)
    bool dbOnlyForLastTouched = false;   // a display that shows the value of the last touched fader only
    bool online = false;
    double now = 0.0;
    double buttonsFrom = 0.0;      // Logic: buttons before this are ignored
    bool cakewalkValues = false;   // Cakewalk: NAME/VALUE pressed (values under the names)
    double tempUntil[8] = {};      // Cakewalk: the knob's value in the name's place until then
    bool touched[8] = {};
    int lastTouched = -1;
    double lastQuery = -10.0;
    int ignoredButtons = 0;
    std::vector<std::vector<std::uint8_t>> toSurface;
    double dbOffset = 0.4;         // how far the real curve is from the translator's built-in guess (mid-way)

    explicit FakeMcuHost (Dialect d) : dialect (d) { if (d == Dialect::cakewalk) showDbOnTouch = false; }

    // ---- the DAW's own curve: the built-in one, bent a little ----
    double trueDb (int pb) const
    {
        const double n = mixology::mcu::faderToNormalised (dialect, pb);
        const double base = mixology::mcu::McuLaw::builtInDb (dialect, n);
        if (base <= -999.0) return base;
        return base + dbOffset * std::sin (n * 3.14159);   // 0 at both ends, up to dbOffset in the middle
    }

    static std::string abbreviate (const std::string& name, std::size_t width)
    {
        if (name.size() <= width) return name;
        // A number at the end is kept ("Background Vox 2" -> "Bckgr2").
        std::size_t d = name.size();
        while (d > 0 && std::isdigit (static_cast<unsigned char> (name[d - 1]))) --d;
        if (d < name.size() && d > 0 && name.size() - d < width)
            return abbreviate (name.substr (0, d), width - (name.size() - d)) + name.substr (d);
        // Drop spaces, then vowels after the first letter, then cut.
        std::string s;
        for (char c : name) if (c != ' ') s.push_back (c);
        if (s.size() > width)
        {
            std::string t;
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                const char c = s[i];
                if (i > 0 && std::string ("aeiou").find (c) != std::string::npos && s.size() - (i - t.size()) > width) continue;
                t.push_back (c);
            }
            s = t;
        }
        return s.substr (0, width);
    }

    std::string panTextOf (const Track& t) const
    {
        char buf[16];
        if (dialect == Dialect::logic)
        {
            const int v = static_cast<int> (std::lround (t.pan < 0 ? t.pan * 64.0 : t.pan * 63.0));
            if (v == 0) std::snprintf (buf, sizeof (buf), "0");
            else std::snprintf (buf, sizeof (buf), "%+d", v);
        }
        else
        {
            const int v = static_cast<int> (std::lround (t.pan * 100.0));
            if (v == 0) std::snprintf (buf, sizeof (buf), "C");
            else std::snprintf (buf, sizeof (buf), "%d%%%c", std::abs (v), v < 0 ? 'L' : 'R');
        }
        return buf;
    }

    std::string dbTextOf (const Track& t) const
    {
        const double db = trueDb (t.fader);
        if (db < -144.0) return "-inf";
        char buf[16];
        std::snprintf (buf, sizeof (buf), "%.1f", db);
        return buf;
    }

    Track* inStrip (int strip)
    {
        const int i = offset + strip;
        return i >= 0 && i < static_cast<int> (tracks.size()) ? &tracks[static_cast<std::size_t> (i)] : nullptr;
    }
    int indexOf (const std::string& name) const
    {
        for (std::size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].name == name) return static_cast<int> (i);
        return -1;
    }
    double dbOf (const std::string& name) const { return trueDb (tracks.at (static_cast<std::size_t> (indexOf (name))).fader); }

    bool helpTag (int strip) const
    {
        return showDbOnTouch && touched[strip] && (! dbOnlyForLastTouched || lastTouched == strip);
    }

    // What one strip shows, top and bottom.
    std::string topOf (int i)
    {
        const auto* t = inStrip (i);
        if (t == nullptr) return "";
        if (dialect == Dialect::logic && helpTag (i)) return "Volume";
        if (dialect == Dialect::cakewalk && now < tempUntil[i]) return panTextOf (*t);
        return abbreviate (t->name, 6);
    }
    std::string bottomOf (int i)
    {
        const auto* t = inStrip (i);
        if (t == nullptr) return "";
        if (dialect == Dialect::logic) return helpTag (i) ? dbTextOf (*t) : panTextOf (*t);
        return cakewalkValues ? panTextOf (*t) : "Pan";
    }

    // ---- what the DAW sends ----
    void lcd (int offsetChars, const std::string& text)
    {
        std::vector<std::uint8_t> m { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, static_cast<std::uint8_t> (offsetChars) };
        for (char c : text) m.push_back (static_cast<std::uint8_t> (c));
        m.push_back (0xF7);
        toSurface.push_back (m);
    }
    void lcdStrip (int strip, int row, const std::string& text)
    {
        std::string seg = text.substr (0, 6);
        seg.resize (7, ' ');
        lcd (row * 56 + strip * 7, seg);
    }
    void redrawStrip (int i)
    {
        lcdStrip (i, 0, topOf (i));
        lcdStrip (i, 1, bottomOf (i));
    }
    void sendFader (int strip, int pb) { toSurface.push_back ({ static_cast<std::uint8_t> (0xE0 + strip), static_cast<std::uint8_t> (pb & 0x7F), static_cast<std::uint8_t> (pb >> 7) }); }
    void light (std::uint8_t n, bool on) { toSurface.push_back ({ 0x90, n, static_cast<std::uint8_t> (on ? 0x7F : 0x00) }); }
    void ring (int strip, const Track* t)
    {
        const int pos = t != nullptr ? 6 + static_cast<int> (std::lround (t->pan * 5.0)) : 0;
        toSurface.push_back ({ 0xB0, static_cast<std::uint8_t> (0x30 + strip), static_cast<std::uint8_t> (pos | 0x10 | (pos == 6 ? 0x40 : 0)) });
    }

    void refreshAll()
    {
        // The whole rows at once, as both DAWs do.
        std::string top, bottom;
        for (int i = 0; i < 8; ++i)
        {
            std::string a = topOf (i).substr (0, 6), b = bottomOf (i).substr (0, 6);
            a.resize (7, ' ');
            b.resize (7, ' ');
            top += a;
            bottom += b;
        }
        lcd (0, top);
        lcd (56, bottom);
        for (int i = 0; i < 8; ++i)
        {
            const auto* t = inStrip (i);
            sendFader (i, t ? t->fader : 0);
            light (static_cast<std::uint8_t> (0x10 + i), t && t->mute);
            light (static_cast<std::uint8_t> (0x08 + i), t && t->solo);
            ring (i, t);
        }
    }

    void moveBank (int by)
    {
        const int n = static_cast<int> (tracks.size());
        const int last = fullLastBank ? std::max (0, n - 8) : std::max (0, n - 1);
        const int before = offset;
        offset = std::clamp (offset + by, 0, last);
        if (offset != before)
        {
            for (auto& t : tempUntil) t = 0.0;
            refreshAll();
        }
    }

    // Time passes on the DAW's side (its refresh: Logic's queries, Cakewalk's passing texts running out).
    void tick (double t)
    {
        now = t;
        if (dialect == Dialect::logic && ! online && now - lastQuery >= 1.0)
        {
            lastQuery = now;
            toSurface.push_back ({ 0xF0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xF7 });   // "who are you?"
        }
        if (dialect == Dialect::cakewalk)
            for (int i = 0; i < 8; ++i)
                if (tempUntil[i] > 0.0 && now >= tempUntil[i])
                {
                    tempUntil[i] = 0.0;
                    lcdStrip (i, 0, topOf (i));
                }
    }

    // ---- what the surface (Mixology) sends ----
    void receive (const std::vector<std::uint8_t>& m)
    {
        if (m.empty()) return;
        if (m[0] == 0xF0)
        {
            if (m.size() < 7) return;
            if (m[5] == 0x01)
            {
                if (dialect == Dialect::logic)   // the handshake: our answer (serial, a response)
                {
                    std::vector<std::uint8_t> r { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x02 };
                    r.insert (r.end(), m.begin() + 6, m.begin() + 13);
                    for (int k = 0; k < 4; ++k) r.push_back (0x10);
                    r.push_back (0xF7);
                    toSurface.push_back (r);
                }
                else   // Cakewalk: a wake-up resets it; with the handshake off it sends everything at once
                {
                    online = true;
                    refreshAll();
                }
            }
            else if (m[5] == 0x03 && dialect == Dialect::logic)
            {
                online = true;
                buttonsFrom = now + 1.5;
                refreshAll();
            }
            return;
        }
        if (! online) return;
        const int status = m[0] & 0xF0, ch = m[0] & 0x0F;
        if (status == 0xE0 && m.size() >= 3)
        {
            if (auto* t = inStrip (ch))
            {
                int pb = (m[2] << 7) | m[1];
                if (dialect == Dialect::cakewalk) pb = (pb >> 4) << 4;   // 10 bits
                t->fader = pb;
                ++t->faderMoves;
                if (helpTag (ch)) lcdStrip (ch, 1, bottomOf (ch));
            }
        }
        else if (status == 0x90 && m.size() >= 3)
        {
            const int n = m[1];
            const bool down = m[2] == 0x7F;
            if (n >= 0x68 && n < 0x70)
            {
                const int strip = n - 0x68;
                touched[strip] = down;
                if (down) lastTouched = strip;
                if (auto* t = inStrip (strip))
                {
                    redrawStrip (strip);
                    if (! down) sendFader (strip, t->fader);   // let go: the DAW puts the fader where the channel is
                }
                return;
            }
            if (! down) return;
            if (dialect == Dialect::logic && now < buttonsFrom) { ++ignoredButtons; return; }
            if (n >= 0x10 && n < 0x18) { if (auto* t = inStrip (n - 0x10)) { t->mute = ! t->mute; light (static_cast<std::uint8_t> (n), t->mute); } }
            else if (n >= 0x08 && n < 0x10) { if (auto* t = inStrip (n - 0x08)) { t->solo = ! t->solo; light (static_cast<std::uint8_t> (n), t->solo); } }
            else if (n == 0x2E) moveBank (-8);
            else if (n == 0x2F) moveBank (8);
            else if (n == 0x30) moveBank (-1);
            else if (n == 0x31) moveBank (1);
            else if (n == 0x34 && dialect == Dialect::cakewalk)
            {
                cakewalkValues = ! cakewalkValues;
                for (int i = 0; i < 8; ++i) lcdStrip (i, 1, bottomOf (i));
            }
        }
        else if (status == 0xB0 && m.size() >= 3 && m[1] >= 0x10 && m[1] < 0x18)
        {
            const int strip = m[1] - 0x10;
            if (auto* t = inStrip (strip))
            {
                const int ticks = m[2] & (dialect == Dialect::cakewalk ? 0x0F : 0x3F);
                const double dir = (m[2] & 0x40) ? -1.0 : 1.0;
                if (dialect == Dialect::logic)
                {
                    // -64 .. +63 in whole steps
                    int units = static_cast<int> (std::lround (t->pan < 0 ? t->pan * 64.0 : t->pan * 63.0));
                    units = std::clamp (units + static_cast<int> (dir) * ticks, -64, 63);
                    t->pan = units < 0 ? units / 64.0 : units / 63.0;
                }
                else
                {
                    const double range = (t->pan + 1.0) / 2.0 + dir * 0.005 * (3.0 * ticks - 2.0);   // its acceleration
                    t->pan = std::clamp (range, 0.0, 1.0) * 2.0 - 1.0;
                    tempUntil[strip] = now + 1.5;
                    lcdStrip (strip, 0, topOf (strip));
                }
                lcdStrip (strip, 1, bottomOf (strip));
                ring (strip, t);
            }
        }
    }

    // The user moves a fader in the DAW.
    void userMovesFader (const std::string& name, int pb)
    {
        const int i = indexOf (name);
        tracks[static_cast<std::size_t> (i)].fader = pb;
        if (i >= offset && i < offset + 8) sendFader (i - offset, pb);
    }
    void rename (const std::string& from, const std::string& to)
    {
        const int i = indexOf (from);
        tracks[static_cast<std::size_t> (i)].name = to;
        if (i >= offset && i < offset + 8) lcdStrip (i - offset, 0, topOf (i - offset));
    }
    // The pan as the DAW keeps it, in its own units (Logic -64 .. 63, Cakewalk percent).
    int panUnits (const std::string& name) const
    {
        const double p = tracks.at (static_cast<std::size_t> (indexOf (name))).pan;
        if (dialect == Dialect::logic) return static_cast<int> (std::lround (p < 0 ? p * 64.0 : p * 63.0));
        return static_cast<int> (std::lround (p * 100.0));
    }
};
