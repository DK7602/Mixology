// Line-based wrapper around mixology::BridgeSession for the end-to-end simulator (run.js).
// stdin:  "in <hex>"                 a SysEx frame from the Cubase script
//         "tick <now>"               advance the session clock
//         "cmd <name> <slot> <hash> <arg> <now>"
//                                    a UI command on the strip showing channel <hash> in <slot>: setDb, minusInf,
//                                    nudge, fader, faderEnd, pan, mute, solo, protect, restore; or a global one
//                                    (slot / hash ignored): restoreAll, bank, refresh, forget
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
          << ",\"changed\":" << s.changedCount()
          << ",\"scanning\":" << (s.scanning() ? "true" : "false")
          << ",\"knownFaders\":" << s.knownFaders().size()
          << ",\"applying\":" << (s.applying() ? "true" : "false")
          << ",\"plan\":{\"done\":" << s.planProgress().done << ",\"skipped\":" << s.planProgress().skipped
          << ",\"serial\":" << s.planProgress().serial << ",\"undo\":" << s.planProgress().undo.size() << "}"
          << ",\"moving\":" << (s.moveInProgress() ? "true" : "false")
          << ",\"lastMove\":" << esc (s.lastMoveResult())
          << ",\"slots\":[";
        for (int i = 0; i < s.slotCount(); ++i)
        {
            const auto& sl = s.slot (i);
            j << (i ? "," : "") << "{\"title\":" << esc (sl.title) << ",\"hash\":" << sl.hash << ",\"volume\":" << sl.volume
              << ",\"volumeText\":" << esc (sl.volumeText) << ",\"pan\":" << sl.pan << ",\"panText\":" << esc (sl.panText)
              << ",\"mute\":" << (sl.mute ? "true" : "false") << ",\"solo\":" << (sl.solo ? "true" : "false")
              << ",\"read\":" << (sl.automationRead ? "true" : "false")
              << ",\"write\":" << (sl.automationWrite ? "true" : "false") << ",\"id\":" << sl.channelId
              << ",\"protected\":" << (s.isProtected (sl.hash) && sl.present ? "true" : "false")
              << ",\"duplicate\":" << (s.nameIsDuplicated (i) ? "true" : "false")
              << ",\"noPan\":" << (sl.present && s.panIgnored (sl.hash) ? "true" : "false") << "}";
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
            ChannelRef ref;
            double arg = 0.0, now = 0.0;
            in >> name >> ref.slot >> ref.hash >> arg >> now;
            BridgeSession::Result r { true, {} };
            if (name == "setDb") r = session.setVolumeDb (ref, arg, now);
            else if (name == "minusInf") r = session.setVolumeDb (ref, FaderLaw::kMinusInf, now);
            else if (name == "nudge") r = session.nudgeDb (ref, arg, now);
            else if (name == "fader") r = session.setVolumeNormalised (ref, arg, now);
            else if (name == "faderEnd") session.noteDragFinished (ref, now);
            else if (name == "pan") r = session.setPan (ref, arg, now);
            else if (name == "mute") r = session.setMute (ref, arg != 0.0, now);
            else if (name == "solo") r = session.setSolo (ref, arg != 0.0, now);
            else if (name == "protect") r = session.setProtected (ref, arg != 0.0, now);
            else if (name == "restore") r = session.restoreChannel (ref, now);
            else if (name == "restoreAll") { auto sum = session.restoreAll (now); r = { sum.restored > 0, sum.message }; }
            else if (name == "bank") r = session.bank (static_cast<bridge::BankAction> (static_cast<int> (arg)), now);
            else if (name == "refresh") session.requestDump (now);
            else if (name == "forget") session.forgetRestorePoints (now);
            else if (name == "scan") r = session.scanAllChannels (now);
            else r = { false, "unknown command" };
            std::cout << "result " << (r.ok ? "ok " : "fail ") << r.message << (r.warning.empty() ? "" : " WARNING " + r.warning) << "\n";
            flush (session);
        }
        else if (op == "plan" || op == "undo")
        {
            // plan <now> name|db|pan;name|db|pan...   (pan "-" = leave it). undo <now>: the last plan's undo.
            double now = 0.0;
            in >> now;
            std::vector<BridgeSession::PlanStep> steps;
            if (op == "undo")
                steps = session.planProgress().undo;
            else
            {
                std::string rest;
                std::getline (in, rest);
                std::istringstream list (rest.substr (rest.find_first_not_of (' ')));
                std::string entry;
                while (std::getline (list, entry, ';'))
                {
                    std::istringstream fields (entry);
                    std::string name, db, pan;
                    std::getline (fields, name, '|');
                    std::getline (fields, db, '|');
                    std::getline (fields, pan, '|');
                    BridgeSession::PlanStep st;
                    st.hash = bridge::nameHash (name);
                    st.title = name;
                    st.setVolume = db != "-";
                    st.db = st.setVolume ? std::stod (db) : 0.0;
                    st.setPan = pan != "-" && ! pan.empty();
                    st.pan = st.setPan ? std::stod (pan) : 0.5;
                    steps.push_back (st);
                }
            }
            const auto r = session.applyPlan (op == "undo" ? "Undo" : "Plan", steps, now);
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
