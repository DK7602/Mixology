#include "FakeCubase.h"

#include "mixology/BridgeProtocol.h"
#include "mixology/BridgeSession.h"
#include "mixology/FaderLaw.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace mixology;
using namespace mixology::bridge;

namespace
{
    struct Rig
    {
        FakeCubase cubase;
        BridgeSession session;
        double now = 0.0;

        Rig()
        {
            std::uint32_t id = 1000;
            for (auto name : { "Kick", "Snare", "Hats", "Perc", "808", "Keys", "Synth", "Guitar", "Lead Vox", "FX", "Adlibs" })
            {
                cubase.channels.push_back ({ name });
                cubase.channels.back().id = id++;
            }
            session.setPortsOpen (true, now);
        }

        // Exchange messages in 10 ms steps (MIDI round trips are well under that in practice).
        void run (double seconds)
        {
            const double end = now + seconds;
            while (now < end)
            {
                session.tick (now);
                for (auto& m : session.takeOutgoing()) cubase.receive (m);
                auto in = std::move (cubase.toPlugin);
                cubase.toPlugin.clear();
                for (auto& m : in) session.receive (m.data(), m.size(), now);
                now += 0.01;
            }
        }

        int slotOf (const std::string& name) const
        {
            for (int s = 0; s < session.slotCount(); ++s)
                if (session.slot (s).present && session.slot (s).title == name) return s;
            return -1;
        }

        double cubaseDb (int channel) const { return FakeCubase::lawDb (cubase.channels[static_cast<std::size_t> (channel)].volume); }
    };
}

TEST_CASE ("Protocol: every message survives encode -> decode", "[bridge]")
{
    Message m;
    m.cmd = Cmd::setVolume; m.slot = 7; m.token = 12345; m.hash = nameHash ("Lead Vox"); m.channelId = 0x0ABCDEF; m.value = 0.123456;
    auto d = decode (encode (m));
    REQUIRE (d);
    CHECK (d->cmd == Cmd::setVolume);
    CHECK (d->slot == 7);
    CHECK (d->token == 12345);
    CHECK (d->hash == nameHash ("Lead Vox"));
    CHECK (d->channelId == 0x0ABCDEF);
    CHECK (d->value == Catch::Approx (0.123456).margin (1.0e-6));

    Message t;
    t.cmd = Cmd::slotTitle; t.slot = 3; t.hash = 99; t.text = "Vocal - Chorus \xC3\xA9\xE2\x9C\x93";   // accents survive
    auto dt = decode (encode (t));
    REQUIRE (dt);
    CHECK (dt->text == t.text);

    // Every byte between F0 and F7 must be 7-bit.
    for (auto cmd : { Cmd::hello, Cmd::setPan, Cmd::slotColour, Cmd::slotVolume, Cmd::ack, Cmd::log })
    {
        Message x; x.cmd = cmd; x.slot = 127; x.token = 0x3FFF; x.hash = 0x0FFFFFFF; x.value = 1.0; x.rgb = 0xFFFFFF; x.flag = 127; x.text = "\xFF\xFE";
        auto bytes = encode (x);
        REQUIRE (bytes.front() == 0xF0);
        REQUIRE (bytes.back() == 0xF7);
        for (std::size_t i = 1; i + 1 < bytes.size(); ++i) CHECK (bytes[i] < 0x80);
        CHECK (decode (bytes));
    }
}

TEST_CASE ("Protocol: foreign, truncated and wrong-version frames are ignored", "[bridge]")
{
    Message m; m.cmd = Cmd::slotVolume; m.slot = 1; m.value = 0.5; m.text = "-6.00";
    auto bytes = encode (m);

    auto shorter = bytes; shorter.erase (shorter.end() - 3); CHECK_FALSE (decode (shorter));
    auto longer = bytes; longer.insert (longer.end() - 1, 0x01); CHECK_FALSE (decode (longer));
    auto version = bytes; version[4] = 2; CHECK_FALSE (decode (version));
    auto foreign = bytes; foreign[1] = 0x43; CHECK_FALSE (decode (foreign));   // Yamaha / Steinberg ID
    const std::uint8_t noteOn[] = { 0x90, 60, 100 };
    CHECK_FALSE (decode (noteOn, 3));
}

TEST_CASE ("Protocol: long names are cut on a UTF-8 boundary", "[bridge]")
{
    std::string s (62, 'a');
    s += "\xC3\xA9";   // 2-byte character straddling the 63-byte limit
    CHECK (truncateUtf8 (s, 63).size() == 62);
    CHECK (nameHash ("Kick") != nameHash ("kick"));
    CHECK (nameHash ("Kick") <= 0x0FFFFFFFu);
}

