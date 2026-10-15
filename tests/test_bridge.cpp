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
            for (auto name : { "Kick", "Snare", "Hats", "Perc", "808", "Keys", "Synth", "Guitar", "Lead Vox", "FX", "Adlibs" })
                cubase.channels.push_back ({ name });   // script v3+ reports no channel IDs (0)
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

        ChannelRef ref (const std::string& name) const { return session.refOf (slotOf (name)); }

        bool logHas (const std::string& text) const
        {
            for (const auto& l : session.logLines())
                if (l.text.find (text) != std::string::npos) return true;
            return false;
        }

        int logCount (const std::string& text) const
        {
            int n = 0;
            for (const auto& l : session.logLines())
                if (l.text.find (text) != std::string::npos) ++n;
            return n;
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


TEST_CASE ("FaderLaw: a contradicting reading replaces stale points", "[bridge]")
{
    FaderLaw law;
    law.learn (0.5, -10.0);
    law.learn (0.6, -30.0);   // impossible: lower dB higher up -> the older point goes
    CHECK (law.size() == 1);
    CHECK (law.toDb (0.6) == Catch::Approx (-30.0));
}

// =================================================================================================
// Milestone 2: every channel in the bank can be moved, with the same safety per write.

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

TEST_CASE ("Bridge: sets exact dB values on any channel, closed loop", "[bridge]")
{
    Rig r;
    r.run (0.2);
    for (int ch : { 0, 3, 7 })
    {
        const auto ref = r.session.refOf (ch);
        for (double target : { 0.0, -6.0, -12.0, 6.0, -0.5, -30.0 })
        {
            REQUIRE (r.session.setVolumeDb (ref, target, r.now).ok);
            r.run (0.5);
            INFO (r.session.lastMoveResult());
            CHECK_FALSE (r.session.moveInProgress());
            CHECK (r.cubaseDb (ch) == Catch::Approx (target).margin (BridgeSession::kDbTolerance + 0.005));
        }
        REQUIRE (r.session.setVolumeDb (ref, FaderLaw::kMinusInf, r.now).ok);
        r.run (0.3);
        CHECK (r.cubase.channels[static_cast<std::size_t> (ch)].volume == 0.0);
    }
    for (std::size_t c : { 1u, 2u, 4u, 5u, 6u, 8u })
        CHECK (r.cubase.channels[c].volume == 0.7766);   // nothing else moved
}

TEST_CASE ("Bridge: moves on several channels at once all land", "[bridge]")
{
    Rig r;
    r.run (0.2);
    const double targets[] = { -3.0, -7.5, -12.0, 2.0, -20.0, -1.0, -9.0, 0.0 };
    for (int ch = 0; ch < 8; ++ch)
        REQUIRE (r.session.setVolumeDb (r.session.refOf (ch), targets[ch], r.now).ok);
    r.run (0.8);
    CHECK_FALSE (r.session.moveInProgress());
    for (int ch = 0; ch < 8; ++ch)
        CHECK (r.cubaseDb (ch) == Catch::Approx (targets[ch]).margin (BridgeSession::kDbTolerance + 0.005));
}

TEST_CASE ("Bridge: after learning, moves land in one or two steps", "[bridge]")
{
    Rig r;
    r.run (0.2);
    // Teach the curve by "watching" the user set a fader on another channel to many positions.
    for (int i = 1; i <= 30; ++i) { r.cubase.userMovesFader (1, i / 30.0); r.run (0.3); }

    const int before = r.cubase.setVolumeCount;
    REQUIRE (r.session.setVolumeDb (r.ref ("Kick"), -9.0, r.now).ok);
    r.run (0.3);
    CHECK (r.cubase.setVolumeCount - before <= 2);
    CHECK (r.cubaseDb (0) == Catch::Approx (-9.0).margin (0.06));
}

TEST_CASE ("Bridge: a strip that now shows another channel is refused here", "[bridge]")
{
    Rig r;
    r.run (0.2);
    const auto snare = r.ref ("Snare");             // slot 1, as the UI showed it
    r.session.bank (BankAction::shiftRight, r.now);  // slot 1 now shows "Hats"
    r.run (0.2);
    REQUIRE (r.session.slot (1).title == "Hats");

    auto res = r.session.setVolumeDb (snare, -20.0, r.now);
    CHECK_FALSE (res.ok);
    CHECK (res.message.find ("Hats") != std::string::npos);
    CHECK_FALSE (r.session.setMute (snare, true, r.now).ok);
    CHECK_FALSE (r.session.setVolumeNormalised (snare, 0.1, r.now).ok);
    r.run (0.2);
    CHECK (r.cubase.setVolumeCount == 0);
    CHECK_FALSE (r.cubase.channels[2].mute);
}

TEST_CASE ("Bridge: the script refuses a write that races a bank change", "[bridge]")
{
    Rig r;
    r.run (0.2);
    // The write leaves Mixology, then the bank moves in Cubase before it arrives.
    REQUIRE (r.session.setVolumeDb (r.ref ("Kick"), -18.0, r.now).ok);
    auto pending = r.session.takeOutgoing();
    r.cubase.bankOffset = 1;   // the user scrolled the mixer
    for (auto& m : pending) r.cubase.receive (m);
    r.run (0.3);

    CHECK (r.cubase.setVolumeCount == 0);
    CHECK (r.cubase.channels[1].volume == 0.7766);   // Snare (now in slot 0) untouched
    CHECK_FALSE (r.session.moveInProgress());
    CHECK (r.session.lastMoveResult().find ("Nothing was changed") != std::string::npos);
    CHECK (r.session.slot (0).title == "Snare");      // the refusal triggered a fresh dump
}

TEST_CASE ("Bridge: renaming or reordering stops a move in flight", "[bridge]")
{
    SECTION ("rename")
    {
        Rig r;
        r.run (0.2);
        const auto kick = r.ref ("Kick");
        r.cubase.rename (0, "Kick In");
        r.run (0.1);
        CHECK_FALSE (r.session.setVolumeDb (kick, -6.0, r.now).ok);   // the old picture is refused
        CHECK (r.session.setVolumeDb (r.ref ("Kick In"), -6.0, r.now).ok);
        r.run (0.3);
        CHECK (r.cubaseDb (0) == Catch::Approx (-6.0).margin (0.06));
    }
    SECTION ("reorder during a move")
    {
        Rig r;
        r.run (0.2);
        REQUIRE (r.session.setVolumeDb (r.ref ("Hats"), -30.0, r.now).ok);
        auto pending = r.session.takeOutgoing();
        r.cubase.moveChannel (5, 0);   // Keys dragged to the top: Hats shifts to slot 3
        r.run (0.05);
        for (auto& m : pending) r.cubase.receive (m);   // arrives too late: refused by the script
        r.run (0.3);
        CHECK (r.cubase.setVolumeCount == 0);
        CHECK_FALSE (r.session.moveInProgress());
        for (const auto& c : r.cubase.channels) CHECK (c.volume == 0.7766);
    }
}

TEST_CASE ("Bridge: two channels with the same name: the slot decides, Restore refuses", "[bridge]")
{
    Rig r;
    r.cubase.channels[3].name = "Kick";   // two channels named Kick
    r.run (0.2);
    CHECK (r.session.nameIsDuplicated (0));
    CHECK (r.session.nameIsDuplicated (3));
    CHECK_FALSE (r.session.nameIsDuplicated (1));

    REQUIRE (r.session.setVolumeDb (r.session.refOf (3), -10.0, r.now).ok);
    r.run (0.3);
    CHECK (r.cubaseDb (3) == Catch::Approx (-10.0).margin (0.06));
    CHECK (r.cubase.channels[0].volume == 0.7766);

    auto res = r.session.restoreChannel (r.session.refOf (3), r.now);
    CHECK_FALSE (res.ok);
    CHECK (res.message.find ("Two channels") != std::string::npos);
}

TEST_CASE ("Bridge: channels writing automation are never moved; Read gets a warning", "[bridge]")
{
    Rig r;
    r.cubase.channels[0].write = true;
    r.cubase.channels[1].read = true;
    r.run (0.2);

    auto res = r.session.setVolumeDb (r.ref ("Kick"), -6.0, r.now);
    CHECK_FALSE (res.ok);
    CHECK (res.message.find ("automation") != std::string::npos);
    CHECK_FALSE (r.session.setPan (r.ref ("Kick"), 0.2, r.now).ok);

    auto snare = r.session.setVolumeDb (r.ref ("Snare"), -6.0, r.now);
    CHECK (snare.ok);
    CHECK (snare.warning.find ("Read") != std::string::npos);
    r.run (0.3);
    CHECK (r.session.setVolumeDb (r.ref ("Snare"), -7.0, r.now).warning.empty());   // warned once
    r.run (0.3);
    CHECK (r.cubase.channels[0].volume == 0.7766);
    CHECK (r.cubaseDb (1) == Catch::Approx (-7.0).margin (0.06));
}

TEST_CASE ("Bridge: protected channels are never written, wherever they are in the bank", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.setProtected (r.ref ("Snare"), true, r.now).ok);
    CHECK (r.session.isProtected (r.session.slot (1).hash));

    auto res = r.session.setVolumeDb (r.ref ("Snare"), -6.0, r.now);
    CHECK_FALSE (res.ok);
    CHECK (res.message.find ("protected") != std::string::npos);
    CHECK_FALSE (r.session.setMute (r.ref ("Snare"), true, r.now).ok);
    CHECK_FALSE (r.session.setVolumeNormalised (r.ref ("Snare"), 0.2, r.now).ok);

    // Still protected after the bank moves (it is the channel that is protected, not the slot).
    r.session.bank (BankAction::shiftRight, r.now);
    r.run (0.2);
    REQUIRE (r.session.slot (0).title == "Snare");
    CHECK_FALSE (r.session.setVolumeDb (r.session.refOf (0), -6.0, r.now).ok);
    CHECK (r.session.setVolumeDb (r.session.refOf (1), -6.0, r.now).ok);   // Hats is not protected
    r.run (0.3);
    CHECK (r.cubase.channels[1].volume == 0.7766);

    // The list survives a save / load (it is stored with the project).
    auto list = r.session.protectedList();
    BridgeSession other;
    other.setProtectedList (list);
    CHECK (other.isProtected (nameHash ("Snare")));
    CHECK_FALSE (other.isProtected (nameHash ("Hats")));

    REQUIRE (r.session.setProtected (r.session.refOf (0), false, r.now).ok);
    CHECK (r.session.setVolumeDb (r.session.refOf (0), -6.0, r.now).ok);
}

