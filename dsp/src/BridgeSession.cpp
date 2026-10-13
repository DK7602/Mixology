#include "mixology/BridgeSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mixology
{
    using namespace bridge;

    namespace
    {
        constexpr double kMaxDb = 6.0;          // top of a Cubase channel fader
        constexpr double kUserMoveSettle = 0.3; // log a Cubase-side fader move once it has settled

        std::string quoted (const std::string& s) { return "'" + s + "'"; }
        std::string slotName (int slot) { return "slot " + std::to_string (slot + 1); }
    }

    std::string formatDb (double db)
    {
        if (db <= FaderLaw::kMinusInf + 1.0) return "-inf dB";
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%+.2f dB", db);
        return buf;
    }

    const char* toString (BridgeSession::Link l)
    {
        switch (l)
        {
            case BridgeSession::Link::offline:   return "offline";
            case BridgeSession::Link::waiting:   return "waiting";
            case BridgeSession::Link::connected: return "connected";
            case BridgeSession::Link::lost:      return "lost";
        }
        return "?";
    }

    BridgeSession::BridgeSession (std::uint16_t nonce)
        : sessionNonce (static_cast<std::uint16_t> (nonce & 0x3FFF))
    {
    }

    // ---------------------------------------------------------------------------------------------
    // Transport

    void BridgeSession::setPortsOpen (bool open, double now, const std::string& detail)
    {
        if (open)
        {
            linkState = Link::waiting;
            lastHelloSent = -1.0e9;
            addLog (now, "MIDI ports open" + (detail.empty() ? std::string() : " (" + detail + ")")
                             + ". Waiting for the Mixology Bridge script in Cubase...");
        }
        else
        {
            if (linkState != Link::offline)
                addLog (now, "MIDI ports closed" + (detail.empty() ? std::string() : ": " + detail));
            linkState = Link::offline;
            if (lockState == Lock::locked)
            {
                pause ("The MIDI ports are closed.", now);
                resumeIfSameAfterReconnect = true;
            }
            move.active = false;
        }
        changed();
    }

    void BridgeSession::receive (const std::uint8_t* data, std::size_t size, double now)
    {
        if (auto m = decode (data, size))
        {
            lastHeard = now;
            handle (*m, now);
        }
    }

    void BridgeSession::tick (double now)
    {
        if (linkState == Link::offline) return;

        if (linkState == Link::waiting || linkState == Link::lost)
        {
            if (now - lastHelloSent >= kHelloInterval)
            {
                Message m;
                m.cmd = Cmd::hello;
                m.token = sessionNonce;
                send (m);
                lastHelloSent = now;
            }
        }
        else if (linkState == Link::connected)
        {
            if (now - lastPingSent >= kPingInterval)
            {
                Message m;
                m.cmd = Cmd::ping;
                m.token = nextToken();
                send (m);
                lastPingSent = now;
            }
            if (now - lastHeard > kLostAfter)
            {
                linkState = Link::lost;
                addLog (now, "Lost contact with Cubase (no reply for " + std::to_string (static_cast<int> (kLostAfter)) + " s). Trying again...");
                if (lockState == Lock::locked)
                {
                    pause ("Lost contact with Cubase.", now);
                    resumeIfSameAfterReconnect = true;
                }
                move.active = false;
                changed();
            }
        }

        if (move.active && now - move.sentAt > kReplyTimeout)
        {
            move.active = false;
            moveResult = "No reply from Cubase: the move may not have happened.";
            addLog (now, moveResult);
            changed();
        }

        if (lastUserMoveLog > 0.0 && now - lastUserMoveLog > kUserMoveSettle)
        {
            lastUserMoveLog = -1.0e9;
            if (lockSlot >= 0 && lockSlot < slotCount())
            {
                const auto& s = slot (lockSlot);
                addLog (now, "Fader moved in Cubase: " + quoted (s.title) + " now shows " + s.volumeText + " dB");
            }
        }
    }

    std::vector<std::vector<std::uint8_t>> BridgeSession::takeOutgoing()
    {
        std::vector<std::vector<std::uint8_t>> out;
        out.swap (outgoing);
        return out;
    }

    void BridgeSession::send (const Message& m)
    {
        auto bytes = encode (m);
        if (! bytes.empty())
            outgoing.push_back (std::move (bytes));
    }

    std::uint16_t BridgeSession::nextToken()
    {
        tokenCounter = static_cast<std::uint16_t> ((tokenCounter % 0x3FFF) + 1);
        return tokenCounter;
    }

    BridgeSlot& BridgeSession::slotRef (int i)
    {
        if (i >= slotCount())
            slots.resize (static_cast<std::size_t> (std::min (i + 1, kMaxSlots)));
        return slots[static_cast<std::size_t> (i)];
    }

    // ---------------------------------------------------------------------------------------------
    // Incoming

    void BridgeSession::handle (const Message& m, double now)
    {
        const bool slotOk = m.slot >= 0 && m.slot < kMaxSlots;
        switch (m.cmd)
        {
            case Cmd::helloAck: connected (m.flag, m.slot, now); return;
            case Cmd::slotTitle: if (slotOk) onTitle (m, now); return;
            case Cmd::slotVolume: if (slotOk) onVolume (m, now); return;

            case Cmd::slotPan:
                if (! slotOk) return;
                slotRef (m.slot).hasPan = true;
                slotRef (m.slot).pan = m.value;
                slotRef (m.slot).panText = m.text;
                changed();
                return;

            case Cmd::slotMute:
                if (slotOk) { slotRef (m.slot).mute = m.flag != 0; changed(); }
                return;

            case Cmd::slotSolo:
                if (slotOk) { slotRef (m.slot).solo = m.flag != 0; changed(); }
                return;

            case Cmd::slotColour:
                if (slotOk) { slotRef (m.slot).hasColour = m.flag != 0; slotRef (m.slot).rgb = m.rgb; changed(); }
                return;

            case Cmd::slotAutomation:
            {
                if (! slotOk) return;
                auto& s = slotRef (m.slot);
                const bool wasWriting = s.automationWrite;
                s.automationRead = (m.flag & 1) != 0;
                s.automationWrite = (m.flag & 2) != 0;
                if (m.slot == lockSlot && s.automationWrite && ! wasWriting)
                    addLog (now, quoted (s.title) + " is writing automation (W is on): Mixology won't move it until W is off.");
                changed();
                return;
            }

            case Cmd::ack:
            {
                const auto status = static_cast<AckStatus> (m.flag);
                if (move.active && m.token == move.token)
                {
                    if (status == AckStatus::applied)
                    {
                        move.acked = true;
                    }
                    else
                    {
                        move.active = false;
                        moveResult = std::string ("Cubase ") + ackStatusText (status) + ". Nothing was changed.";
                        addLog (now, moveResult);
                    }
                }
                else if (status != AckStatus::applied)
                {
                    addLog (now, std::string ("Cubase ") + ackStatusText (status) + ".");
                }
                if (status == AckStatus::nameMismatch && lockState == Lock::locked)
                    pause ("Cubase reported that " + slotName (lockSlot) + " holds a different channel now.", now);
                changed();
                return;
            }

            case Cmd::log:
                addLog (now, "Cubase: " + m.text);
                return;

            case Cmd::pong:
                return;

            default:
                return;   // our own outgoing commands echoed back: ignore
        }
    }

    void BridgeSession::connected (int scriptVer, int count, double now)
    {
        const bool wasConnected = linkState == Link::connected;
        remoteVersion = scriptVer;
        linkState = Link::connected;

        slots.assign (static_cast<std::size_t> (std::clamp (count, 1, kMaxSlots)), BridgeSlot {});
        addLog (now, std::string (wasConnected ? "Cubase script restarted" : "Connected to Cubase")
                         + " (Mixology Bridge script v" + std::to_string (scriptVer) + ", "
                         + std::to_string (slotCount()) + " channels per bank).");

        if (lockState == Lock::locked)
        {
            pause ("Reconnected: checking that the locked channel is still there...", now);
            resumeIfSameAfterReconnect = true;
        }
        move.active = false;
        requestDump (now);
        changed();
    }

    void BridgeSession::onTitle (const Message& m, double now)
    {
        auto& s = slotRef (m.slot);
        const std::string oldTitle = s.title;
        s.present = ! m.text.empty();
        s.title = m.text;
        s.hash = m.hash;
        s.channelId = m.channelId;
        if (! s.present)
        {
            s.hasVolume = s.hasPan = s.hasColour = false;
            s.mute = s.solo = s.automationRead = s.automationWrite = false;
        }
        changed();

        if (m.slot != lockSlot || lockState == Lock::none) return;

        const bool same = isLockedChannel (s);
        if (lockState == Lock::locked && ! same)
        {
            const bool renamed = s.present && lockId != 0 && s.channelId == lockId;
            pause (slotName (m.slot) + (renamed ? " was renamed from " : " changed from ") + quoted (lockTitle) + " to "
                       + (s.present ? quoted (s.title) : std::string ("(empty)"))
                       + (renamed ? " (same Cubase channel ID)." : " (bank moved, tracks reordered or renamed).")
                       + " Nothing will be written until you lock a channel again.",
                   now);
            resumeIfSameAfterReconnect = false;
        }
        else if (lockState == Lock::paused && resumeIfSameAfterReconnect && linkState == Link::connected)
        {
            resumeIfSameAfterReconnect = false;
            if (same)
            {
                lockState = Lock::locked;
                pausedBecause.clear();
                addLog (now, quoted (lockTitle) + " is still in " + slotName (m.slot) + ": writing allowed again.");
            }
            else
            {
                pausedBecause = "After reconnecting, " + slotName (m.slot) + " holds "
                                + (s.present ? quoted (s.title) : std::string ("nothing")) + " instead of "
                                + quoted (lockTitle) + ". Lock a channel again to continue.";
                addLog (now, pausedBecause);
            }
        }
        (void) oldTitle;
    }

    void BridgeSession::onVolume (const Message& m, double now)
    {
        auto& s = slotRef (m.slot);
        s.hasVolume = true;
        s.volume = m.value;
        s.volumeText = m.text;
        if (auto db = FaderLaw::parseDb (m.text))
        {
            s.volumeDb = *db;
            law.learn (m.value, *db);
        }
        else
        {
            s.volumeDb = law.toDb (m.value);
        }
        changed();

        if (m.slot != lockSlot) return;

        // Our move's readback is the first report near the position we sent. Anything else while a move is
        // running (a stale report, someone grabbing the fader) is not taken as the result.
        if (move.active && now - move.sentAt <= kReplyTimeout && std::abs (m.value - move.sentValue) <= 0.01)
        {
            if (move.byValue)
            {
                move.active = false;
                moveResult = "Set " + quoted (s.title) + " to " + s.volumeText + " dB";
                return;
            }

            const double got = s.volumeDb;
            const bool wantInf = move.targetDb <= FaderLaw::kMinusInf + 1.0;
            const bool landed = wantInf ? got <= FaderLaw::kMinusInf + 1.0
                                        : std::abs (got - move.targetDb) <= kDbExact;
            if (landed)
            {
                move.active = false;
                moveResult = "Set " + quoted (s.title) + " to " + formatDb (got) + " (Cubase shows " + s.volumeText + ")"
                             + (move.step > 1 ? ", " + std::to_string (move.step) + " steps" : std::string());
                addLog (now, moveResult);
                return;
            }

            const double next = law.toNormalised (move.targetDb);
            const bool noProgress = std::abs (next - move.sentValue) * kValueMax < 1.0;
            if (move.step >= kMaxRefineSteps || noProgress)
            {
                move.active = false;
                if (! wantInf && std::abs (got - move.targetDb) <= kDbTolerance)
                    moveResult = "Set " + quoted (s.title) + " to " + formatDb (got) + " (Cubase shows " + s.volumeText + ")";
                else
                    moveResult = "Closest Cubase allows: " + formatDb (got) + " (asked for " + formatDb (move.targetDb) + ")";
                addLog (now, moveResult);
                return;
            }
            ++move.step;
            sendVolume (next, now);
            return;
        }

        // Not our move: the user (or automation playback) moved the fader in Cubase.
        lastUserMoveLog = now;
    }

    // ---------------------------------------------------------------------------------------------
    // Commands

    void BridgeSession::pause (const std::string& why, double now)
    {
        lockState = Lock::paused;
        pausedBecause = why;
        move.active = false;
        addLog (now, "PAUSED: " + why);
        changed();
    }

    bool BridgeSession::isLockedChannel (const BridgeSlot& s) const
    {
        // Same name AND (when Cubase reports one) the same channel ID.
        return s.present && s.hash == lockHash && s.title == lockTitle && s.channelId == lockId;
    }

    Message BridgeSession::writeMessage (Cmd cmd)
    {
        Message m;
        m.cmd = cmd;
        m.slot = lockSlot;
        m.token = nextToken();
        m.hash = lockHash;
        m.channelId = lockId;
        return m;
    }

    BridgeSession::Result BridgeSession::checkWritable() const
    {
        if (linkState != Link::connected) return { false, "Not connected to Cubase." };
        if (lockState == Lock::none) return { false, "Lock a channel first." };
        if (lockState == Lock::paused) return { false, "Paused: " + pausedBecause };
        if (lockSlot < 0 || lockSlot >= slotCount()) return { false, "The locked slot is gone." };
        const auto& s = slots[static_cast<std::size_t> (lockSlot)];
        if (! isLockedChannel (s))
            return { false, "The locked channel isn't in its slot any more." };
        if (s.automationWrite) return { false, quoted (s.title) + " is writing automation (W is on). Switch W off first." };
        if (! s.hasVolume) return { false, "Waiting for Cubase to report the fader." };
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::lockChannel (int slotIndex, double now)
    {
        if (linkState != Link::connected) return { false, "Not connected to Cubase." };
        if (slotIndex < 0 || slotIndex >= slotCount()) return { false, "No such slot." };
        const auto& s = slots[static_cast<std::size_t> (slotIndex)];
        if (! s.present) return { false, "That slot is empty." };

        if (original.valid && original.hash != s.hash)
        {
            addLog (now, "Restore point for " + quoted (original.title) + " dropped (a different channel is now locked).");
            original = {};
        }
        lockState = Lock::locked;
        lockSlot = slotIndex;
        lockTitle = s.title;
        lockHash = s.hash;
        lockId = s.channelId;
        pausedBecause.clear();
        resumeIfSameAfterReconnect = false;
        move = {};
        addLog (now, "Locked " + quoted (s.title) + " (" + slotName (slotIndex) + (s.channelId != 0 ? ", Cubase ID " + std::to_string (s.channelId) : std::string (", no Cubase ID"))
                         + "). Only this channel can be moved.");
        changed();
        return { true, {} };
    }

    void BridgeSession::unlock (double now)
    {
        if (lockState != Lock::none)
            addLog (now, "Unlocked " + quoted (lockTitle) + ".");
        lockState = Lock::none;
        lockSlot = -1;
        lockTitle.clear();
        lockHash = lockId = 0;
        pausedBecause.clear();
        move = {};
        changed();
    }

    void BridgeSession::takeSnapshotIfNeeded (double now)
    {
        if (original.valid && original.hash == lockHash) return;
        const auto& s = slots[static_cast<std::size_t> (lockSlot)];
        original.valid = true;
        original.slot = lockSlot;
        original.title = s.title;
        original.hash = s.hash;
        original.volume = s.volume;
        original.volumeText = s.volumeText;
        original.hasPan = s.hasPan;
        original.pan = s.pan;
        original.mute = s.mute;
        original.solo = s.solo;
        addLog (now, "Restore point saved for " + quoted (s.title) + ": " + s.volumeText + " dB"
                         + (s.hasPan ? ", pan " + s.panText : std::string())
                         + (s.mute ? ", muted" : "") + (s.solo ? ", soloed" : "") + ".");
    }

    void BridgeSession::sendVolume (double value, double now)
    {
        auto m = writeMessage (Cmd::setVolume);
        m.value = std::clamp (value, 0.0, 1.0);
        send (m);

        move.active = true;
        move.sentValue = m.value;
        move.sentAt = now;
        move.token = m.token;
        move.acked = false;
        changed();
    }

    BridgeSession::Result BridgeSession::setVolumeDb (double db, double now)
    {
        auto ok = checkWritable();
        if (! ok.ok) return ok;
        if (move.active) return { false, "A move is still in progress." };
        if (! std::isfinite (db) || db < -144.0) db = FaderLaw::kMinusInf;
        db = std::min (db, kMaxDb);

        takeSnapshotIfNeeded (now);
        move = {};
        move.targetDb = db;
        move.step = 1;
        moveResult.clear();
        addLog (now, "Moving " + quoted (lockTitle) + " to " + formatDb (db) + "...");
        sendVolume (law.toNormalised (db), now);
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::nudgeDb (double deltaDb, double now)
    {
        auto ok = checkWritable();
        if (! ok.ok) return ok;
        const auto& s = slots[static_cast<std::size_t> (lockSlot)];
        if (s.volumeDb <= FaderLaw::kMinusInf + 1.0) return { false, "The fader is at -inf: set a value instead." };
        return setVolumeDb (std::round ((s.volumeDb + deltaDb) * 100.0) / 100.0, now);
    }

    BridgeSession::Result BridgeSession::setVolumeNormalised (double position, double now)
    {
        auto ok = checkWritable();
        if (! ok.ok) return ok;
        takeSnapshotIfNeeded (now);
        move = {};
        move.byValue = true;
        move.targetDb = law.toDb (position);
        move.step = kMaxRefineSteps;
        sendVolume (position, now);
        return { true, {} };
    }

    void BridgeSession::noteDragFinished (double now)
    {
        if (lockSlot < 0 || lockSlot >= slotCount() || lockState != Lock::locked) return;
        const auto& s = slots[static_cast<std::size_t> (lockSlot)];
        addLog (now, "Fader dragged in Mixology: " + quoted (s.title) + " now shows " + s.volumeText + " dB");
    }

    BridgeSession::Result BridgeSession::setPan (double normalised, double now)
    {
        auto ok = checkWritable();
        if (! ok.ok) return ok;
        takeSnapshotIfNeeded (now);
        auto m = writeMessage (Cmd::setPan);
        m.value = std::clamp (normalised, 0.0, 1.0);
        send (m);
        changed();
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::setMute (bool on, double now)
    {
        auto ok = checkWritable();
        if (! ok.ok) return ok;
        takeSnapshotIfNeeded (now);
        auto m = writeMessage (Cmd::setMute);
        m.flag = on ? 1 : 0;
        send (m);
        addLog (now, std::string (on ? "Muting " : "Unmuting ") + quoted (lockTitle) + ".");
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::setSolo (bool on, double now)
    {
        auto ok = checkWritable();
        if (! ok.ok) return ok;
        takeSnapshotIfNeeded (now);
        auto m = writeMessage (Cmd::setSolo);
        m.flag = on ? 1 : 0;
        send (m);
        addLog (now, std::string (on ? "Soloing " : "Unsoloing ") + quoted (lockTitle) + ".");
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::restore (double now)
    {
        if (! original.valid) return { false, "Nothing to restore yet (no moves made)." };
        auto ok = checkWritable();
        if (! ok.ok) return ok;
        if (original.hash != lockHash) return { false, "The restore point belongs to " + quoted (original.title) + "." };
        if (move.active) return { false, "A move is still in progress." };

        const auto& s = slots[static_cast<std::size_t> (lockSlot)];
        addLog (now, "Restoring " + quoted (original.title) + " to " + original.volumeText + " dB"
                         + (original.hasPan ? ", pan " + s.panText + " -> original" : std::string()) + "...");

        if (original.hasPan && std::abs (s.pan - original.pan) > 1.0e-6)
        {
            auto m = writeMessage (Cmd::setPan);
            m.value = original.pan;
            send (m);
        }
        for (auto [cmd, want, have] : { std::tuple { Cmd::setMute, original.mute, s.mute },
                                        std::tuple { Cmd::setSolo, original.solo, s.solo } })
        {
            if (want == have) continue;
            auto m = writeMessage (cmd);
            m.flag = want ? 1 : 0;
            send (m);
        }

        move = {};
        const auto db = FaderLaw::parseDb (original.volumeText);
        move.targetDb = db ? *db : law.toDb (original.volume);
        move.step = kMaxRefineSteps;   // exact position: no refinement needed
        moveResult.clear();
        sendVolume (original.volume, now);
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::bank (BankAction action, double now)
    {
        if (linkState != Link::connected) return { false, "Not connected to Cubase." };
        Message m;
        m.cmd = Cmd::bank;
        m.flag = static_cast<int> (action);
        send (m);
        static const char* names[] = { "reset", "previous bank", "next bank", "shift left", "shift right" };
        addLog (now, std::string ("Bank: ") + names[static_cast<int> (action) % 5] + ".");
        return { true, {} };
    }

    void BridgeSession::requestDump (double)
    {
        Message m;
        m.cmd = Cmd::requestDump;
        send (m);
    }

    void BridgeSession::addLog (double now, std::string text)
    {
        logBuffer.push_back ({ now, std::move (text) });
        while (logBuffer.size() > kMaxLogLines)
            logBuffer.pop_front();
        ++logCount;
        changed();
    }
}