TEST_CASE ("FaderLaw: parses Cubase's display text", "[bridge]")
{
    CHECK (*FaderLaw::parseDb ("-6.02") == Catch::Approx (-6.02));
    CHECK (*FaderLaw::parseDb ("+6.00") == Catch::Approx (6.0));
    CHECK (*FaderLaw::parseDb ("0.00 dB") == Catch::Approx (0.0));
    CHECK (*FaderLaw::parseDb ("-12,50") == Catch::Approx (-12.5));
    CHECK (*FaderLaw::parseDb ("\xE2\x88\x92" "3.10") == Catch::Approx (-3.1));
    CHECK (*FaderLaw::parseDb ("-oo") == FaderLaw::kMinusInf);
    CHECK (*FaderLaw::parseDb ("-\xE2\x88\x9E") == FaderLaw::kMinusInf);
    CHECK_FALSE (FaderLaw::parseDb ("C"));
    CHECK_FALSE (FaderLaw::parseDb (""));
}

TEST_CASE ("FaderLaw: learns a curve and inverts it", "[bridge]")
{
    FaderLaw law;
    for (int i = 1; i <= 40; ++i)
    {
        const double n = i / 40.0;
        law.learn (n, FakeCubase::lawDb (n));
    }
    for (double db : { -40.0, -20.0, -12.0, -6.0, -3.0, 0.0, 3.0, 6.0 })
    {
        const double n = law.toNormalised (db);
        CHECK (FakeCubase::lawDb (n) == Catch::Approx (db).margin (0.3));
    }
    FaderLaw copy;
    copy.deserialise (law.serialise());
    CHECK (copy.size() == law.size());
    CHECK (copy.toNormalised (-6.0) == Catch::Approx (law.toNormalised (-6.0)));
}

TEST_CASE ("Bridge: connects, lists the bank, and reads the real fader", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.link() == BridgeSession::Link::connected);
    REQUIRE (r.session.slotCount() == 8);
    CHECK (r.session.slot (0).title == "Kick");
    CHECK (r.session.slot (7).title == "Guitar");
    CHECK (r.session.slot (0).volumeText == FakeCubase::displayText (0.7766));

    r.cubase.userMovesFader (1, 0.5);
    r.run (0.5);
    CHECK (r.session.slot (1).volumeDb == Catch::Approx (FakeCubase::lawDb (0.5)).margin (0.006));
}

TEST_CASE ("Bridge: sets exact dB values on the locked channel, closed loop", "[bridge]")
{
    Rig r;
    r.run (0.2);
    const int kick = r.slotOf ("Kick");
    REQUIRE (r.session.lockChannel (kick, r.now).ok);

    for (double target : { 0.0, -6.0, -12.0, 6.0, -0.5, -30.0 })
    {
        REQUIRE (r.session.setVolumeDb (target, r.now).ok);
        r.run (0.5);
        CHECK_FALSE (r.session.moveInProgress());
        CHECK (r.cubaseDb (0) == Catch::Approx (target).margin (BridgeSession::kDbTolerance + 0.005));
        INFO (r.session.lastMoveResult());
    }

    REQUIRE (r.session.setVolumeDb (FaderLaw::kMinusInf, r.now).ok);
    r.run (0.3);
    CHECK (r.cubase.channels[0].volume == 0.0);

    // Nothing else moved.
    for (std::size_t c = 1; c < r.cubase.channels.size(); ++c)
        CHECK (r.cubase.channels[c].volume == 0.7766);
}

TEST_CASE ("Bridge: after learning, moves land in one or two steps", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    // Teach the curve by "watching" the user sweep a fader on another channel.
    for (int i = 1; i <= 30; ++i) { r.cubase.userMovesFader (1, i / 30.0); r.run (0.02); }

    const int before = r.cubase.setVolumeCount;
    REQUIRE (r.session.setVolumeDb (-9.0, r.now).ok);
    r.run (0.3);
    CHECK (r.cubase.setVolumeCount - before <= 2);
    CHECK (r.cubaseDb (0) == Catch::Approx (-9.0).margin (0.06));
}

TEST_CASE ("Bridge: nothing is written without a lock", "[bridge]")
{
    Rig r;
    r.run (0.2);
    CHECK_FALSE (r.session.setVolumeDb (-6.0, r.now).ok);
    CHECK_FALSE (r.session.setMute (true, r.now).ok);
    r.run (0.2);
    CHECK (r.cubase.setVolumeCount == 0);
}