TEST_CASE ("Bridge: Cubase fader moves are seen, never echoed, and logged once", "[bridge]")
{
    Rig r;
    r.run (0.2);
    for (int i = 0; i < 20; ++i) { r.cubase.userMovesFader (0, 0.5 + i * 0.01); r.run (0.01); }
    r.run (0.5);
    CHECK (r.cubase.setVolumeCount == 0);   // no feedback loop
    CHECK (r.session.slot (0).volume == Catch::Approx (0.69).margin (1.0e-5));
    CHECK (r.logCount ("Fader moved in Cubase: 'Kick'") == 1);

    // Cubase repeating the same value (it does, e.g. on Refresh) is not a move.
    for (int i = 0; i < 5; ++i) { r.cubase.sendVolume (0); r.run (0.5); }
    r.session.requestDump (r.now);
    r.run (0.5);
    CHECK (r.logCount ("Fader moved in Cubase: 'Kick'") == 1);
    CHECK_FALSE (r.logHas ("Fader moved in Cubase: 'Snare'"));   // the first report is where it started
}

TEST_CASE ("Bridge: dragging in Mixology sends exact positions, latest wins, logged once", "[bridge]")
{
    Rig r;
    r.run (0.2);
    const auto kick = r.ref ("Kick");
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE (r.session.setVolumeNormalised (kick, 0.5 + i * 0.02, r.now).ok);
        r.run (0.005);   // faster than the round trip: moves overlap
    }
    r.session.noteDragFinished (kick, r.now);
    r.run (1.0);
    CHECK (r.cubase.channels[0].volume == Catch::Approx (0.68).margin (1.0 / 16384.0));
    CHECK_FALSE (r.session.moveInProgress());
    CHECK (r.logCount ("Fader dragged in Mixology: 'Kick'") == 1);
    CHECK_FALSE (r.logHas ("Fader moved in Cubase"));   // our own readbacks are not Cubase moves
    for (std::size_t c = 1; c < r.cubase.channels.size(); ++c)
        CHECK (r.cubase.channels[c].volume == 0.7766);
}

