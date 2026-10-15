#include "mixology/BridgeSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mixology
{
    using namespace bridge;

    namespace
    {
        constexpr double kMaxDb = 6.0;   // top of a Cubase channel fader

        std::string quoted (const std::string& s) { return "'" + s + "'"; }
        std::string slotName (int slot) { return "slot " + std::to_string (slot + 1); }
        bool isMinusInf (double db) { return db <= FaderLaw::kMinusInf + 1.0; }
    }

    std::string formatDb (double db)
    {
        if (isMinusInf (db)) return "-inf dB";
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
            cancelMoves ({}, now);
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

    void BridgeSession::watchPan (int slot, double to, double now)
    {
        if (slot < 0 || slot >= slotCount()) return;
        const auto& s = slots[static_cast<std::size_t> (slot)];
        if (s.present) panWrites[s.hash] = { to, now };
        if (! s.present || ! s.hasPan || std::abs (s.pan - to) < 0.01) return;
        panWatches.erase (std::remove_if (panWatches.begin(), panWatches.end(), [slot] (const PanWatch& w) { return w.slot == slot; }), panWatches.end());
        panWatches.push_back ({ slot, s.hash, s.pan, to, now });
    }

    void BridgeSession::checkPanWatches (double now)
    {
        for (auto it = panWatches.begin(); it != panWatches.end();)
        {
            const auto& w = *it;
            const auto& s = slots[static_cast<std::size_t> (w.slot)];
            if (! s.present || s.hash != w.hash || std::abs (s.pan - w.to) < 0.01) { it = panWatches.erase (it); continue; }   // gone, or it landed
            if (now - w.sentAt < kPanLandSeconds) { ++it; continue; }
            if (std::abs (s.pan - w.from) < 0.003) markNoPan (s, "Cubase didn't follow the pan move (it stays at " + s.panText + ")", now);
            it = panWatches.erase (it);
        }
    }

    void BridgeSession::markNoPan (const BridgeSlot& s, const std::string& why, double now)
    {
        if (! noPan.insert (s.hash).second) return;
        ++noPanChanges;
        panWrites.erase (s.hash);
        addLog (now, quoted (s.title) + ": " + why + ". Its output is probably mono (e.g. a mono group), so it plays in the middle. "
                         "Mixology shows \"No pan\" and leaves its pan alone.");
        changed();
    }

    void BridgeSession::notePanReport (int slotIndex, double now)
    {
        const auto& s = slots[static_cast<std::size_t> (slotIndex)];
        if (! s.present || s.hash == 0) return;
        if (noPan.count (s.hash) != 0)
        {
            // Mixology never moves these, so a pan away from hard left came from Cubase: it pans after all.
            if (s.pan > kHardLeft)
            {
                noPan.erase (s.hash);
                ++noPanChanges;
                addLog (now, quoted (s.title) + ": Cubase now reports a real pan (" + s.panText + "), so Mixology pans it again.");
            }
            return;
        }
        auto it = panWrites.find (s.hash);
        if (it == panWrites.end()) return;
        const double since = now - it->second.at;
        if (s.pan <= kHardLeft && it->second.value > 0.005 && since >= kPanLandSeconds)
            markNoPan (s, "Cubase took Mixology's pan move, then reported it back at hard left (" + s.panText + ")", now);
        else if (std::abs (s.pan - it->second.value) > 0.01 && since >= kPanLandSeconds)
            panWrites.erase (it);   // moved somewhere else in Cubase: a real panner
    }

    void BridgeSession::setNoPanList (const std::vector<std::uint32_t>& list)
    {
        std::set<std::uint32_t> next (list.begin(), list.end());
        next.erase (0u);
        if (next == noPan) return;
        noPan = std::move (next);
        ++noPanChanges;
        changed();
    }

    void BridgeSession::tick (double now)
    {
        if (linkState == Link::offline) return;
        if (! panWatches.empty()) checkPanWatches (now);

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
                cancelMoves ({}, now);
                changed();
            }
        }

        if (scan.active && now >= scan.nextAt) scanStep (now);

        for (int i = 0; i < static_cast<int> (meta.size()); ++i)
        {
            auto& mt = meta[static_cast<std::size_t> (i)];
            auto& mv = mt.move;
            if (mv.active && now - mv.sentAt > kReplyTimeout)
            {
                mv.active = false;
                const auto& s = slot (i);
                if (mv.kind == MoveKind::drag)
                {
                    // Drag readbacks overlap; the last one may simply not match. Report where it ended up.
                    if (mv.logResult) addLog (now, "Fader dragged in Mixology: " + quoted (s.title) + " now shows " + s.volumeText + " dB");
                    mt.loggedText = s.volumeText;
                }
                else
                {
                    moveResult = "No reply from Cubase for " + quoted (s.title) + ": the move may not have happened.";
                    addLog (now, moveResult);
                }
                changed();
            }

            if (mt.learnAt > 0.0 && now - mt.learnAt >= kLearnSettle)
            {
                mt.learnAt = -1.0;
                law.learn (mt.learnValue, mt.learnDb);
            }

            if (mt.cubaseMovedAt > 0.0 && now - mt.cubaseMovedAt > kUserMoveSettle)
            {
                mt.cubaseMovedAt = -1.0;
                const auto& s = slot (i);
                if (s.present && s.volumeText != mt.loggedText && ! s.automationRead)
                    addLog (now, "Fader moved in Cubase: " + quoted (s.title) + " now shows " + s.volumeText + " dB");
                mt.loggedText = s.volumeText;
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
        {
            const auto n = static_cast<std::size_t> (std::min (i + 1, kMaxSlots));
            slots.resize (n);
            meta.resize (n);
        }
        return slots[static_cast<std::size_t> (i)];
    }

    BridgeSession::SlotMeta& BridgeSession::metaRef (int i)
    {
        slotRef (i);
        return meta[static_cast<std::size_t> (i)];
    }

    ChannelRef BridgeSession::refOf (int i) const
    {
        if (i < 0 || i >= slotCount() || ! slot (i).present) return {};
        return { i, slot (i).hash };
    }

    bool BridgeSession::moveInProgress (int slotIndex) const
    {
        for (int i = 0; i < static_cast<int> (meta.size()); ++i)
            if ((slotIndex < 0 || i == slotIndex) && meta[static_cast<std::size_t> (i)].move.active) return true;
        return false;
    }

    bool BridgeSession::nameIsDuplicated (int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= slotCount() || ! slot (slotIndex).present) return false;
        const auto h = slot (slotIndex).hash;
        for (int i = 0; i < slotCount(); ++i)
            if (i != slotIndex && slot (i).present && slot (i).hash == h) return true;
        return false;
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
            {
                if (! slotOk) return;
                auto& s = slotRef (m.slot);
                s.hasPan = true;
                s.pan = m.value;
                s.panText = m.text;
                if (s.present)
                    if (auto it = faders.find (s.hash); it != faders.end())
                    {
                        it->second.hasPan = true;
                        it->second.pan = s.pan;
                        it->second.panText = s.panText;
                    }
                notePanReport (m.slot, now);
                changed();
                return;
            }

            case Cmd::slotMute:
                if (slotOk)
                {
                    auto& s = slotRef (m.slot);
                    s.mute = m.flag != 0;
                    if (s.present) if (auto it = faders.find (s.hash); it != faders.end()) it->second.mute = s.mute;
                    changed();
                }
                return;

            case Cmd::slotSolo:
                if (slotOk)
                {
                    auto& s = slotRef (m.slot);
                    s.solo = m.flag != 0;
                    if (s.present) if (auto it = faders.find (s.hash); it != faders.end()) it->second.solo = s.solo;
                    changed();
                }
                return;

            case Cmd::slotColour:
                if (slotOk) { slotRef (m.slot).hasColour = m.flag != 0; slotRef (m.slot).rgb = m.rgb; changed(); }
                return;

            case Cmd::slotAutomation:
            {
                if (! slotOk) return;
                auto& s = slotRef (m.slot);
                s.automationRead = (m.flag & 1) != 0;
                s.automationWrite = (m.flag & 2) != 0;
                auto& mv = metaRef (m.slot).move;
                if (s.automationWrite && mv.active)
                {
                    mv.active = false;
                    addLog (now, quoted (s.title) + " started writing automation (W is on): Mixology stopped moving it.");
                }
                changed();
                return;
            }

            case Cmd::ack:
            {
                const auto status = static_cast<AckStatus> (m.flag);
                if (status == AckStatus::applied) return;

                std::string what;
                for (int i = 0; i < static_cast<int> (meta.size()); ++i)
                {
                    auto& mv = meta[static_cast<std::size_t> (i)].move;
                    if (mv.active && mv.token == m.token)
                    {
                        mv.active = false;
                        what = quoted (slot (i).title) + ": ";
                    }
                }
                moveResult = what + "Cubase " + ackStatusText (status) + ". Nothing was changed.";
                addLog (now, moveResult);
                if (status == AckStatus::nameMismatch || status == AckStatus::emptySlot)
                    requestDump (now);   // our picture of the bank is out of date
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

        const auto n = static_cast<std::size_t> (std::clamp (count, 1, kMaxSlots));
        slots.assign (n, BridgeSlot {});
        meta.assign (n, SlotMeta {});
        addLog (now, std::string (wasConnected ? "Cubase script restarted" : "Connected to Cubase")
                         + " (Mixology Bridge script v" + std::to_string (scriptVer) + ", "
                         + std::to_string (slotCount()) + " channels per bank).");
        if (scriptVer < bridge::kLatestScriptVersion)
            addLog (now, "A newer Cubase script (v" + std::to_string (bridge::kLatestScriptVersion) + ") came with this Mixology: copy the "
                         "'Mixology' folder from the download's 'Cubase MIDI Remote' folder again (manual, Cubase bridge step 2).");
        requestDump (now);
        changed();
    }

    void BridgeSession::onTitle (const Message& m, double now)
    {
        auto& s = slotRef (m.slot);
        auto& mt = metaRef (m.slot);
        const bool sameChannel = s.present && ! m.text.empty() && s.hash == m.hash && s.title == m.text;

        if (! sameChannel)
        {
            if (mt.move.active)
                addLog (now, "Stopped moving " + quoted (s.title) + ": " + slotName (m.slot) + " now shows "
                                 + (m.text.empty() ? std::string ("nothing") : quoted (m.text)) + ".");
            mt = SlotMeta {};
            s = BridgeSlot {};
        }
        s.present = ! m.text.empty();
        s.title = m.text;
        s.hash = m.hash;
        s.channelId = m.channelId;
        changed();
    }

    void BridgeSession::onVolume (const Message& m, double now)
    {
        auto& s = slotRef (m.slot);
        auto& mt = metaRef (m.slot);
        const bool first = ! s.hasVolume;
        s.hasVolume = true;
        s.volume = m.value;
        s.volumeText = m.text;
        if (auto db = FaderLaw::parseDb (m.text))
        {
            s.volumeDb = *db;
            mt.learnAt = now;   // learned in tick() if no newer report comes (the pair has settled)
            mt.learnValue = m.value;
            mt.learnDb = *db;
        }
        else
        {
            s.volumeDb = law.toDb (m.value);
        }
        if (s.present)
        {
            auto& f = faders[s.hash];
            f.title = s.title;
            f.db = s.volumeDb;
            f.seenAt = now;
            f.volume = s.volume;
            f.volumeText = s.volumeText;
            f.hasPan = s.hasPan;
            f.pan = s.pan;
            f.panText = s.panText;
            f.mute = s.mute;
            f.solo = s.solo;
        }
        changed();

        if (first)
        {
            mt.loggedText = s.volumeText;   // where the fader started: not a move
            return;
        }

        // Our move's readback is the first report near the position we sent. Anything else while a move is
        // running (a stale report, someone grabbing the fader) is not taken as the result.
        auto& mv = mt.move;
        if (mv.active && mv.hash == s.hash && now - mv.sentAt <= kReplyTimeout && std::abs (m.value - mv.sentValue) <= 0.01)
        {
            if (mv.kind != MoveKind::setDb)
            {
                mv.active = false;
                mt.loggedText = s.volumeText;
                if (mv.kind == MoveKind::restore)
                {
                    moveResult = "Restored " + quoted (s.title) + ": Cubase shows " + s.volumeText + " dB";
                    addLog (now, moveResult);
                }
                else if (mv.logResult)
                {
                    addLog (now, "Fader dragged in Mixology: " + quoted (s.title) + " now shows " + s.volumeText + " dB");
                }
                return;
            }

            const double got = s.volumeDb;
            const bool wantInf = isMinusInf (mv.targetDb);
            const bool landed = wantInf ? isMinusInf (got) : std::abs (got - mv.targetDb) <= kDbExact;
            if (landed)
            {
                mv.active = false;
                mt.loggedText = s.volumeText;
                moveResult = "Set " + quoted (s.title) + " to " + formatDb (got) + " (Cubase shows " + s.volumeText + ")"
                             + (mv.step > 1 ? ", " + std::to_string (mv.step) + " steps" : std::string());
                addLog (now, moveResult);
                return;
            }

            // Next try: through this readback and the previous one (secant), or on the learned curve shifted by
            // how far it was off here. Readbacks refine this move without being learned until they settle.
            double next = law.toNormalised (mv.targetDb);
            if (! wantInf && ! isMinusInf (got))
            {
                if (mv.hasPrev && std::abs (got - mv.prevDb) > 1.0e-6 && std::abs (mv.sentValue - mv.prevValue) > 1.0e-9)
                    next = mv.sentValue + (mv.targetDb - got) * (mv.sentValue - mv.prevValue) / (got - mv.prevDb);
                else
                    next = law.toNormalised (mv.targetDb - (got - law.toDb (mv.sentValue)));
                next = std::clamp (next, std::max (0.0, mv.sentValue - 0.25), std::min (1.0, mv.sentValue + 0.25));
            }
            mv.hasPrev = ! isMinusInf (got);
            mv.prevValue = mv.sentValue;
            mv.prevDb = got;
            const bool noProgress = std::abs (next - mv.sentValue) * kValueMax < 1.0;
            if (mv.step >= kMaxRefineSteps || noProgress)
            {
                mv.active = false;
                mt.loggedText = s.volumeText;
                if (! wantInf && std::abs (got - mv.targetDb) <= kDbTolerance)
                    moveResult = "Set " + quoted (s.title) + " to " + formatDb (got) + " (Cubase shows " + s.volumeText + ")";
                else
                    moveResult = "Closest Cubase allows for " + quoted (s.title) + ": " + formatDb (got) + " (asked for " + formatDb (mv.targetDb) + ")";
                addLog (now, moveResult);
                return;
            }
            ++mv.step;
            sendVolume (m.slot, next, now);
            return;
        }

        // Reports right after our own writes are their echo (drag readbacks overlap). Anything later is a move
        // made in Cubase (mouse, controller, automation): logged once it settles, if the value really changed.
        if (now - mt.lastOwnWrite > kEchoWindow && ! mv.active)
            mt.cubaseMovedAt = now;
    }

    // ---------------------------------------------------------------------------------------------
    // Writing

    void BridgeSession::cancelMoves (const std::string& why, double now)
    {
        if (scan.active && scan.plan)
        {
            plan.notes.push_back ("Stopped: " + (why.empty() ? std::string ("contact with Cubase was lost") : why));
            finishPlan (now);
        }
        scan.active = false;
        bool any = false;
        for (auto& mt : meta)
        {
            any = any || mt.move.active;
            mt.move.active = false;
        }
        if (any && ! why.empty()) addLog (now, why);
    }

    BridgeSession::Result BridgeSession::checkWritable (ChannelRef ref, Field field) const
    {
        if (linkState != Link::connected) return { false, "Not connected to Cubase." };
        if (scan.active && scan.plan) return { false, "Mixology is applying " + plan.what + " (a few seconds)." };
        if (scan.active) return { false, "Mixology is reading your faders for Analyze Mix (a second or two)." };
        if (ref.slot < 0 || ref.slot >= slotCount()) return { false, "No such channel." };
        const auto& s = slots[static_cast<std::size_t> (ref.slot)];
        if (! s.present) return { false, "That strip is empty." };
        if (s.hash != ref.hash)
            return { false, "That strip now shows " + quoted (s.title) + " (the bank or the track order changed). Nothing was moved." };
        if (isProtected (s.hash)) return { false, quoted (s.title) + " is protected (padlock). Click the padlock to let Mixology move it." };
        if (s.automationWrite) return { false, quoted (s.title) + " is writing automation (W is on). Switch W off first." };
        if (field == Field::volume && ! s.hasVolume) return { false, "Waiting for Cubase to report " + quoted (s.title) + "'s fader." };
        if (field == Field::pan && ! s.hasPan) return { false, quoted (s.title) + " has no pan control." };
        if (field == Field::pan && panIgnored (s.hash))
            return { false, quoted (s.title) + " doesn't pan in Cubase: its output is mono (a mono group or output), so it plays in the middle. Mixology leaves its pan alone." };
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::warningsFor (int slotIndex, Result ok, double now)
    {
        const auto& s = slot (slotIndex);
        if (ok.ok && s.automationRead && ! readWarned[s.hash])
        {
            readWarned[s.hash] = true;
            ok.warning = quoted (s.title) + " has automation Read (R) on: if it has volume automation, playback will move the fader back.";
            addLog (now, ok.warning);
        }
        return ok;
    }

    Message BridgeSession::writeMessage (Cmd cmd, int slotIndex)
    {
        const auto& s = slot (slotIndex);
        Message m;
        m.cmd = cmd;
        m.slot = slotIndex;
        m.token = nextToken();
        m.hash = s.hash;
        m.channelId = s.channelId;
        return m;
    }

    void BridgeSession::takeRestorePointIfNeeded (int slotIndex, double now)
    {
        const auto& s = slot (slotIndex);
        if (restore.count (s.hash)) return;
        RestorePoint p;
        p.title = s.title;
        p.hash = s.hash;
        p.ambiguous = nameIsDuplicated (slotIndex);
        p.volume = s.volume;
        p.volumeText = s.volumeText;
        p.hasPan = s.hasPan;
        p.pan = s.pan;
        p.panText = s.panText;
        p.mute = s.mute;
        p.solo = s.solo;
        restore[s.hash] = p;
        addLog (now, "Restore point saved for " + quoted (s.title) + ": " + s.volumeText + " dB"
                         + (s.hasPan ? ", pan " + s.panText : std::string())
                         + (s.mute ? ", muted" : "") + (s.solo ? ", soloed" : "") + "."
                         + (p.ambiguous ? " (Another channel has the same name: Restore can't tell them apart.)" : ""));
    }

    void BridgeSession::sendVolume (int slotIndex, double value, double now)
    {
        auto m = writeMessage (Cmd::setVolume, slotIndex);
        m.value = std::clamp (value, 0.0, 1.0);
        send (m);

        auto& mt = metaRef (slotIndex);
        mt.lastOwnWrite = now;
        mt.cubaseMovedAt = -1.0;
        mt.move.active = true;
        mt.move.hash = slot (slotIndex).hash;
        mt.move.sentValue = m.value;
        mt.move.sentAt = now;
        mt.move.token = m.token;
        changed();
    }

    void BridgeSession::startMove (int slotIndex, double db, double now)
    {
        auto& mv = metaRef (slotIndex).move;
        mv = {};
        mv.kind = MoveKind::setDb;
        mv.targetDb = db;
        mv.step = 1;
        moveResult.clear();
        sendVolume (slotIndex, law.toNormalised (db), now);
    }

    BridgeSession::Result BridgeSession::setVolumeDb (ChannelRef ref, double db, double now)
    {
        auto ok = checkWritable (ref, Field::volume);
        if (! ok.ok) return ok;
        if (! std::isfinite (db) || db < -144.0) db = FaderLaw::kMinusInf;
        db = std::min (db, kMaxDb);

        takeRestorePointIfNeeded (ref.slot, now);
        startMove (ref.slot, db, now);   // replaces a move in progress on this channel (latest wins)
        return warningsFor (ref.slot, ok, now);
    }

    BridgeSession::Result BridgeSession::nudgeDb (ChannelRef ref, double deltaDb, double now)
    {
        auto ok = checkWritable (ref, Field::volume);
        if (! ok.ok) return ok;
        const auto& s = slot (ref.slot);
        const auto& mv = meta[static_cast<std::size_t> (ref.slot)].move;
        // Several nudges in a row (mouse wheel) add up even while Cubase hasn't answered yet.
        const double from = mv.active && mv.kind == MoveKind::setDb ? mv.targetDb : s.volumeDb;
        if (isMinusInf (from)) return { false, quoted (s.title) + " is at -inf: type a value instead." };
        return setVolumeDb (ref, std::round ((from + deltaDb) * 100.0) / 100.0, now);
    }

    std::optional<double> BridgeSession::movingToDb (int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= slotCount()) return std::nullopt;
        const auto& mv = meta[static_cast<std::size_t> (slotIndex)].move;
        return mv.active && mv.kind == MoveKind::setDb ? std::optional<double> (mv.targetDb) : std::nullopt;
    }

    BridgeSession::Result BridgeSession::setVolumeNormalised (ChannelRef ref, double position, double now)
    {
        auto ok = checkWritable (ref, Field::volume);
        if (! ok.ok) return ok;
        takeRestorePointIfNeeded (ref.slot, now);
        auto& mv = metaRef (ref.slot).move;
        mv = {};
        mv.kind = MoveKind::drag;
        mv.logResult = false;   // logged once, when the drag ends
        mv.targetDb = law.toDb (position);
        sendVolume (ref.slot, position, now);
        return warningsFor (ref.slot, ok, now);
    }

    void BridgeSession::noteDragFinished (ChannelRef ref, double now)
    {
        if (ref.slot < 0 || ref.slot >= slotCount()) return;
        const auto& s = slot (ref.slot);
        if (! s.present || s.hash != ref.hash) return;
        auto& mt = meta[static_cast<std::size_t> (ref.slot)];
        if (mt.move.active && mt.move.kind == MoveKind::drag)
        {
            mt.move.logResult = true;   // logged when Cubase confirms the last position
            return;
        }
        addLog (now, "Fader dragged in Mixology: " + quoted (s.title) + " now shows " + s.volumeText + " dB");
        mt.loggedText = s.volumeText;
    }

    BridgeSession::Result BridgeSession::setPan (ChannelRef ref, double normalised, double now)
    {
        auto ok = checkWritable (ref, Field::pan);
        if (! ok.ok) return ok;
        takeRestorePointIfNeeded (ref.slot, now);
        auto m = writeMessage (Cmd::setPan, ref.slot);
        m.value = std::clamp (normalised, 0.0, 1.0);
        send (m);
        watchPan (ref.slot, m.value, now);
        changed();
        return warningsFor (ref.slot, ok, now);
    }

    BridgeSession::Result BridgeSession::setMute (ChannelRef ref, bool on, double now)
    {
        auto ok = checkWritable (ref, Field::mute);
        if (! ok.ok) return ok;
        takeRestorePointIfNeeded (ref.slot, now);
        auto m = writeMessage (Cmd::setMute, ref.slot);
        m.flag = on ? 1 : 0;
        send (m);
        addLog (now, std::string (on ? "Muting " : "Unmuting ") + quoted (slot (ref.slot).title) + ".");
        return { true, {} };
    }

    BridgeSession::Result BridgeSession::setSolo (ChannelRef ref, bool on, double now)
    {
        auto ok = checkWritable (ref, Field::solo);
        if (! ok.ok) return ok;
        takeRestorePointIfNeeded (ref.slot, now);
        auto m = writeMessage (Cmd::setSolo, ref.slot);
        m.flag = on ? 1 : 0;
        send (m);
        addLog (now, std::string (on ? "Soloing " : "Unsoloing ") + quoted (slot (ref.slot).title) + ".");
        return { true, {} };
    }

    // ---------------------------------------------------------------------------------------------
    // Protect

    BridgeSession::Result BridgeSession::setProtected (ChannelRef ref, bool on, double now)
    {
        if (ref.slot < 0 || ref.slot >= slotCount()) return { false, "No such channel." };
        const auto& s = slot (ref.slot);
        if (! s.present) return { false, "That strip is empty." };
        if (s.hash != ref.hash) return { false, "That strip now shows " + quoted (s.title) + "." };
        if (on == isProtected (s.hash)) return { true, {} };

        if (on)
        {
            protectedChannels[s.hash] = s.title;
            auto& mv = metaRef (ref.slot).move;
            mv.active = false;
            addLog (now, "Protected " + quoted (s.title) + ": Mixology won't move it."
                             + (nameIsDuplicated (ref.slot) ? " (Every channel with this name is protected.)" : ""));
        }
        else
        {
            protectedChannels.erase (s.hash);
            addLog (now, quoted (s.title) + " is no longer protected.");
        }
        changed();
        return { true, {} };
    }

    std::vector<BridgeSession::Protected> BridgeSession::protectedList() const
    {
        std::vector<Protected> out;
        for (const auto& [hash, title] : protectedChannels)
            out.push_back ({ hash, title });
        return out;
    }

    void BridgeSession::setProtectedList (const std::vector<Protected>& list)
    {
        protectedChannels.clear();
        for (const auto& p : list)
            if (p.hash != 0) protectedChannels[p.hash] = p.title;
        changed();
    }

    // ---------------------------------------------------------------------------------------------
    // Restore

    bool BridgeSession::isChanged (const RestorePoint& p, int* slotOut) const
    {
        int found = -1, count = 0;
        for (int i = 0; i < slotCount(); ++i)
            if (slot (i).present && slot (i).hash == p.hash) { found = i; ++count; }
        if (slotOut != nullptr) *slotOut = count == 1 ? found : -1;
        if (count > 1) return true;   // not unique: assume it still differs
        if (count == 0)
        {
            // Not in view: as it was when last seen (mute and solo aren't known there).
            const auto it = faders.find (p.hash);
            if (it == faders.end()) return true;
            const auto& f = it->second;
            const bool volumeDiffers = std::abs (f.volume - p.volume) > 1.0e-4 && f.volumeText != p.volumeText;
            const bool panDiffers = p.hasPan && f.hasPan && std::abs (f.pan - p.pan) > 1.0e-4;
            return volumeDiffers || panDiffers;
        }

        const auto& s = slot (found);
        const bool volumeDiffers = std::abs (s.volume - p.volume) > 1.0e-4 && s.volumeText != p.volumeText;
        const bool panDiffers = p.hasPan && s.hasPan && std::abs (s.pan - p.pan) > 1.0e-4;
        return volumeDiffers || panDiffers || s.mute != p.mute || s.solo != p.solo;
    }

    int BridgeSession::changedCount() const
    {
        int n = 0;
        for (const auto& [hash, p] : restore)
            if (isChanged (p)) ++n;
        return n;
    }

    BridgeSession::Result BridgeSession::restoreChannel (ChannelRef ref, double now)
    {
        auto ok = checkWritable (ref, Field::volume);
        if (! ok.ok) return ok;
        const auto& s = slot (ref.slot);
        auto it = restore.find (s.hash);
        if (it == restore.end()) return { false, "Mixology hasn't changed " + quoted (s.title) + "." };
        const auto& p = it->second;
        if (p.ambiguous || nameIsDuplicated (ref.slot))
            return { false, "Two channels are called " + quoted (s.title) + ": rename one so Mixology can tell which to restore." };

        addLog (now, "Restoring " + quoted (p.title) + " to " + p.volumeText + " dB"
                         + (p.hasPan ? ", pan " + p.panText : std::string())
                         + (p.mute ? ", muted" : "") + (p.solo ? ", soloed" : "") + "...");

        if (p.hasPan && s.hasPan && std::abs (s.pan - p.pan) > 1.0e-6)
        {
            if (! panIgnored (s.hash))
            {
                auto m = writeMessage (Cmd::setPan, ref.slot);
                m.value = p.pan;
                send (m);
                watchPan (ref.slot, m.value, now);
            }
        }
        for (auto [cmd, want, have] : { std::tuple { Cmd::setMute, p.mute, s.mute },
                                        std::tuple { Cmd::setSolo, p.solo, s.solo } })
        {
            if (want == have) continue;
            auto m = writeMessage (cmd, ref.slot);
            m.flag = want ? 1 : 0;
            send (m);
        }

        if (std::abs (s.volume - p.volume) > 1.0e-7)
        {
            auto& mv = metaRef (ref.slot).move;
            mv = {};
            mv.kind = MoveKind::restore;   // the exact position Cubase reported before: no refinement needed
            mv.targetDb = law.toDb (p.volume);
            moveResult.clear();
            sendVolume (ref.slot, p.volume, now);
        }
        else
        {
            moveResult = "Restored " + quoted (p.title) + ".";
        }
        changed();
        return { true, {} };
    }

    BridgeSession::RestoreSummary BridgeSession::restoreAll (double now)
    {
        RestoreSummary sum;
        if (linkState != Link::connected) { sum.message = "Not connected to Cubase."; return sum; }
        if (scan.active) { sum.message = "Mixology is busy (" + (scan.plan ? plan.what : std::string ("reading faders")) + "): try again in a second."; return sum; }

        // Changed channels outside the bank: the whole restore goes through a plan (the bank pages through
        // the project and comes back).
        std::vector<PlanStep> steps;
        int outside = 0;
        for (const auto& [hash, p] : restore)
        {
            int at = -1;
            if (! isChanged (p, &at)) continue;
            bool inView = false;
            for (int i = 0; i < slotCount(); ++i) inView = inView || (slot (i).present && slot (i).hash == hash);
            if (! inView) ++outside;
            if (p.ambiguous) continue;
            PlanStep st;
            st.hash = hash;
            st.title = p.title;
            st.setVolume = true;
            st.exactVolume = true;
            st.position = p.volume;
            st.setPan = p.hasPan;
            st.pan = p.pan;
            st.setMute = true;
            st.mute = p.mute;
            st.setSolo = true;
            st.solo = p.solo;
            steps.push_back (st);
        }
        if (outside > 0)
        {
            sum.outsideBank = outside;
            const auto n = steps.size();
            const auto r = applyPlan ("Restore all", std::move (steps), now);
            sum.message = r.ok ? "Restoring " + std::to_string (n) + " channel" + (n == 1 ? "" : "s")
                                     + ": Mixology steps through the bank and comes back (a few seconds)."
                               : r.message;
            return sum;
        }

        for (const auto& [hash, p] : restore)
        {
            int at = -1;
            if (! isChanged (p, &at)) continue;
            if (at < 0)
            {
                bool inView = false;
                for (int i = 0; i < slotCount(); ++i) inView = inView || (slot (i).present && slot (i).hash == hash);
                if (inView) ++sum.skipped; else ++sum.outsideBank;
                continue;
            }
            if (restoreChannel (refOf (at), now).ok) ++sum.restored;
            else ++sum.skipped;
        }

        if (sum.restored + sum.outsideBank + sum.skipped == 0)
            sum.message = restore.empty() ? "Mixology hasn't changed anything yet." : "Everything Mixology changed is already back how it was.";
        else
        {
            sum.message = "Restored " + std::to_string (sum.restored) + " channel" + (sum.restored == 1 ? "" : "s") + ".";
            if (sum.outsideBank > 0)
                sum.message += " " + std::to_string (sum.outsideBank) + " more " + (sum.outsideBank == 1 ? "is" : "are")
                               + " outside the bank: use the bank arrows, then Restore again.";
            if (sum.skipped > 0)
                sum.message += " " + std::to_string (sum.skipped) + " skipped (protected, writing automation, or two channels with the same name).";
        }
        addLog (now, sum.message);
        changed();
        return sum;
    }

    bool BridgeSession::isAudible (std::uint32_t hash) const
    {
        const auto it = faders.find (hash);
        if (it == faders.end()) return true;   // never seen: assume it plays
        if (it->second.mute) return false;
        const bool anySolo = std::any_of (faders.begin(), faders.end(), [] (const auto& f) { return f.second.solo; });
        return ! anySolo || it->second.solo;
    }

    void BridgeSession::forgetRestorePoints (double now)
    {
        if (restore.empty()) return;
        restore.clear();
        addLog (now, "Restore points cleared: the mix as it is now is the new starting point.");
        changed();
    }

    // ---------------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------------
    // Reading every channel's fader: reset the bank, then page through it, then go back.

    void BridgeSession::sendBank (BankAction action)
    {
        Message m;
        m.cmd = Cmd::bank;
        m.flag = static_cast<int> (action);
        send (m);
    }

    BridgeSession::Result BridgeSession::scanAllChannels (double now)
    {
        if (linkState != Link::connected) return { false, "Not connected to Cubase." };
        if (scan.active && scan.plan) return { false, "Mixology is still applying " + plan.what + "." };
        if (scan.active) return { true, {} };
        cancelMoves ({}, now);
        scan = {};
        scan.active = true;
        scan.originalFirst = slotCount() > 0 && slot (0).present ? slot (0).hash : 0;
        scan.nextAt = now + kScanPageSeconds;
        sendBank (BankAction::reset);
        changed();
        return { true, {} };
    }

    void BridgeSession::scanStep (double now)
    {
        if (scan.restoring)
        {
            if (! scan.pending.empty())
            {
                sendBank (scan.pending.front());
                scan.pending.pop_front();
                scan.nextAt = now + kScanStepSeconds;
                return;
            }
            if (scan.jump && scan.jumpChecks < 2)
            {
                // Did it land? (Cubase may stop a page short at the end of the project.) Finish with single steps.
                if (! scan.jumpSettling)
                {
                    scan.jumpSettling = true;
                    scan.nextAt = now + kJumpSettleSeconds;
                    return;
                }
                scan.jumpSettling = false;
                ++scan.jumpChecks;
                const int at = viewOffset();
                if (at >= 0 && at != scan.jumpTarget)
                {
                    for (int k = 0; k < std::abs (scan.jumpTarget - at); ++k)
                        scan.pending.push_back (scan.jumpTarget > at ? BankAction::shiftRight : BankAction::shiftLeft);
                    scan.nextAt = now;
                    return;
                }
            }
            scan.active = false;
            if (scan.plan) finishPlan (now);
            else if (! scan.jump) addLog (now, "Read " + std::to_string (faders.size()) + " channel fader" + (faders.size() == 1 ? "" : "s") + " for Analyze Mix.");
            changed();
            return;
        }

        if (scan.waiting)
        {
            // A plan's moves on this page land before the bank moves on (a bank change would cut them off).
            if (moveInProgress (-1) && now < scan.waitUntil)
            {
                scan.nextAt = now + kPlanPollSeconds;
                return;
            }
            scan.waiting = false;
        }
        else
        {
            // A new page is in view: remember where the channel that was first in view sits, and the channels' order.
            if (! scan.inPlace) recordPage();
            if (! scan.inPlace)
                for (int i = 0; i < slotCount() && scan.foundPage < 0; ++i)
                    if (slot (i).present && scan.originalFirst != 0 && slot (i).hash == scan.originalFirst)
                    {
                        scan.foundPage = scan.page;
                        scan.foundSlot = i;
                    }
            if (scan.plan && applyPlanHere (now))
            {
                scan.waiting = true;
                scan.waitUntil = now + kPlanSettleSeconds;
                scan.nextAt = now + kPlanPollSeconds;
                return;
            }
        }
        scanAdvance (now);
    }

    void BridgeSession::scanAdvance (double now)
    {
        if (scan.inPlace)
        {
            // A plan starts with the channels already in view; only what is left needs the bank to move.
            scan.inPlace = false;
            if (planPending.empty())
            {
                scan.active = false;
                finishPlan (now);
                return;
            }
            scan.page = 0;
            scan.nextAt = now + kScanPageSeconds;
            sendBank (BankAction::reset);
            return;
        }

        int present = 0;
        for (int i = 0; i < slotCount(); ++i)
            if (slot (i).present) ++present;
        const std::uint32_t first = slotCount() > 0 && slot (0).present ? slot (0).hash : 0;
        const bool last = first == 0 || present < slotCount() || (scan.page > 0 && first == scan.lastFirst) || scan.page >= 40;
        const bool planDone = scan.plan && planPending.empty() && (scan.foundPage >= 0 || scan.originalFirst == 0);
        if (! last && ! planDone)
        {
            scan.lastFirst = first;
            ++scan.page;
            scan.nextAt = now + kScanPageSeconds;
            sendBank (BankAction::next);
            return;
        }

        // Every page seen (not a plan that stopped early): the project's channel order is known, and a channel the
        // read didn't see was renamed or deleted since (else Analyze Mix would list its old name as a channel
        // without a Link).
        if (last && ! scan.seen.empty())
        {
            order.clear();
            orderTitles.clear();
            for (const auto& [hash, title] : scan.seen)
                if (hash != 0)
                {
                    order.push_back (hash);
                    orderTitles.push_back (title);
                }
            for (auto it = faders.begin(); it != faders.end();)
                if (std::find (order.begin(), order.end(), it->first) == order.end()) it = faders.erase (it);
                else ++it;
        }

        // Back to where the bank was: reset, whole pages, then single steps (spaced out, one per step).
        scan.restoring = true;
        scan.pending.push_back (BankAction::reset);
        if (scan.foundPage >= 0)
        {
            for (int p = 0; p < scan.foundPage; ++p) scan.pending.push_back (BankAction::next);
            for (int k = 0; k < scan.foundSlot; ++k) scan.pending.push_back (BankAction::shiftRight);
        }
        scan.nextAt = now;   // the next tick starts sending
    }

    // ---------------------------------------------------------------------------------------------
    // Plans

    BridgeSession::Result BridgeSession::applyPlan (const std::string& what, std::vector<PlanStep> steps, double now)
    {
        if (linkState != Link::connected) return { false, "Not connected to Cubase." };
        if (scan.active) return { false, scan.plan ? "Mixology is still applying " + plan.what + "." : "Mixology is reading your faders: try again in a second." };
        planPending.clear();
        for (auto& st : steps)
            if (st.hash != 0) planPending[st.hash] = std::move (st);
        if (planPending.empty()) return { false, "Nothing to change." };

        cancelMoves ({}, now);
        const auto serial = plan.serial;
        plan = {};
        plan.serial = serial;
        plan.active = true;
        plan.what = what;
        plan.total = static_cast<int> (planPending.size());
        addLog (now, what + ": changing " + std::to_string (plan.total) + " channel" + (plan.total == 1 ? "" : "s") + "...");

        scan = {};
        scan.active = true;
        scan.plan = true;
        scan.inPlace = true;
        scan.originalFirst = slotCount() > 0 && slot (0).present ? slot (0).hash : 0;
        scan.nextAt = now;
        changed();
        return { true, {} };
    }

    bool BridgeSession::applyPlanHere (double now)
    {
        bool sent = false;
        for (int i = 0; i < slotCount(); ++i)
        {
            const auto& s = slot (i);
            if (! s.present) continue;
            const auto it = planPending.find (s.hash);
            if (it == planPending.end()) continue;
            const auto step = it->second;
            planPending.erase (it);

            auto skip = [&] (const std::string& why) {
                ++plan.skipped;
                plan.notes.push_back (quoted (s.title) + ": " + why);
            };
            if (nameIsDuplicated (i)) { skip ("two channels have this name (rename one)."); continue; }
            if (isProtected (s.hash)) { skip ("protected (padlock)."); continue; }
            if (s.automationWrite) { skip ("writing automation (W is on)."); continue; }

            // Each field only changes if it is where the plan saw it: a fader or pan you moved since stays yours.
            bool doVolume = step.setVolume, doPan = step.setPan && s.hasPan;
            std::string kept;
            if (doVolume && ! s.hasVolume) { doVolume = false; kept = "Cubase didn't report its fader"; }
            if (doVolume && step.checkFrom)
            {
                const bool bothInf = isMinusInf (s.volumeDb) && isMinusInf (step.fromDb);
                if (! bothInf && ! (std::abs (s.volumeDb - step.fromDb) <= kPlanFromTolerance))
                {
                    doVolume = false;
                    kept = "its fader moved since Mixology saw it (now " + formatDb (s.volumeDb) + "): left as it is";
                }
            }
            if (doPan && step.checkPan && std::abs (s.pan - step.fromPan) > 0.01)
            {
                doPan = false;
                kept += (kept.empty() ? "" : "; ") + std::string ("its pan moved since Mixology saw it (now ") + s.panText + "): left as it is";
            }
            if (! kept.empty() && ! doVolume && ! doPan && ! step.setMute && ! step.setSolo) { skip (kept + "."); continue; }
            if (! kept.empty()) plan.notes.push_back (quoted (s.title) + ": " + kept + ".");

            // How it was just before (for this plan's undo).
            PlanStep before;
            before.hash = s.hash;
            before.title = s.title;
            before.setVolume = doVolume;
            before.exactVolume = true;
            before.position = s.volume;
            before.checkFrom = true;   // undo only if it is still where this plan puts it
            // ... which is the target as Cubase can take it: a fader stops at its top (+6 dB), so a plan asking
            // for more leaves it there, and Undo must still recognise it as the plan's own move.
            double landed = step.exactVolume ? law.toDb (step.position) : step.db;
            if (! std::isfinite (landed) || landed < -144.0) landed = FaderLaw::kMinusInf;
            before.fromDb = std::min (landed, kMaxDb);
            before.setPan = doPan;
            before.pan = s.pan;
            before.checkPan = true;
            before.fromPan = std::clamp (step.pan, 0.0, 1.0);
            before.setMute = step.setMute;
            before.mute = s.mute;
            before.setSolo = step.setSolo;
            before.solo = s.solo;
            plan.undo.push_back (before);

            takeRestorePointIfNeeded (i, now);
            if (doVolume)
            {
                if (step.exactVolume)
                {
                    if (std::abs (s.volume - step.position) > 1.0e-7)
                    {
                        auto& mv = metaRef (i).move;
                        mv = {};
                        mv.kind = MoveKind::restore;
                        mv.targetDb = law.toDb (step.position);
                        sendVolume (i, step.position, now);
                        sent = true;
                    }
                }
                else
                {
                    double db = step.db;
                    if (! std::isfinite (db) || db < -144.0) db = FaderLaw::kMinusInf;
                    db = std::min (db, kMaxDb);
                    const bool there = isMinusInf (db) ? isMinusInf (s.volumeDb) : std::abs (s.volumeDb - db) <= kDbExact;
                    if (! there)
                    {
                        startMove (i, db, now);
                        sent = true;
                    }
                }
            }
            if (doPan && std::abs (s.pan - step.pan) > 1.0e-6 && ! panIgnored (s.hash))
            {
                auto m = writeMessage (Cmd::setPan, i);
                m.value = std::clamp (step.pan, 0.0, 1.0);
                send (m);
                watchPan (i, m.value, now);
            }
            for (auto [want, cmd, on, have] : { std::tuple { step.setMute, Cmd::setMute, step.mute, s.mute },
                                                std::tuple { step.setSolo, Cmd::setSolo, step.solo, s.solo } })
            {
                if (! want || on == have) continue;
                auto m = writeMessage (cmd, i);
                m.flag = on ? 1 : 0;
                send (m);
            }
            ++plan.done;
            changed();
        }
        return sent;
    }

    void BridgeSession::finishPlan (double now)
    {
        if (! plan.active) return;
        plan.active = false;
        for (const auto& [hash, st] : planPending)
        {
            ++plan.skipped;
            plan.notes.push_back (quoted (st.title) + ": not found in Cubase (renamed or removed?).");
        }
        planPending.clear();

        plan.message = plan.what + ": changed " + std::to_string (plan.done) + " channel" + (plan.done == 1 ? "" : "s") + ".";
        if (plan.skipped > 0)
            plan.message += " " + std::to_string (plan.skipped) + " skipped.";
        addLog (now, plan.message);
        for (const auto& n : plan.notes) addLog (now, "  " + n);
        ++plan.serial;
        changed();
    }

    void BridgeSession::recordPage()
    {
        for (int i = 0; i < slotCount(); ++i)
        {
            // In slot order, each channel once: a last page Cubase shows overlapping the one before adds only the new ones.
            const auto& s = slot (i);
            if (! s.present || s.hash == 0) continue;
            if (std::none_of (scan.seen.begin(), scan.seen.end(), [&s] (const auto& c) { return c.first == s.hash; }))
                scan.seen.push_back ({ s.hash, s.title });
        }
    }

    int BridgeSession::viewOffset() const
    {
        if (order.empty() || slotCount() <= 0 || ! slot (0).present) return -1;
        const auto it = std::find (order.begin(), order.end(), slot (0).hash);
        if (it == order.end()) return -1;
        const auto first = static_cast<std::size_t> (it - order.begin());
        for (int i = 1; i < slotCount(); ++i)   // the rest in view must follow in the same order
        {
            const auto at = first + static_cast<std::size_t> (i);
            if (! slot (i).present) { if (at < order.size()) return -1; continue; }
            if (at >= order.size() || order[at] != slot (i).hash) return -1;
        }
        return static_cast<int> (first);
    }

    BridgeSession::Result BridgeSession::showChannel (int first, double now)
    {
        if (linkState != Link::connected) return { false, "Not connected to Cubase." };
        if (scan.active) return { false, scan.plan ? "Mixology is still applying " + plan.what + "." : "Mixology is moving the bank: one moment." };
        const int n = slotCount();
        const int total = static_cast<int> (order.size());
        if (n <= 0 || total == 0) return { false, "Mixology hasn't read your channels yet." };
        first = std::clamp (first, 0, std::max (0, total - n));   // a full bank at the end
        if (viewOffset() == first) return { true, {} };
        cancelMoves ({}, now);
        scan = {};
        scan.active = true;
        scan.restoring = true;
        scan.jump = true;
        scan.jumpTarget = first;
        // From the start: whole pages to the nearest page start (one that exists), then single steps either way.
        const int page = std::min ((first + n / 2) / n, std::max (0, (total - 1) / n));
        scan.pending.push_back (BankAction::reset);
        for (int p = 0; p < page; ++p) scan.pending.push_back (BankAction::next);
        const int rest = first - page * n;
        for (int k = 0; k < std::abs (rest); ++k) scan.pending.push_back (rest > 0 ? BankAction::shiftRight : BankAction::shiftLeft);
        scan.nextAt = now;
        changed();
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