TEST_CASE ("Bridge: a bank change pauses writing and never moves the wrong channel", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (r.slotOf ("Snare"), r.now).ok);
    REQUIRE (r.session.setVolumeDb (-3.0, r.now).ok);
    r.run (0.3);

    r.session.bank (BankAction::shiftRight, r.now);   // slot 1 now shows "Hats"
    r.run (0.2);
    CHECK (r.session.lock() == BridgeSession::Lock::paused);
    CHECK_FALSE (r.session.setVolumeDb (-20.0, r.now).ok);
    r.run (0.2);
    CHECK (r.cubaseDb (2) == Catch::Approx (FakeCubase::lawDb (0.7766)));   // Hats untouched
}

TEST_CASE ("Bridge: the script refuses a write that races a bank change", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    r.run (0.05);

    // The write leaves Mixology, then the bank moves in Cubase before it arrives.
    REQUIRE (r.session.setVolumeDb (-18.0, r.now).ok);
    auto pending = r.session.takeOutgoing();
    r.cubase.bankOffset = 1;   // the user scrolled the mixer
    for (auto& m : pending) r.cubase.receive (m);
    r.run (0.3);

    CHECK (r.cubase.setVolumeCount == 0);
    CHECK (r.cubase.channels[1].volume == 0.7766);   // Snare (now in slot 0) untouched
    CHECK (r.session.lock() == BridgeSession::Lock::paused);
}

TEST_CASE ("Bridge: renaming or reordering tracks pauses writing", "[bridge]")
{
    SECTION ("rename")
    {
        Rig r;
        r.run (0.2);
        REQUIRE (r.session.lockChannel (0, r.now).ok);   // Kick
        r.cubase.rename (0, "Kick In");
        r.run (0.1);
        CHECK (r.session.lock() == BridgeSession::Lock::paused);
        CHECK (r.session.pauseReason().find ("Kick In") != std::string::npos);
    }
    SECTION ("reorder")
    {
        Rig r;
        r.run (0.2);
        REQUIRE (r.session.lockChannel (2, r.now).ok);   // Hats
        r.cubase.moveChannel (5, 0);                       // Keys dragged to the top: Hats shifts to slot 3
        r.run (0.1);
        CHECK (r.session.lock() == BridgeSession::Lock::paused);
        CHECK_FALSE (r.session.setVolumeDb (-6.0, r.now).ok);
        // Re-locking Hats in its new slot works.
        REQUIRE (r.session.lockChannel (r.slotOf ("Hats"), r.now).ok);
        REQUIRE (r.session.setVolumeDb (-6.0, r.now).ok);
        r.run (0.3);
        CHECK (r.cubaseDb (3) == Catch::Approx (-6.0).margin (0.06));
    }
}

TEST_CASE ("Bridge: duplicate names elsewhere in the bank don't confuse the lock", "[bridge]")
{
    Rig r;
    r.cubase.channels[3].name = "Kick";   // two channels named Kick
    r.run (0.2);
    REQUIRE (r.session.lockChannel (3, r.now).ok);
    REQUIRE (r.session.setVolumeDb (-10.0, r.now).ok);
    r.run (0.3);
    CHECK (r.cubaseDb (3) == Catch::Approx (-10.0).margin (0.06));
    CHECK (r.cubase.channels[0].volume == 0.7766);
}

TEST_CASE ("Bridge: channels writing automation are never moved", "[bridge]")
{
    Rig r;
    r.cubase.channels[0].write = true;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    auto res = r.session.setVolumeDb (-6.0, r.now);
    CHECK_FALSE (res.ok);
    CHECK (res.message.find ("automation") != std::string::npos);
    r.run (0.2);
    CHECK (r.cubase.setVolumeCount == 0);
}

TEST_CASE ("Bridge: Cubase fader moves are seen and never echoed back", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    for (int i = 0; i < 20; ++i) { r.cubase.userMovesFader (0, 0.5 + i * 0.01); r.run (0.01); }
    r.run (0.5);
    CHECK (r.cubase.setVolumeCount == 0);   // no feedback loop
    CHECK (r.session.slot (0).volume == Catch::Approx (0.69).margin (1.0e-5));
}

TEST_CASE ("Bridge: Restore puts back volume, pan, mute and solo", "[bridge]")
{
    Rig r;
    r.cubase.channels[0].volume = 0.6123;
    r.cubase.channels[0].pan = 0.3;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    REQUIRE (r.session.setVolumeDb (2.0, r.now).ok);
    r.run (0.3);
    REQUIRE (r.session.setPan (0.8, r.now).ok);
    REQUIRE (r.session.setMute (true, r.now).ok);
    r.run (0.2);
    CHECK (r.cubase.channels[0].mute);

    REQUIRE (r.session.restore (r.now).ok);
    r.run (0.3);
    CHECK (r.cubase.channels[0].volume == Catch::Approx (0.6123).margin (1.0 / 16384.0));
    CHECK (r.cubase.channels[0].pan == Catch::Approx (0.3).margin (1.0e-6));
    CHECK_FALSE (r.cubase.channels[0].mute);
}