TEST_CASE ("Bridge: nudges add up while Cubase is still answering", "[bridge]")
{
    Rig r;
    r.run (0.2);
    for (int i = 0; i < 6; ++i) REQUIRE (r.session.nudgeDb (r.ref ("Perc"), -0.5, r.now).ok);
    r.run (0.6);
    CHECK (r.cubaseDb (3) == Catch::Approx (FakeCubase::lawDb (0.7766) - 3.0).margin (0.06));
}

TEST_CASE ("Bridge: Restore puts back volume, pan, mute and solo", "[bridge]")
{
    Rig r;
    r.cubase.channels[0].volume = 0.6123;
    r.cubase.channels[0].pan = 0.3;
    r.run (0.2);
    const auto kick = r.ref ("Kick");
    REQUIRE (r.session.setVolumeDb (kick, 2.0, r.now).ok);
    r.run (0.3);
    REQUIRE (r.session.setPan (kick, 0.8, r.now).ok);
    REQUIRE (r.session.setMute (kick, true, r.now).ok);
    r.run (0.2);
    CHECK (r.cubase.channels[0].mute);
    CHECK (r.session.changedCount() == 1);

    REQUIRE (r.session.restoreChannel (kick, r.now).ok);
    r.run (0.3);
    CHECK (r.cubase.channels[0].volume == Catch::Approx (0.6123).margin (1.0 / 16384.0));
    CHECK (r.cubase.channels[0].pan == Catch::Approx (0.3).margin (1.0e-6));
    CHECK_FALSE (r.cubase.channels[0].mute);
    CHECK (r.session.changedCount() == 0);
    CHECK (r.session.lastMoveResult().find ("Restored 'Kick'") != std::string::npos);
}

