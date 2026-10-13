// Line-based wrapper around mixology::BridgeSession for the end-to-end simulator (run.js).
// stdin:  "in <hex>"                 a SysEx frame from the Cubase script
//         "tick <now>"               advance the session clock
//         "cmd <name> <arg> <now>"   a UI command (lock, setDb, nudge, pan, mute, solo, restore, bank, unlock)
// stdout: zero or more "out <hex>" lines (frames for the script), then "state <json>", then "end".

#include "mixology/BridgeSession.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

using namespace mixology;

namespace
{
    std::string toHex (const std::vector<std::uint8_t>& b)
    {
        static const char* d = "0123456789abcdef";
        std::string s;
        for (auto c : b) { s.push_back (d[c >> 4]); s.push_back (d[c & 15]); }
        return s;
    }

    std::vector<std::uint8_t> fromHex (const std::string& s)
    {
        std::vector<std::uint8_t> b;
        for (std::size_t i = 0; i + 1 < s.size(); i += 2)
            b.push_back (static_cast<std::uint8_t> (std::stoi (s.substr (i, 2), nullptr, 16)));
        return b;
    }

    std::string esc (const std::string& s)
    {
        std::string o = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\') { o.push_back ('\\'); o.push_back (static_cast<char> (c)); }
            else if (c < 0x20) { char buf[8]; std::snprintf (buf, sizeof (buf), "\\u%04x", c); o += buf; }
            else o.push_back (static_cast<char> (c));
        }
        return o + "\"";
    }

    void printState (BridgeSession& s)
    {
        std::ostringstream j;
        j << "{\"link\":" << esc (toString (s.link()))
          << ",\"lock\":" << esc (s.lock() == BridgeSession::Lock::none ? "none" : s.lock() == BridgeSession::Lock::locked ? "locked" : "paused")
          << ",\"lockedSlot\":" << s.lockedSlot()
          << ",\"pauseReason\":" << esc (s.pauseReason())
          << ",\"moving\":" << (s.moveInProgress() ? "true" : "false")
          << ",\"lastMove\":" << esc (s.lastMoveResult())
          << ",\"slots\":[";
        for (int i = 0; i < s.slotCount(); ++i)
        {
            const auto& sl = s.slot (i);
            j << (i ? "," : "") << "{\"title\":" << esc (sl.title) << ",\"volume\":" << sl.volume
              << ",\"volumeText\":" << esc (sl.volumeText) << ",\"pan\":" << sl.pan << ",\"panText\":" << esc (sl.panText)
              << ",\"mute\":" << (sl.mute ? "true" : "false") << ",\"solo\":" << (sl.solo ? "true" : "false")
              << ",\"write\":" << (sl.automationWrite ? "true" : "false") << ",\"id\":" << sl.channelId << "}";
        }
        j << "],\"log\":[";
        bool first = true;
        for (const auto& l : s.logLines()) { j << (first ? "" : ",") << esc (l.text); first = false; }
        j << "]}";
        std::cout << "state " << j.str() << "\n";
    }

    void flush (BridgeSession& s)
    {
        for (const auto& m : s.takeOutgoing())
            std::cout << "out " << toHex (m) << "\n";
        printState (s);
        std::cout << "end" << std::endl;
    }
}

int main()
{
    BridgeSession session (77);
    session.setPortsOpen (true, 0.0, "simulator");

    std::string line;
    while (std::getline (std::cin, line))
    {
        std::istringstream in (line);
        std::string op;
        in >> op;
        if (op == "in")
        {
            std::string hex;
            double now = 0.0;
            in >> hex >> now;
            const auto bytes = fromHex (hex);
            session.receive (bytes.data(), bytes.size(), now);
            continue;   // no reply: the next tick reports
        }
        if (op == "tick")
        {
            double now = 0.0;
            in >> now;
            session.tick (now);
            flush (session);
        }
        else if (op == "cmd")
        {
            std::string name;
            double arg = 0.0, now = 0.0;
            in >> name >> arg >> now;
            BridgeSession::Result r { true, {} };
            if (name == "lock") r = session.lockChannel (static_cast<int> (arg), now);
            else if (name == "unlock") session.unlock (now);
            else if (name == "setDb") r = session.setVolumeDb (arg, now);
            else if (name == "minusInf") r = session.setVolumeDb (FaderLaw::kMinusInf, now);
            else if (name == "nudge") r = session.nudgeDb (arg, now);
            else if (name == "pan") r = session.setPan (arg, now);
            else if (name == "mute") r = session.setMute (arg != 0.0, now);
            else if (name == "solo") r = session.setSolo (arg != 0.0, now);
            else if (name == "restore") r = session.restore (now);
            else if (name == "bank") r = session.bank (static_cast<bridge::BankAction> (static_cast<int> (arg)), now);
            else r = { false, "unknown command" };
            std::cout << "result " << (r.ok ? "ok " : "fail ") << r.message << "\n";
            flush (session);
        }
        else if (op == "quit")
        {
            break;
        }
    }
    return 0;
}
