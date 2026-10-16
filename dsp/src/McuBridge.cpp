#include "mixology/McuBridge.h"

#include "mixology/FaderLaw.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mixology::mcu
{
    namespace
    {
        constexpr std::uint8_t kHeader[] = { 0xF0, 0x00, 0x00, 0x66, 0x14 };   // Mackie Control (main unit)
        constexpr std::uint8_t kSerial[] = { 'M', 'I', 'X', 'O', 'L', 'G', 'Y' };
        constexpr std::uint8_t kChallenge[] = { 0x4D, 0x58, 0x31, 0x32 };

        // The built-in fader curves: dB = k * log10 (position / zero), so 0 dB lands where each DAW puts it and the
        // top is +6 dB. Logic: 0 dB at 12443 / 16383 (seen in captures). Cakewalk: 0 dB at 101 / 127 (its default
        // volume). The shape below 0 dB is a guess the display's readings correct.
        constexpr double kLogicZero = 12443.0 / 16383.0;
        constexpr double kCakewalkZero = 101.0 / 127.0;

        double zeroOf (Dialect d) { return d == Dialect::logic ? kLogicZero : kCakewalkZero; }
        double slopeOf (Dialect d) { return 6.0 / std::log10 (1.0 / zeroOf (d)); }

        std::string trim (const std::string& s)
        {
            const auto b = s.find_first_not_of (' ');
            if (b == std::string::npos) return {};
            const auto e = s.find_last_not_of (' ');
            return s.substr (b, e - b + 1);
        }

        // What Mixology is told: the Cubase script's text, so the plug-in reads every DAW the same way.
        std::string dbText (double db)
        {
            if (db <= -144.0) return "-oo";
            char buf[32];
            std::snprintf (buf, sizeof (buf), "%s%.2f", db > 0.005 ? "+" : "", db);
            return buf;
        }

        std::string panText (double pan)
        {
            const int v = static_cast<int> (std::lround (pan * 100.0));
            if (v == 0) return "C";
            return (v < 0 ? "L" : "R") + std::to_string (std::abs (v));
        }

        double clamp01 (double v) { return std::clamp (v, 0.0, 1.0); }

        bool isAlnum (char c) { return std::isalnum (static_cast<unsigned char> (c)) != 0 || (static_cast<unsigned char> (c) & 0x80) != 0; }

        std::string letters (const std::string& s)
        {
            std::string o;
            for (char c : s)
                if (isAlnum (c)) o.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (c))));
            return o;
        }

        bool subsequence (const std::string& small, const std::string& big)
        {
            std::size_t j = 0;
            for (char c : big)
                if (j < small.size() && c == small[j]) ++j;
            return j == small.size();
        }

        // Cakewalk's knob: n steps in one message move 0.005 * (3n - 2) of the pan's range (its surface source).
        double cakewalkStep (int n) { return 0.005 * (3.0 * n - 2.0); }
    }

    // ---- positions -------------------------------------------------------------------------------------

    double faderToNormalised (Dialect d, int pb)
    {
        pb = std::clamp (pb, 0, kFaderMax);
        if (d == Dialect::cakewalk) return (pb >> 4) / 1023.0;   // Cakewalk reads 10 bits
        return pb / double (kFaderMax);
    }

    int normalisedToFader (Dialect d, double n)
    {
        n = clamp01 (n);
        if (d == Dialect::cakewalk) return static_cast<int> (std::lround (n * 1023.0)) << 4;   // what it echoes back
        return static_cast<int> (std::lround (n * kFaderMax));
    }

    // ---- the fader curve ------------------------------------------------------------------------------

    McuLaw::McuLaw (Dialect d) : dialect (d) {}

    double McuLaw::builtInDb (Dialect d, double n)
    {
        n = clamp01 (n);
        if (n <= 1.0e-6) return FaderLaw::kMinusInf;
        return std::max (-200.0, slopeOf (d) * std::log10 (n / zeroOf (d)));
    }

    double McuLaw::builtInNormalised (Dialect d, double db)
    {
        if (db <= FaderLaw::kMinusInf + 1.0) return 0.0;
        return clamp01 (zeroOf (d) * std::pow (10.0, db / slopeOf (d)));
    }

    void McuLaw::learn (double n, double db)
    {
        if (! std::isfinite (n) || ! std::isfinite (db)) return;
        const int k = static_cast<int> (std::lround (clamp01 (n) * kFaderMax));
        db = std::max (db, FaderLaw::kMinusInf);
        // The curve only rises: a new reading replaces older points that contradict it.
        for (auto it = points.begin(); it != points.end();)
        {
            const bool contradicts = (it->first < k && it->second > db + 0.01) || (it->first > k && it->second < db - 0.01);
            it = contradicts ? points.erase (it) : std::next (it);
        }
        points[k] = db;
    }

    double McuLaw::toDb (double n) const
    {
        n = clamp01 (n);
        const int k = static_cast<int> (std::lround (n * kFaderMax));
        if (k == 0) return FaderLaw::kMinusInf;
        if (auto it = points.find (k); it != points.end()) return it->second;
        auto hi = points.lower_bound (k);
        const bool hasHi = hi != points.end() && hi->second > FaderLaw::kMinusInf;
        const bool hasLo = hi != points.begin() && std::prev (hi)->second > FaderLaw::kMinusInf;
        if (hasHi && hasLo)
        {
            const auto lo = std::prev (hi);
            // Between two readings: the built-in curve's shape, moved to pass through both.
            const double n0 = lo->first / double (kFaderMax), n1 = hi->first / double (kFaderMax);
            const double e0 = lo->second - builtInDb (dialect, n0), e1 = hi->second - builtInDb (dialect, n1);
            const double t = (n - n0) / std::max (1.0e-9, n1 - n0);
            return builtInDb (dialect, n) + e0 + t * (e1 - e0);
        }
        // Outside the readings: the built-in curve, moved toward the nearest one (less the further away).
        if (hasLo || hasHi)
        {
            const auto& p = hasLo ? *std::prev (hi) : *hi;
            const double dist = std::abs (p.first - k) / double (kFaderMax);
            const double offset = (p.second - builtInDb (dialect, p.first / double (kFaderMax))) * std::max (0.0, 1.0 - dist * 4.0);
            return builtInDb (dialect, n) + offset;
        }
        return builtInDb (dialect, n);
    }

    double McuLaw::toNormalised (double db) const
    {
        if (db <= FaderLaw::kMinusInf + 1.0) return 0.0;
        if (toDb (1.0) <= db) return 1.0;
        // toDb rises with the position: bisect it, then the nearer of the two 14-bit steps around it.
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 24; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (toDb (mid) < db ? lo : hi) = mid;
        }
        const double a = std::floor (lo * kFaderMax) / kFaderMax, b = std::ceil (hi * kFaderMax) / kFaderMax;
        return std::abs (toDb (a) - db) <= std::abs (toDb (b) - db) ? a : b;
    }

    std::string McuLaw::serialise() const
    {
        std::ostringstream s;
        bool first = true;
        for (const auto& [k, db] : points)
        {
            s << (first ? "" : ";") << k << ":" << db;
            first = false;
        }
        return s.str();
    }

    void McuLaw::deserialise (const std::string& text)
    {
        points.clear();
        std::istringstream in (text);
        std::string item;
        while (std::getline (in, item, ';'))
        {
            const auto colon = item.find (':');
            if (colon == std::string::npos) continue;
            const int k = std::atoi (item.substr (0, colon).c_str());
            const double db = std::atof (item.substr (colon + 1).c_str());
            if (k >= 0 && k <= kFaderMax && std::isfinite (db)) points[k] = db;
        }
    }

    // ---- the display's texts ---------------------------------------------------------------------------

    bool parseDisplayDb (const std::string& raw, double& db)
    {
        std::string t;
        for (char c : raw)
            if (c != ' ') t.push_back (static_cast<char> (std::tolower (static_cast<unsigned char> (c))));
        if (t.empty()) return false;
        if (t == "-inf" || t == "-infdb" || t == "-oo" || t == "-oodb" || t == "-\xE2\x88\x9E")
        {
            db = FaderLaw::kMinusInf;
            return true;
        }
        const bool hasDb = t.size() > 2 && t.compare (t.size() - 2, 2, "db") == 0;
        if (hasDb) t.erase (t.size() - 2);
        if (t.find ('.') == std::string::npos && ! hasDb) return false;   // "+32", "-64": pans
        char* end = nullptr;
        const double v = std::strtod (t.c_str(), &end);
        if (end == t.c_str() || *end != '\0' || ! std::isfinite (v) || v < -200.0 || v > 24.0) return false;
        db = v;
        return true;
    }

    bool parsePanText (Dialect d, const std::string& raw, double& pan)
    {
        std::string t;
        for (char c : raw)
            if (c != ' ') t.push_back (static_cast<char> (std::toupper (static_cast<unsigned char> (c))));
        if (t.empty()) return false;
        if (t == "C" || t == "<C>" || t == "CENTER" || t == "CNTR" || t == "CTR" || t == "0" || t == "+0" || t == "-0" || t == "C0")
        {
            pan = 0.0;
            return true;
        }
        if (d == Dialect::logic)
        {
            // Logic: -64 (left) .. 0 .. +63 (right), whole numbers.
            if (t.find ('.') != std::string::npos) return false;
            char* end = nullptr;
            const long v = std::strtol (t.c_str(), &end, 10);
            if (end == t.c_str() || *end != '\0' || v < -64 || v > 64) return false;
            pan = v < 0 ? static_cast<double> (v) / 64.0 : static_cast<double> (v) / 63.0;
            pan = std::clamp (pan, -1.0, 1.0);
            return true;
        }
        // Cakewalk: "L30", "30%L", "100%R" ... percent of the way to that side.
        std::string digits;
        char side = 0;
        for (char c : t)
        {
            if (c == 'L' || c == 'R') side = side == 0 ? c : side;
            else if (std::isdigit (static_cast<unsigned char> (c))) digits.push_back (c);
            else if (c != '%') return false;
        }
        if (side == 0 || digits.empty() || digits.size() > 3) return false;
        const int v = std::atoi (digits.c_str());
        if (v > 100) return false;
        pan = (side == 'L' ? -1.0 : 1.0) * v / 100.0;
        return true;
    }

    std::vector<std::string> expandNames (const std::vector<std::string>& shown, const std::vector<std::string>& fullNames,
                                          std::map<std::string, std::string>& seenAs)
    {
        const auto n = shown.size();
        std::vector<std::string> out (n);
        std::vector<std::vector<const std::string*>> cand (n);
        std::vector<bool> done (n, false);
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto& s = shown[i];
            if (s.empty()) { done[i] = true; continue; }
            if (std::find (fullNames.begin(), fullNames.end(), s) != fullNames.end()) { out[i] = s; done[i] = true; continue; }   // not shortened
            const auto ls = letters (s);
            for (const auto& f : fullNames)
            {
                const auto lf = letters (f);
                if (ls.empty() || lf.empty() || lf[0] != ls[0] || ! subsequence (ls, lf)) continue;
                if (const auto it = seenAs.find (f); it != seenAs.end() && it->second != s) continue;   // that track shows another text
                cand[i].push_back (&f);
            }
        }
        // A track one strip is sure of can't be another strip's (unless both show the same text: a doubled name).
        for (bool progress = true; progress;)
        {
            progress = false;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (done[i] || cand[i].size() != 1) continue;
                const auto* f = cand[i].front();
                out[i] = *f;
                done[i] = true;
                seenAs[*f] = shown[i];
                progress = true;
                for (std::size_t j = 0; j < n; ++j)
                    if (! done[j] && shown[j] != shown[i])
                        cand[j].erase (std::remove (cand[j].begin(), cand[j].end(), f), cand[j].end());
            }
        }
        for (std::size_t i = 0; i < n; ++i)
            if (! done[i]) out[i] = shown[i];   // fits several tracks (or none): as the display shows it
        return out;
    }

    std::string expandName (const std::string& shortName, const std::vector<std::string>& fullNames)
    {
        std::map<std::string, std::string> seenAs;
        return expandNames ({ shortName }, fullNames, seenAs).front();
    }

    // ---- the bridge ------------------------------------------------------------------------------------

    McuBridge::McuBridge (Dialect d) : kind (d), faderLaw (d)
    {
        lcd.fill (' ');
    }

    const char* McuBridge::dawName() const { return kind == Dialect::logic ? "Logic Pro" : "Cakewalk"; }

    std::vector<std::vector<std::uint8_t>> McuBridge::takeForMixology() { auto out = std::move (toMixology); toMixology.clear(); return out; }
    std::vector<std::vector<std::uint8_t>> McuBridge::takeForDaw() { auto out = std::move (toDaw); toDaw.clear(); return out; }

    void McuBridge::setTrackNames (std::vector<std::string> n)
    {
        std::sort (n.begin(), n.end());
        n.erase (std::unique (n.begin(), n.end()), n.end());
        names = std::move (n);
    }

    void McuBridge::reset (double now)
    {
        strips = {};
        seenAs.clear();
        lcd.fill (' ');
        seen = false;
        helloWaiting = false;
        displayShowsDb = false;
        movesWithoutDb = 0;
        later.clear();
        valueToggles = 0;
        valueCheckAt = -1.0;
        buttonsFrom = -1.0e9;
        waitingForBank = false;
        lastWake = -1.0e9;
        wake (now);
    }

    void McuBridge::send (bridge::Cmd cmd, const bridge::Message& in)
    {
        auto m = in;
        m.cmd = cmd;
        toMixology.push_back (bridge::encode (m));
    }

    void McuBridge::announce (std::uint16_t token)
    {
        bridge::Message a;
        a.flag = bridge::kLatestScriptVersion;   // behaves as the current Cubase script does
        a.slot = kStrips;
        a.token = token;
        send (bridge::Cmd::helloAck, a);
    }

    void McuBridge::sendAck (std::uint16_t token, bridge::AckStatus status)
    {
        bridge::Message m;
        m.token = token;
        m.flag = static_cast<int> (status);
        send (bridge::Cmd::ack, m);
    }

    void McuBridge::log (const std::string& text)
    {
        for (std::size_t i = 0; i < text.size(); i += 56)
        {
            bridge::Message m;
            m.text = (i ? "  " : "") + text.substr (i, 56);
            send (bridge::Cmd::log, m);
        }
    }

    void McuBridge::note (std::uint8_t n, std::uint8_t velocity)
    {
        toDaw.push_back ({ 0x90, n, velocity });
    }

    void McuBridge::press (std::uint8_t n, double now)
    {
        if (now < buttonsFrom)   // Logic isn't listening to buttons yet: they wait
        {
            later.push_back ({ buttonsFrom, { 0x90, n, 0x7F } });
            later.push_back ({ buttonsFrom, { 0x90, n, 0x00 } });
            return;
        }
        note (n, 0x7F);
        note (n, 0x00);
    }

    void McuBridge::sysexReply (std::uint8_t cmd, const std::vector<std::uint8_t>& payload)
    {
        std::vector<std::uint8_t> r (std::begin (kHeader), std::end (kHeader));
        r.push_back (cmd);
        r.insert (r.end(), payload.begin(), payload.end());
        r.push_back (0xF7);
        toDaw.push_back (r);
    }

    void McuBridge::wake (double now)
    {
        // "A Mackie Control just switched on": Logic answers with its handshake, Cakewalk starts over and sends
        // everything (names, faders, lights). Only while the DAW hasn't spoken.
        if (seen || now - lastWake < kWakeInterval) return;
        lastWake = now;
        std::vector<std::uint8_t> p (std::begin (kSerial), std::end (kSerial));
        p.insert (p.end(), std::begin (kChallenge), std::end (kChallenge));
        sysexReply (0x01, p);
    }

    void McuBridge::markSeen (double)
    {
        if (seen) return;
        seen = true;
        if (helloWaiting)   // Mixology asked while the DAW was still silent: answer now
        {
            helloWaiting = false;
            announce (helloToken);
        }
    }

    bool McuBridge::settled (int i, double now) const
    {
        const auto& s = strips[static_cast<std::size_t> (i)];
        return s.topChangedAt < 0.0 || now - s.topChangedAt >= kSettle;
    }

    // ---- from Mixology -----------------------------------------------------------------------------------

    void McuBridge::fromMixology (const std::vector<std::uint8_t>& frame, double now)
    {
        if (const auto m = bridge::decode (frame))
            handle (*m, now);
    }

    McuBridge::Strip* McuBridge::target (const bridge::Message& m, double now)
    {
        if (m.slot < 0 || m.slot >= kStrips)
        {
            sendAck (m.token, bridge::AckStatus::emptySlot);
            return nullptr;
        }
        auto& s = strips[static_cast<std::size_t> (m.slot)];
        if (! s.titleSent || s.sentTitle.empty())
        {
            sendAck (m.token, bridge::AckStatus::emptySlot);
            return nullptr;
        }
        // While the bank moves or the strip's name is being rewritten, it may already show another channel. (A strip
        // Mixology just moved redraws its own name: that is no new channel.)
        const auto shownNow = titles()[static_cast<std::size_t> (m.slot)];
        const bool steady = settled (m.slot, now) || now < s.nameHoldUntil;
        if (s.hash != m.hash || m.channelId != 0 || waitingForBank || ! steady || shownNow != s.sentTitle)
        {
            log ("Refused: slot " + std::to_string (m.slot + 1) + " is changing (now \"" + shownNow + "\")");
            sendAck (m.token, bridge::AckStatus::nameMismatch);
            return nullptr;
        }
        return &s;
    }

    void McuBridge::handle (const bridge::Message& m, double now)
    {
        using bridge::Cmd;
        switch (m.cmd)
        {
            case Cmd::hello:
                if (seen)
                {
                    announce (m.token);
                }
                else
                {
                    // Nothing from the DAW yet: no answer (Mixology keeps saying it waits) until it speaks.
                    helloWaiting = true;
                    helloToken = m.token;
                    wake (now);
                }
                return;
            case Cmd::requestDump:
                if (seen) dump (now);
                return;
            case Cmd::ping:
                if (seen)
                {
                    bridge::Message p;
                    p.token = m.token;
                    send (Cmd::pong, p);
                }
                return;
            case Cmd::setVolume:
                if (auto* s = target (m, now))
                {
                    sendFader (static_cast<int> (s - strips.data()), m.value, now);
                    sendAck (m.token, bridge::AckStatus::applied);
                }
                break;
            case Cmd::setPan:
                if (auto* s = target (m, now))
                {
                    const int i = static_cast<int> (s - strips.data());
                    s->panTarget = std::clamp (m.value * 2.0 - 1.0, -1.0, 1.0);
                    s->panMoving = true;
                    s->panSteps = 0;
                    s->panSentAt = -1.0;
                    stepPan (i, now);
                    sendAck (m.token, bridge::AckStatus::applied);
                }
                break;
            case Cmd::setMute:
            case Cmd::setSolo:
                if (auto* s = target (m, now))
                {
                    const int i = static_cast<int> (s - strips.data());
                    const bool on = m.flag != 0;
                    const bool isMute = m.cmd == Cmd::setMute;
                    bool& state = isMute ? s->mute : s->solo;
                    if (state != on)   // the buttons toggle
                    {
                        press (static_cast<std::uint8_t> ((isMute ? kNoteMute : kNoteSolo) + i), now);
                        state = on;    // the DAW lights the button to confirm; until then, what was asked
                    }
                    sendAck (m.token, bridge::AckStatus::applied);
                }
                break;
            case Cmd::bank:
            {
                const auto action = static_cast<bridge::BankAction> (m.flag);
                int presses = 1;
                std::uint8_t button = kNoteBankLeft;
                switch (action)
                {
                    case bridge::BankAction::reset: button = kNoteBankLeft; presses = 32; break;   // the DAW stops at the first
                    case bridge::BankAction::previous: button = kNoteBankLeft; break;
                    case bridge::BankAction::next: button = kNoteBankRight; break;
                    case bridge::BankAction::shiftLeft: button = kNoteChannelLeft; break;
                    case bridge::BankAction::shiftRight: button = kNoteChannelRight; break;
                }
                for (int k = 0; k < presses; ++k) press (button, now);
                waitingForBank = true;
                bankSentAt = std::max (now, buttonsFrom);
                bankAnswered = false;
                for (auto& s : strips) s.nameHoldUntil = -1.0;   // new channels: their names are real
                break;
            }
            default:
                break;
        }
        flush (false, now);
    }

    void McuBridge::sendFader (int i, double normalised, double now)
    {
        auto& s = strips[static_cast<std::size_t> (i)];
        const int value = normalisedToFader (kind, normalised);
        if (s.touchUntil < now)
            note (static_cast<std::uint8_t> (kNoteTouch + i), 0x7F);   // touch it first, as a hand would
        toDaw.push_back ({ static_cast<std::uint8_t> (0xE0 + i), static_cast<std::uint8_t> (value & 0x7F),
                           static_cast<std::uint8_t> ((value >> 7) & 0x7F) });
        s.touchUntil = now + kTouchHold;
        s.fader = value;   // the DAW doesn't answer a fader it was moved to; this is where it is now
        s.hasFader = true;
        s.faderChangedAt = now;
        s.hasDisplayDb = false;
        s.nameHoldUntil = now + kNameHold;   // Logic may show "Volume" and the value for a moment
        // Give the display a moment to say what dB this really is (unless this DAW never shows it).
        const bool noDbShown = ! displayShowsDb && movesWithoutDb >= kMovesBeforeNoWait;
        s.volumeHoldUntil = noDbShown ? now : now + kReadWait;
        s.holdPending = ! noDbShown;
    }

    void McuBridge::stepPan (int i, double now)
    {
        auto& s = strips[static_cast<std::size_t> (i)];
        if (! s.panMoving) return;
        const double from = s.hasPan ? s.pan : 0.0;
        const double delta = s.panTarget - from;
        const double unit = kind == Dialect::logic ? 1.0 / 64.0 : 0.01;   // one knob step, in pan (-1 .. 1)
        if (std::abs (delta) < 0.5 * unit || s.panSteps >= kMaxPanSteps || ! s.hasPan)
        {
            s.panMoving = false;
            return;
        }
        const bool right = delta > 0.0;
        auto turn = [&] (int n) {
            toDaw.push_back ({ 0xB0, static_cast<std::uint8_t> (kCcVPot + i), static_cast<std::uint8_t> ((right ? 0x00 : 0x40) | n) });
        };
        if (kind == Dialect::cakewalk)
        {
            // Its knob accelerates: n steps in one message move 0.005 * (3n - 2) of the range (0..1, half a pan unit).
            double left = std::abs (delta) / 2.0;
            int messages = 0;
            while (left > 0.0025 && messages < 40)
            {
                int n = std::clamp (static_cast<int> (std::floor ((left / 0.005 + 2.0) / 3.0)), 1, 15);
                if (cakewalkStep (n) > left + 0.0025) n = 1;
                turn (n);
                left -= cakewalkStep (n);
                ++messages;
            }
        }
        else
        {
            // Logic: one step per message (several in one may count extra: its knobs accelerate).
            const int ticks = std::clamp (static_cast<int> (std::lround (std::abs (delta) / unit)), 1, 64);
            for (int k = 0; k < ticks; ++k) turn (1);
        }
        // Where it should be now, until the display's text (or the ring) says where it really is.
        s.pan = s.panTarget;
        s.panFromText = false;
        ++s.panSteps;
        s.panSentAt = now;
        s.panReadAt = -1.0;
        s.nameHoldUntil = now + kNameHold;   // Cakewalk shows the knob's value where the name goes
    }

    // ---- from the DAW ------------------------------------------------------------------------------------

    void McuBridge::fromDaw (const std::uint8_t* d, std::size_t size, double now)
    {
        if (d == nullptr || size == 0) return;
        if (d[0] == 0xF0)
        {
            onSysex (d, size, now);
            return;
        }
        const std::uint8_t status = d[0] & 0xF0, channel = d[0] & 0x0F;
        if (status == 0xE0 && size >= 3)
        {
            markSeen (now);
            if (channel >= kStrips) return;   // the master fader: Mixology leaves it alone
            auto& s = strips[channel];
            if (s.touchUntil >= now) return;  // Mixology holds it: the DAW's echo would pull it back
            const int v = (d[2] << 7) | d[1];
            if (! s.hasFader || v != s.fader)
            {
                s.fader = v;
                s.faderChangedAt = now;
                s.hasDisplayDb = false;
            }
            s.hasFader = true;
        }
        else if ((status == 0x90 || status == 0x80) && size >= 3)
        {
            markSeen (now);
            const int n = d[1];
            const bool on = status == 0x90 && d[2] >= 0x40;
            if (n >= kNoteSolo && n < kNoteSolo + kStrips) strips[static_cast<std::size_t> (n - kNoteSolo)].solo = on;
            else if (n >= kNoteMute && n < kNoteMute + kStrips) strips[static_cast<std::size_t> (n - kNoteMute)].mute = on;
        }
        else if (status == 0xB0 && size >= 3)
        {
            markSeen (now);
            const int cc = d[1];
            if (cc >= kCcRing && cc < kCcRing + kStrips)
            {
                auto& s = strips[static_cast<std::size_t> (cc - kCcRing)];
                const int pos = d[2] & 0x0F;
                s.ring = pos;
                // The ring is coarse (11 lights): the display's pan text wins when there is one, and what Mixology
                // set stands while the ring agrees with it.
                const double ringPan = (pos - 6) / 5.0;
                if (pos >= 1 && pos <= 11 && ! s.panFromText && ! s.panMoving && (! s.hasPan || std::abs (s.pan - ringPan) > 0.1))
                {
                    s.pan = ringPan;
                    s.hasPan = true;
                }
            }
        }
        else if (status == 0xD0)
        {
            markSeen (now);   // meters: the DAW is there
        }
    }

    void McuBridge::onSysex (const std::uint8_t* d, std::size_t size, double now)
    {
        if (size < 7 || d[size - 1] != 0xF7) return;
        if (! std::equal (std::begin (kHeader), std::begin (kHeader) + 4, d)) return;
        if (d[4] != 0x14) return;   // another unit (an extender, a C4): not us
        switch (d[5])
        {
            case 0x00:   // Logic: device query. Answer with the "host connection query" (serial, challenge).
            {
                std::vector<std::uint8_t> p (std::begin (kSerial), std::end (kSerial));
                p.insert (p.end(), std::begin (kChallenge), std::end (kChallenge));
                sysexReply (0x01, p);
                return;   // not "seen" yet: the handshake isn't done
            }
            case 0x02:   // Logic: its answer to our query. Confirm; it then sends everything (and ignores buttons a while).
                sysexReply (0x03, { std::begin (kSerial), std::end (kSerial) });
                buttonsFrom = now + kLogicButtonPause;
                break;
            case 0x13:   // firmware version
                sysexReply (0x14, { 'V', '1', '.', '0', '0' });
                return;
            case 0x1A:   // Cakewalk with its handshake on: our serial number
                sysexReply (0x1B, { std::begin (kSerial), std::end (kSerial) });
                return;
            case 0x12:   // the display: offset, then characters
            {
                if (size < 8) return;
                int at = d[6];
                for (std::size_t k = 7; k + 1 < size && at < static_cast<int> (lcd.size()); ++k, ++at)
                {
                    const char c = (d[k] >= 0x20 && d[k] < 0x7F) ? static_cast<char> (d[k]) : ' ';
                    auto& cell = lcd[static_cast<std::size_t> (at)];
                    if (cell == c) continue;
                    cell = c;
                    if (at < 56)
                    {
                        strips[static_cast<std::size_t> (at / 7)].topChangedAt = now;
                        if (waitingForBank) bankAnswered = true;
                    }
                }
                break;
            }
            case 0x62:   // all lights off
                for (auto& s : strips) s.mute = s.solo = false;
                break;
            default:
                break;
        }
        markSeen (now);
    }

    std::string McuBridge::segment (int strip, int row) const
    {
        const auto at = static_cast<std::size_t> (row * 56 + strip * 7);
        return trim (std::string (lcd.data() + at, 7));
    }

    bool McuBridge::isPassingText (const std::string& text) const
    {
        // A value or a parameter's label where a name goes (shown for a moment): not a channel name.
        double ignored = 0.0;
        if (parseDisplayDb (text, ignored) || parsePanText (kind, text, ignored)) return true;
        static const char* labels[] = { "Volume", "Vol", "Pan", "Level", "Panner", "Width" };
        for (const char* l : labels)
            if (text == l) return true;
        return false;
    }

    std::array<std::string, kStrips> McuBridge::titles()
    {
        std::vector<std::string> shown (kStrips);
        for (int i = 0; i < kStrips; ++i)
            shown[static_cast<std::size_t> (i)] = strips[static_cast<std::size_t> (i)].name;
        const auto full = expandNames (shown, names, seenAs);
        std::array<std::string, kStrips> out;
        std::copy (full.begin(), full.end(), out.begin());
        return out;
    }

    void McuBridge::readDisplay (double now)
    {
        // The names. A strip Mixology didn't just touch that shows a new name means the DAW changed what the
        // surface shows (a bank move, a rename): then every new name counts. Otherwise text in the place of a
        // strip Mixology just moved is passing (a knob's value, "Volume") and the name stays.
        bool othersChanged = false;
        for (int i = 0; i < kStrips; ++i)
        {
            const auto& s = strips[static_cast<std::size_t> (i)];
            const auto top = segment (i, 0);
            if (settled (i, now) && top != s.name && now >= s.nameHoldUntil && ! isPassingText (top)) othersChanged = true;
        }
        const bool takeAll = waitingForBank || othersChanged;
        for (int i = 0; i < kStrips; ++i)
        {
            auto& s = strips[static_cast<std::size_t> (i)];
            const auto top = segment (i, 0);
            if (! settled (i, now) || top == s.name) continue;
            double p = 0.0;
            if (now < s.nameHoldUntil && parsePanText (kind, top, p))   // Cakewalk: the knob's value, for a while
            {
                s.pan = p;
                s.hasPan = true;
                s.panFromText = true;
                s.panReadAt = now;
            }
            if (takeAll)
            {
                s.name = isPassingText (top) ? std::string() : top;   // not yet known: no channel until its name shows
                s.nameHoldUntil = -1.0;
            }
            else if (now >= s.nameHoldUntil && ! isPassingText (top))
            {
                s.name = top;
            }
        }

        for (int i = 0; i < kStrips; ++i)
        {
            auto& s = strips[static_cast<std::size_t> (i)];
            const auto value = segment (i, 1);
            if (value != s.valueText)
            {
                s.valueText = value;
                s.valueChangedAt = now;
                double p = 0.0;
                if (parsePanText (kind, value, p))
                {
                    s.pan = p;
                    s.hasPan = true;
                    s.panFromText = true;
                    s.panReadAt = now;
                }
            }
            // A dB value under a fader that stood still while the text settled: a point on the DAW's curve. Only
            // text the DAW wrote after the fader's last move counts: a display that kept an older value (it may
            // show only the last touched fader) would bend the curve.
            double db = 0.0;
            if (! s.hasDisplayDb && s.hasFader && s.valueChangedAt > s.faderChangedAt && parseDisplayDb (s.valueText, db)
                && now - s.valueChangedAt >= kSettle && now - s.faderChangedAt >= kSettle)
            {
                s.hasDisplayDb = true;
                s.holdPending = false;
                displayShowsDb = true;
                faderLaw.learn (faderToNormalised (kind, s.fader), db);
            }
        }
    }

    void McuBridge::tick (double now)
    {
        // Buttons that waited for Logic to listen.
        if (! later.empty() && now >= later.front().at)
        {
            for (auto& l : later) toDaw.push_back (std::move (l.bytes));
            later.clear();
        }

        // Release faders Mixology let go of.
        for (int i = 0; i < kStrips; ++i)
        {
            auto& s = strips[static_cast<std::size_t> (i)];
            if (s.touchUntil >= 0.0 && now > s.touchUntil)
            {
                s.touchUntil = -1.0;
                note (static_cast<std::uint8_t> (kNoteTouch + i), 0x00);
            }
        }

        readDisplay (now);
        for (auto& s : strips)
            if (s.holdPending && now >= s.volumeHoldUntil)
            {
                s.holdPending = false;   // the wait ran out without a dB on the display
                if (! displayShowsDb) ++movesWithoutDb;
            }
        if (waitingForBank)
        {
            bool all = true;
            for (int i = 0; i < kStrips; ++i) all = all && settled (i, now);
            if ((bankAnswered && all) || now - bankSentAt >= kBankWait)
            {
                waitingForBank = false;
                readDisplay (now);   // the names the bank shows now, before anything is reported
            }
        }

        // Cakewalk shows the knobs' labels ("Pan") under the names until NAME/VALUE is pressed: Mixology wants values.
        if (kind == Dialect::cakewalk && seen && valueToggles < 2 && now >= valueCheckAt)
        {
            bool label = false, value = false;
            for (int i = 0; i < kStrips; ++i)
            {
                const auto& s = strips[static_cast<std::size_t> (i)];
                if (s.name.empty() || s.valueText.empty()) continue;
                double p = 0.0;
                (parsePanText (kind, s.valueText, p) || parseDisplayDb (s.valueText, p) ? value : label) = true;
            }
            if (label && ! value)
            {
                press (kNoteNameValue, now);
                ++valueToggles;
                valueCheckAt = now + 1.0;
            }
            else if (value)
            {
                valueToggles = 2;   // values it is
            }
        }

        for (int i = 0; i < kStrips; ++i)
        {
            auto& s = strips[static_cast<std::size_t> (i)];
            if (s.panMoving && (s.panReadAt >= s.panSentAt || now - s.panSentAt >= kPanStepWait))
                stepPan (i, now);
        }

        wake (now);
        flush (false, now);
    }

    void McuBridge::flush (bool everything, double now)
    {
        if (! seen) return;
        const auto shownNow = titles();
        for (int i = 0; i < kStrips; ++i)
        {
            auto& s = strips[static_cast<std::size_t> (i)];
            if ((! waitingForBank && settled (i, now)) || everything || ! s.titleSent)
            {
                const auto& title = waitingForBank && s.titleSent ? s.sentTitle : shownNow[static_cast<std::size_t> (i)];
                if (everything || ! s.titleSent || title != s.sentTitle)
                {
                    s.titleSent = true;
                    s.sentTitle = title;
                    s.hash = title.empty() ? 0 : bridge::nameHash (title);
                    s.sentVolume.clear();
                    s.sentPan.clear();
                    s.sentVolumeValue = s.sentPanValue = -1.0;
                    s.sentMute = s.sentSolo = -1;
                    bridge::Message t;
                    t.slot = i;
                    t.hash = s.hash;
                    t.text = title;
                    send (bridge::Cmd::slotTitle, t);
                    if (! title.empty())
                    {
                        bridge::Message a;
                        a.slot = i;
                        a.flag = 0;   // the surface isn't told per channel whether it writes automation
                        send (bridge::Cmd::slotAutomation, a);
                    }
                }
            }
            if (! s.titleSent || s.sentTitle.empty()) continue;

            if (s.hasFader && (everything || s.hasDisplayDb || now >= s.volumeHoldUntil))
            {
                const double n = faderToNormalised (kind, s.fader);
                const auto text = dbText (faderLaw.toDb (n));
                if (everything || text != s.sentVolume || std::abs (n - s.sentVolumeValue) > 1.0e-7)
                {
                    s.sentVolume = text;
                    s.sentVolumeValue = n;
                    bridge::Message v;
                    v.slot = i;
                    v.value = n;
                    v.text = text;
                    send (bridge::Cmd::slotVolume, v);
                }
            }
            if (! s.panMoving || everything)
            {
                const double value = s.hasPan ? (s.pan + 1.0) / 2.0 : 0.5;
                const auto text = s.hasPan ? panText (s.pan) : std::string();
                if (everything || text != s.sentPan || std::abs (value - s.sentPanValue) > 1.0e-6)
                {
                    s.sentPan = text;
                    s.sentPanValue = value;
                    bridge::Message p;
                    p.slot = i;
                    p.value = value;
                    p.text = text;
                    send (bridge::Cmd::slotPan, p);
                }
            }
            if (everything || s.sentMute != (s.mute ? 1 : 0))
            {
                s.sentMute = s.mute ? 1 : 0;
                bridge::Message f;
                f.slot = i;
                f.flag = s.sentMute;
                send (bridge::Cmd::slotMute, f);
            }
            if (everything || s.sentSolo != (s.solo ? 1 : 0))
            {
                s.sentSolo = s.solo ? 1 : 0;
                bridge::Message f;
                f.slot = i;
                f.flag = s.sentSolo;
                send (bridge::Cmd::slotSolo, f);
            }
        }
    }

    void McuBridge::dump (double now)
    {
        flush (true, now);
        int named = 0;
        for (int i = 0; i < kStrips; ++i)
        {
            const auto& s = strips[static_cast<std::size_t> (i)];
            if (s.sentTitle.empty()) continue;
            ++named;
            log ("slot " + std::to_string (i + 1) + " \"" + s.sentTitle + "\"" + (s.name != s.sentTitle ? " (display: \"" + s.name + "\")" : ""));
        }
        log (std::string (dawName()) + " Mackie Control: " + std::to_string (named) + " named channel(s) on the surface, fader curve "
             + std::to_string (faderLaw.learned()) + " reading(s)" + (displayShowsDb ? ", the display shows dB" : ""));
        log ("errors " + std::to_string (errors));
    }

    McuBridge::StripView McuBridge::view (int i) const
    {
        const auto& s = strips.at (static_cast<std::size_t> (i));
        StripView v;
        v.name = s.name;
        v.title = s.sentTitle;
        v.fader = s.fader;
        v.mute = s.mute;
        v.solo = s.solo;
        v.pan = s.pan;
        v.hasPan = s.hasPan;
        v.valueText = s.valueText;
        return v;
    }
}