TEST_CASE ("Bridge: a channel whose pan Cubase ignores (mono group) is learned and left alone", "[bridge]")
{
    Rig r;
    r.cubase.channels[1].pan = 0.0;   // what the MIDI Remote reports for a mono track into a mono group: "L100"
    r.cubase.channels[1].panIgnored = true;
    r.run (0.2);
    const auto snare = r.ref ("Snare");
    CHECK_FALSE (r.session.panIgnored (snare.hash));
    REQUIRE (r.session.setPan (snare, 0.5, r.now).ok);   // the user turns it to the middle: Cubase doesn't follow
    r.run (0.5);
    CHECK_FALSE (r.session.panIgnored (snare.hash));     // not yet: Cubase may still be answering
    r.run (1.0);
    CHECK (r.session.panIgnored (snare.hash));
    bool logged = false;
    for (const auto& line : r.session.logLines()) logged = logged || line.text.find ("didn't follow the pan move") != std::string::npos;
    CHECK (logged);
    const auto again = r.session.setPan (snare, 0.7, r.now);
    CHECK_FALSE (again.ok);
    CHECK (again.message.find ("output is mono") != std::string::npos);

    // A channel whose pan works is never marked.
    const auto kick = r.ref ("Kick");
    REQUIRE (r.session.setPan (kick, 0.2, r.now).ok);
    r.run (2.0);
    CHECK_FALSE (r.session.panIgnored (kick.hash));
    CHECK (r.cubase.channels[0].pan == Catch::Approx (0.2).margin (1.0e-6));
}

TEST_CASE ("Bridge: a pan that seems to land, then reads back at hard left, is a channel Cubase doesn't pan", "[bridge]")
{
    // The user's Electric guitars: mono tracks into a mono group. The old script echoes the pan it sent (so the
    // move looks done), but the next time Cubase reports the channel (a bank change, Auto Mix's walk) it is
    // hard left ("L100") again.
    Rig r;
    r.cubase.echoPanWrites = true;
    r.cubase.channels[1].pan = 0.0;
    r.cubase.channels[1].panIgnored = true;
    r.run (0.2);
    const auto snare = r.ref ("Snare"), kick = r.ref ("Kick");
    REQUIRE (r.session.setPan (snare, 0.5, r.now).ok);   // set to the middle in Mixology
    REQUIRE (r.session.setPan (kick, 0.3, r.now).ok);    // a channel that pans
    r.run (2.0);
    CHECK_FALSE (r.session.panIgnored (snare.hash));     // the echo said it landed
    const auto before = r.session.noPanVersion();
    r.cubase.dump();                                     // Cubase reports the bank again
    r.run (0.3);
    CHECK (r.session.panIgnored (snare.hash));
    CHECK_FALSE (r.session.panIgnored (kick.hash));      // it really is at L40
    CHECK (r.session.noPanVersion() != before);
    bool logged = false;
    for (const auto& line : r.session.logLines()) logged = logged || line.text.find ("reported it back at hard left") != std::string::npos;
    CHECK (logged);
    CHECK_FALSE (r.session.setPan (snare, 0.5, r.now).ok);   // never touched again
    REQUIRE (r.session.noPanList().size() == 1);
    CHECK (r.session.noPanList()[0] == snare.hash);

    // A pan written AT hard left that stays there is just a hard-left pan.
    REQUIRE (r.session.setPan (kick, 0.0, r.now).ok);
    r.run (2.0);
    r.cubase.dump();
    r.run (0.3);
    CHECK_FALSE (r.session.panIgnored (kick.hash));

    // The routing gets fixed in Cubase and the user pans it there: Mixology pans it again.
    r.cubase.channels[1].panIgnored = false;
    r.cubase.userMovesPan (1, 0.6);
    r.run (0.2);
    CHECK_FALSE (r.session.panIgnored (snare.hash));
    CHECK (r.session.setPan (snare, 0.5, r.now).ok);

    // The list comes back with the project.
    Rig again;
    again.run (0.2);
    again.session.setNoPanList ({ again.ref ("Guitar").hash, 0u });
    CHECK (again.session.panIgnored (again.ref ("Guitar").hash));
    CHECK (again.session.noPanList().size() == 1);
}

TEST_CASE ("Bridge: Restore all brings back every changed channel in view", "[bridge]")
{
    Rig r;
    r.run (0.2);
    for (const char* name : { "Kick", "Hats", "808", "Guitar" })
        REQUIRE (r.session.setVolumeDb (r.ref (name), -15.0, r.now).ok);
    REQUIRE (r.session.setSolo (r.ref ("Keys"), true, r.now).ok);
    r.run (0.6);
    CHECK (r.session.changedCount() == 5);

    // Kick leaves the bank; Hats gets protected after it was changed.
    r.session.bank (BankAction::shiftRight, r.now);
    r.run (0.2);
    REQUIRE (r.session.setProtected (r.ref ("Hats"), true, r.now).ok);

    // Hats unprotected again: everything in view comes back right away.
    REQUIRE (r.session.setProtected (r.ref ("Hats"), false, r.now).ok);
    r.session.bank (BankAction::reset, r.now);
    r.run (0.2);
    REQUIRE (r.session.setProtected (r.ref ("Hats"), true, r.now).ok);
    auto sum = r.session.restoreAll (r.now);
    r.run (0.5);
    CHECK (sum.restored == 4);       // Kick, 808, Guitar, Keys
    CHECK (sum.outsideBank == 0);
    CHECK (sum.skipped == 1);        // Hats (protected)
    CHECK (r.cubase.channels[0].volume == Catch::Approx (0.7766).margin (1.0 / 16384.0));
    CHECK (r.cubase.channels[4].volume == Catch::Approx (0.7766).margin (1.0 / 16384.0));
    CHECK (r.cubase.channels[7].volume == Catch::Approx (0.7766).margin (1.0 / 16384.0));
    CHECK_FALSE (r.cubase.channels[5].solo);
    CHECK (r.cubaseDb (2) == Catch::Approx (-15.0).margin (0.06));   // protected: left alone
}