TEST_CASE ("Bridge: losing Cubase pauses, reconnecting to the same channel resumes", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (4, r.now).ok);   // 808
    r.cubase.alive = false;
    r.run (BridgeSession::kLostAfter + 0.5);
    CHECK (r.session.link() == BridgeSession::Link::lost);
    CHECK (r.session.lock() == BridgeSession::Lock::paused);

    r.cubase.alive = true;
    r.run (1.5);
    CHECK (r.session.link() == BridgeSession::Link::connected);
    CHECK (r.session.lock() == BridgeSession::Lock::locked);
    CHECK (r.session.setVolumeDb (-4.0, r.now).ok);
}

TEST_CASE ("Bridge: reconnecting to a different channel in that slot stays paused", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (4, r.now).ok);
    r.cubase.alive = false;
    r.run (BridgeSession::kLostAfter + 0.5);
    r.cubase.channels[4].name = "Bass Gtr";   // project changed while disconnected
    r.cubase.alive = true;
    r.run (1.5);
    CHECK (r.session.link() == BridgeSession::Link::connected);
    CHECK (r.session.lock() == BridgeSession::Lock::paused);
    CHECK_FALSE (r.session.setVolumeDb (-4.0, r.now).ok);
}

TEST_CASE ("Bridge: a script reload mid-session is handled", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (1, r.now).ok);
    r.cubase.activate();   // user reloads scripts in Cubase
    r.run (0.3);
    CHECK (r.session.link() == BridgeSession::Link::connected);
    CHECK (r.session.lock() == BridgeSession::Lock::locked);
}

TEST_CASE ("Bridge: log shows each move's result (manual check)", "[bridge][.print]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    for (double target : { 0.0, -6.0, -12.0, 6.0 })
    {
        r.session.setVolumeDb (target, r.now);
        r.run (0.5);
    }
    for (auto& l : r.session.logLines()) printf ("%6.2f  %s\n", l.time, l.text.c_str());
}

TEST_CASE ("Bridge: two channels with the same name are told apart by Cubase's channel ID", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);    // "Kick", id 1000
    // Swap in a different channel that has the same name (e.g. a duplicated track) at the same slot.
    r.cubase.channels[0].id = 4242;
    r.cubase.sendSlot (0);
    r.run (0.1);
    CHECK (r.session.lock() == BridgeSession::Lock::paused);
    CHECK_FALSE (r.session.setVolumeDb (-6.0, r.now).ok);
}

TEST_CASE ("Bridge: a rename keeps the ID, is reported as a rename, and still pauses", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (1, r.now).ok);
    r.cubase.rename (1, "Snare Top");
    r.run (0.1);
    CHECK (r.session.lock() == BridgeSession::Lock::paused);
    CHECK (r.session.pauseReason().find ("renamed") != std::string::npos);
}

TEST_CASE ("Bridge: a stale fader report during a move is not taken as the result", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    REQUIRE (r.session.setVolumeDb (-20.0, r.now).ok);
    // A late report of the OLD position arrives before Cubase applies the move.
    r.cubase.sendVolume (0);
    r.run (0.5);
    CHECK (r.cubaseDb (0) == Catch::Approx (-20.0).margin (0.06));
    CHECK (r.session.lastMoveResult().find ("-20.00") != std::string::npos);
}

TEST_CASE ("Bridge: dragging the fader in Mixology sends exact positions, latest wins", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.lockChannel (0, r.now).ok);
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE (r.session.setVolumeNormalised (0.5 + i * 0.02, r.now).ok);
        r.run (0.005);   // faster than the round trip: moves overlap
    }
    r.run (0.3);
    CHECK (r.cubase.channels[0].volume == Catch::Approx (0.68).margin (1.0 / 16384.0));
    CHECK_FALSE (r.session.moveInProgress());
    for (std::size_t c = 1; c < r.cubase.channels.size(); ++c)
        CHECK (r.cubase.channels[c].volume == 0.7766);
}

TEST_CASE ("FaderLaw: a contradicting reading replaces stale points", "[bridge]")
{
    FaderLaw law;
    law.learn (0.5, -10.0);
    law.learn (0.6, -30.0);   // impossible: lower dB higher up -> the older point goes
    CHECK (law.size() == 1);
    CHECK (law.toDb (0.6) == Catch::Approx (-30.0));
}