TEST_CASE ("Bridge: Restore all reaches channels outside the bank, then the bank comes back", "[bridge]")
{
    Rig r;
    r.run (0.2);
    for (const char* name : { "Kick", "Snare", "Guitar" })
        REQUIRE (r.session.setVolumeDb (r.ref (name), -15.0, r.now).ok);
    r.run (0.6);
    r.session.bank (BankAction::next, r.now);           // the user looks at channels 8 - 10
    r.run (0.3);
    REQUIRE (r.session.slot (0).title == "Lead Vox");

    const auto sum = r.session.restoreAll (r.now);
    CHECK (sum.outsideBank == 3);                       // Kick, Snare, Guitar
    CHECK (r.session.applying());
    CHECK_FALSE (r.session.setVolumeDb (r.session.refOf (0), -6.0, r.now).ok);   // nothing else moves meanwhile
    r.run (5.0);
    CHECK_FALSE (r.session.applying());
    for (int ch : { 0, 1, 7 })
        CHECK (r.cubase.channels[static_cast<std::size_t> (ch)].volume == Catch::Approx (0.7766).margin (1.0 / 16384.0));
    CHECK (r.cubase.bankOffset == 8);                   // back where the user was
    CHECK (r.session.planProgress().done == 3);
    CHECK (r.session.changedCount() == 0);
}

TEST_CASE ("Bridge: a plan sets levels and pans across the whole project, with its own undo", "[bridge]")
{
    Rig r;
    for (int i = 0; i < 11; ++i) r.cubase.channels[static_cast<std::size_t> (i)].volume = 0.5 + 0.02 * i;
    r.run (0.2);
    r.session.bank (BankAction::shiftRight, r.now);     // the user looks at channels 1 - 8
    r.run (0.3);
    REQUIRE (r.session.slot (0).title == "Snare");
    REQUIRE (r.session.scanAllChannels (r.now).ok);
    r.run (3.0);
    REQUIRE (r.session.knownFaders().size() == 11);
    REQUIRE (r.session.setProtected (r.ref ("Keys"), true, r.now).ok);

    auto from = [&] (const char* name) {
        const auto it = r.session.knownFaders().find (nameHash (name));
        return it != r.session.knownFaders().end() ? it->second.db : 0.0;
    };
    std::vector<BridgeSession::PlanStep> steps;
    auto add = [&] (const char* name, double db, bool pan = false, double p = 0.5) {
        BridgeSession::PlanStep st;
        st.hash = nameHash (name);
        st.title = name;
        st.setVolume = true;
        st.db = db;
        st.checkFrom = true;
        st.fromDb = from (name);
        st.setPan = pan;
        st.pan = p;
        steps.push_back (st);
    };
    add ("Kick", -4.0);                 // outside the bank (left of it)
    add ("Lead Vox", -2.5, true, 0.5);
    add ("Adlibs", -9.0, true, 0.2);    // outside (right of it)
    add ("Keys", -20.0);                // protected: skipped
    add ("Gone", -3.0);                 // not in the project
    r.cubase.userMovesFader (9, 0.3);   // FX: moved in Cubase since the plan was made
    r.run (0.3);
    add ("FX", -10.0);

    REQUIRE (r.session.applyPlan ("Balance", steps, r.now).ok);
    CHECK_FALSE (r.session.scanAllChannels (r.now).ok);   // no fader read while a plan runs
    CHECK_FALSE (r.session.scanning());
    r.run (8.0);
    REQUIRE_FALSE (r.session.applying());
    CHECK (r.cubaseDb (0) == Catch::Approx (-4.0).margin (0.06));
    CHECK (r.cubaseDb (8) == Catch::Approx (-2.5).margin (0.06));
    CHECK (r.cubaseDb (10) == Catch::Approx (-9.0).margin (0.06));
    CHECK (r.cubase.channels[10].pan == Catch::Approx (0.2).margin (1.0e-6));
    CHECK (r.cubase.channels[9].volume == Catch::Approx (0.3).margin (1.0 / 16384.0));   // left as the user set it
    CHECK (r.cubase.channels[5].volume == Catch::Approx (0.6).margin (1.0 / 16384.0));   // protected
    const auto& pr = r.session.planProgress();
    CHECK (pr.done == 3);
    CHECK (pr.skipped == 3);
    CHECK (pr.serial == 1);
    CHECK (r.cubase.bankOffset == 1);                   // back where the user was
    CHECK (r.session.slot (0).title == "Snare");
    bool sawMoved = false, sawGone = false, sawProtected = false;
    for (const auto& n : pr.notes)
    {
        sawMoved = sawMoved || (n.find ("'FX'") != std::string::npos && n.find ("moved since") != std::string::npos);
        sawGone = sawGone || n.find ("'Gone'") != std::string::npos;
        sawProtected = sawProtected || (n.find ("'Keys'") != std::string::npos && n.find ("protected") != std::string::npos);
    }
    CHECK (sawMoved);
    CHECK (sawGone);
    CHECK (sawProtected);

    // Undo: exactly where the faders and pans were, except what you changed yourself since (Lead Vox's fader).
    REQUIRE (pr.undo.size() == 3);
    r.cubase.userMovesFader (8, 0.75);   // in view: Lead Vox
    r.run (0.5);
    const auto undo = pr.undo;
    REQUIRE (r.session.applyPlan ("Undo Balance", undo, r.now).ok);
    r.run (8.0);
    CHECK (r.cubase.channels[0].volume == Catch::Approx (0.5).margin (1.0 / 16384.0));
    CHECK (r.cubase.channels[8].volume == Catch::Approx (0.75).margin (1.0 / 16384.0));   // yours: kept
    CHECK (r.cubase.channels[10].volume == Catch::Approx (0.7).margin (1.0 / 16384.0));
    CHECK (r.cubase.channels[10].pan == Catch::Approx (0.5).margin (1.0e-6));
    CHECK (r.cubase.bankOffset == 1);
    bool keptNote = false;
    for (const auto& n : r.session.planProgress().notes) keptNote = keptNote || (n.find ("'Lead Vox'") != std::string::npos && n.find ("moved since") != std::string::npos);
    CHECK (keptNote);
}

TEST_CASE ("Bridge: Undo also takes back a move that stopped at the fader top", "[bridge]")
{
    Rig r;
    r.run (0.2);
    const auto kick = r.ref ("Kick");
    REQUIRE (r.session.setVolumeDb (kick, 4.5, r.now).ok);   // like the Drum Machine at +4.5 dB
    r.run (0.6);
    const double before = r.cubase.channels[0].volume;
    BridgeSession::PlanStep st;
    st.hash = kick.hash;
    st.title = "Kick";
    st.setVolume = true;
    st.db = 6.5;                 // more than the fader can do: it stops at +6
    st.checkFrom = true;
    st.fromDb = 4.5;
    REQUIRE (r.session.applyPlan ("Auto Mix", { st }, r.now).ok);
    r.run (3.0);
    REQUIRE_FALSE (r.session.applying());
    CHECK (r.cubaseDb (0) == Catch::Approx (6.0).margin (0.06));
    const auto undo = r.session.planProgress().undo;
    REQUIRE (undo.size() == 1);
    REQUIRE (r.session.applyPlan ("Undo Auto Mix", undo, r.now).ok);
    r.run (3.0);
    CHECK (r.cubase.channels[0].volume == Catch::Approx (before).margin (1.0 / 16384.0));   // back, not "moved since"
    CHECK (r.session.planProgress().skipped == 0);
}

TEST_CASE ("Bridge: a plan for channels in view doesn't move the bank", "[bridge]")
{
    Rig r;
    r.run (0.2);
    BridgeSession::PlanStep st;
    st.hash = nameHash ("Snare");
    st.title = "Snare";
    st.setVolume = true;
    st.db = -7.0;
    REQUIRE (r.session.applyPlan ("Balance", { st }, r.now).ok);
    const int bankMoves = r.cubase.bankOffset;
    r.run (1.0);
    CHECK_FALSE (r.session.applying());
    CHECK (r.cubaseDb (1) == Catch::Approx (-7.0).margin (0.06));
    CHECK (r.cubase.bankOffset == bankMoves);
    CHECK (r.session.planProgress().done == 1);
}

TEST_CASE ("Bridge: losing Cubase stops moves; after reconnecting, writing works again", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.setVolumeDb (r.ref ("808"), -4.0, r.now).ok);
    r.cubase.alive = false;
    r.run (BridgeSession::kLostAfter + 0.5);
    CHECK (r.session.link() == BridgeSession::Link::lost);
    CHECK_FALSE (r.session.moveInProgress());
    CHECK_FALSE (r.session.setVolumeDb (r.session.refOf (4), -4.0, r.now).ok);

    r.cubase.channels[4].name = "Bass Gtr";   // the project changed while disconnected
    r.cubase.alive = true;
    r.run (1.5);
    CHECK (r.session.link() == BridgeSession::Link::connected);
    CHECK (r.session.slot (4).title == "Bass Gtr");
    CHECK (r.session.setVolumeDb (r.ref ("Bass Gtr"), -4.0, r.now).ok);
    r.run (0.3);
    CHECK (r.cubaseDb (4) == Catch::Approx (-4.0).margin (0.06));
}

TEST_CASE ("Bridge: a script reload mid-session is handled", "[bridge]")
{
    Rig r;
    r.run (0.2);
    r.cubase.activate();   // user reloads scripts in Cubase
    r.run (0.3);
    CHECK (r.session.link() == BridgeSession::Link::connected);
    CHECK (r.session.slot (1).title == "Snare");
    CHECK (r.session.setVolumeDb (r.ref ("Snare"), -2.0, r.now).ok);
}

TEST_CASE ("Bridge: a stale fader report during a move is not taken as the result", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.setVolumeDb (r.ref ("Kick"), -20.0, r.now).ok);
    // A late report of the OLD position arrives before Cubase applies the move.
    r.cubase.sendVolume (0);
    r.run (0.5);
    CHECK (r.cubaseDb (0) == Catch::Approx (-20.0).margin (0.06));
    CHECK (r.session.lastMoveResult().find ("-20.00") != std::string::npos);
}

TEST_CASE ("Bridge: log shows each move's result (manual check)", "[bridge][.print]")
{
    Rig r;
    r.run (0.2);
    for (double target : { 0.0, -6.0, -12.0, 6.0 })
    {
        r.session.setVolumeDb (r.ref ("Kick"), target, r.now);
        r.run (0.5);
    }
    r.session.restoreAll (r.now);
    r.run (0.5);
    for (auto& l : r.session.logLines()) printf ("%6.2f  %s\n", l.time, l.text.c_str());
}

TEST_CASE ("Bridge: a fast Cubase-side drag with lagging text doesn't bend the fader curve", "[bridge]")
{
    Rig r;
    r.run (0.2);
    // Settled readings first (as after a few normal moves), then fast drags whose text lags behind.
    for (double n : { 0.9, 0.78, 0.6, 0.45, 0.3, 0.15 }) { r.cubase.userMovesFader (2, n); r.run (0.4); }
    for (int k = 0; k < 4; ++k)
    {
        r.cubase.userDragsWithLaggingText (1, 0.8, 0.05, 40);
        r.run (0.02);
        r.cubase.userDragsWithLaggingText (1, 0.05, 0.8, 40);
        r.run (0.02);
    }
    r.cubase.sendVolume (1);   // the drag ends: Cubase's final, matching report
    r.run (0.5);

    // Every learned point sits on Cubase's real curve, and the scale marks stay apart.
    const auto& law = r.session.faderLaw();
    for (double n : { 0.2, 0.3, 0.5, 0.7, 0.9 })   // inside the settled readings' range
        CHECK (law.toDb (n) == Catch::Approx (FakeCubase::lawDb (n)).margin (1.5));
    double previous = 2.0;
    for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -36.0, -48.0 })
    {
        const double pos = law.toNormalised (db);
        CHECK (previous - pos > 0.02);   // marks never pile up
        previous = pos;
    }

    // And exact moves still land.
    for (double target : { -24.0, -36.0, -3.0 })
    {
        REQUIRE (r.session.setVolumeDb (r.ref ("Kick"), target, r.now).ok);
        r.run (0.6);
        CHECK (r.cubaseDb (0) == Catch::Approx (target).margin (BridgeSession::kDbTolerance + 0.005));
    }
}

TEST_CASE ("Bridge: reading every channel's fader, then back to the same bank", "[bridge]")
{
    Rig r;
    for (int i = 0; i < 11; ++i) r.cubase.channels[static_cast<std::size_t> (i)].volume = 0.3 + 0.05 * i;
    r.run (0.2);
    r.session.bank (BankAction::shiftRight, r.now);
    r.session.bank (BankAction::shiftRight, r.now);   // the user looks at channels 3 - 10
    r.run (0.3);
    REQUIRE (r.session.slot (0).title == "Hats");
    CHECK (r.session.knownFaders().size() == 10);      // only what has been in view so far

    REQUIRE (r.session.scanAllChannels (r.now).ok);
    CHECK (r.session.scanning());
    CHECK_FALSE (r.session.setVolumeDb (r.session.refOf (0), -6.0, r.now).ok);   // nothing moves meanwhile
    r.run (3.0);
    CHECK_FALSE (r.session.scanning());
    CHECK (r.session.knownFaders().size() == 11);
    for (const auto& c : r.cubase.channels)
    {
        const auto it = r.session.knownFaders().find (nameHash (c.name));
        REQUIRE (it != r.session.knownFaders().end());
        CHECK (it->second.db == Catch::Approx (FakeCubase::lawDb (c.volume)).margin (0.006));
    }
    CHECK (r.cubase.bankOffset == 2);                   // back where the user was
    CHECK (r.session.slot (0).title == "Hats");
    CHECK (r.cubase.setVolumeCount == 0);
    CHECK (r.session.setVolumeDb (r.session.refOf (0), -6.0, r.now).ok);
}


TEST_CASE ("Bridge: a channel renamed or deleted is forgotten at the next full read", "[bridge]")
{
    Rig r;
    r.run (0.2);
    REQUIRE (r.session.scanAllChannels (r.now).ok);
    r.run (3.0);
    REQUIRE (r.session.knownFaders().size() == 11);
    const auto oldInView = r.cubase.channels[1].name, oldAway = r.cubase.channels[9].name;
    r.cubase.rename (1, "Keys - Verse");     // in view
    r.cubase.rename (9, "Bass - Chorus");    // on another page
    r.cubase.channels.pop_back();            // and one deleted
    r.run (0.3);
    REQUIRE (r.session.scanAllChannels (r.now).ok);
    r.run (3.0);
    CHECK (r.session.knownFaders().size() == 10);
    CHECK (r.session.knownFaders().count (nameHash (oldInView)) == 0);
    CHECK (r.session.knownFaders().count (nameHash (oldAway)) == 0);
    CHECK (r.session.knownFaders().count (nameHash ("Keys - Verse")) == 1);
    CHECK (r.session.knownFaders().count (nameHash ("Bass - Chorus")) == 1);
}

TEST_CASE ("Bridge: muted and soloed-out channels aren't in the mix", "[bridge]")
{
    Rig r;
    r.run (0.2);
    CHECK (r.session.isAudible (nameHash ("Kick")));
    REQUIRE (r.session.setMute (r.ref ("Kick"), true, r.now).ok);
    r.run (0.2);
    CHECK_FALSE (r.session.isAudible (nameHash ("Kick")));
    CHECK (r.session.isAudible (nameHash ("Snare")));
    REQUIRE (r.session.setSolo (r.ref ("Snare"), true, r.now).ok);
    r.run (0.2);
    CHECK (r.session.isAudible (nameHash ("Snare")));
    CHECK_FALSE (r.session.isAudible (nameHash ("Hats")));    // another channel is soloed
    CHECK (r.session.isAudible (nameHash ("Never Seen")));    // unknown: assumed to play
    r.cubase.channels[1].solo = false;                        // unsoloed in Cubase
    r.session.requestDump (r.now);
    r.run (0.2);
    CHECK (r.session.isAudible (nameHash ("Hats")));
}

TEST_CASE ("Bridge: the project's channel order (from the fader scan), and showing any channel first", "[bridge]")
{
    Rig r;
    r.run (0.2);
    CHECK (r.session.channelOrder().empty());
    CHECK (r.session.viewOffset() == -1);   // not known before a scan
    CHECK_FALSE (r.session.showChannel (2, r.now).ok);
    r.session.bank (BankAction::shiftRight, r.now);
    r.run (0.2);
    REQUIRE (r.session.scanAllChannels (r.now).ok);
    r.run (3.0);
    REQUIRE (r.session.channelOrder().size() == 11);
    for (std::size_t i = 0; i < 11; ++i)
    {
        CHECK (r.session.channelOrder()[i] == nameHash (r.cubase.channels[i].name));
        CHECK (r.session.channelTitles()[i] == r.cubase.channels[i].name);
    }
    CHECK (r.session.viewOffset() == 1);    // the user's bank, back after the scan

    REQUIRE (r.session.showChannel (3, r.now).ok);
    r.run (1.5);
    CHECK (r.cubase.bankOffset == 3);
    CHECK (r.session.viewOffset() == 3);
    CHECK (r.session.slot (0).title == "Perc");
    REQUIRE (r.session.showChannel (99, r.now).ok);   // past the end: the last full bank (11 - 8)
    r.run (1.5);
    CHECK (r.cubase.bankOffset == 3);
    REQUIRE (r.session.showChannel (0, r.now).ok);
    r.run (1.5);
    CHECK (r.cubase.bankOffset == 0);
    CHECK (r.cubase.setVolumeCount == 0);   // nothing but the bank moved
}

TEST_CASE ("Bridge: a host whose last bank stops short still lands on the channel asked for", "[bridge]")
{
    Rig r;
    r.cubase.channels.clear();
    for (int i = 0; i < 30; ++i) r.cubase.channels.push_back ({ "Track " + std::to_string (i + 1) });
    r.cubase.fullLastBank = true;   // "next" from 16 shows 22 - 29, not 24 - 31
    r.run (0.2);
    REQUIRE (r.session.scanAllChannels (r.now).ok);
    r.run (5.0);
    REQUIRE (r.session.channelOrder().size() == 30);   // the overlapping last page adds only the new channels
    CHECK (r.session.channelTitles().back() == "Track 30");
    REQUIRE (r.session.showChannel (20, r.now).ok);    // reset, 3 x next (stops at 22), 4 x left (18), then 2 x right
    r.run (3.0);
    CHECK (r.cubase.bankOffset == 20);
    CHECK (r.session.viewOffset() == 20);
    CHECK (r.session.slot (0).title == "Track 21");
}
