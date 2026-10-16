#include "FakeMcuHost.h"

#include "mixology/BridgeProtocol.h"
#include "mixology/BridgeSession.h"
#include "mixology/FaderLaw.h"
#include "mixology/McuBridge.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace mixology;
using namespace mixology::mcu;

namespace
{
    const std::vector<std::string> kNames {
        "Kick", "Snare", "Hats", "Lead Vocal", "Lead Vocal Double", "Background Vox 1", "Background Vox 2", "Bass",
        "Piano", "Strings", "Guitar Left", "Guitar Right",
    };

    int startFader (std::size_t i) { return (9000 + 304 * static_cast<int> (i)) & ~15; }   // multiples of 16: Cakewalk's 10 bits

    /** Mixology's session <-> the translator <-> a Mackie Control DAW, in 10 ms steps. */
    struct Rig
    {
        FakeMcuHost host;
        McuBridge bridge;
        BridgeSession session;
        double now = 0.0;

        explicit Rig (Dialect d = Dialect::logic, bool knowNames = true) : host (d), bridge (d), session (5)
        {
            for (const auto& n : kNames) host.tracks.push_back ({ n });
            for (std::size_t i = 0; i < host.tracks.size(); ++i)
            {
                host.tracks[i].fader = startFader (i);
                host.tracks[i].pan = (static_cast<int> (i % 3) - 1) * 0.25;
            }
            if (knowNames) bridge.setTrackNames (kNames);
            session.setDaw (bridge.dawName(), true);
            session.setPortsOpen (true, now);
            bridge.reset (now);
        }

        void step()
        {
            host.tick (now);
            auto fromHost = std::move (host.toSurface);
            host.toSurface.clear();
            for (auto& m : fromHost) bridge.fromDaw (m.data(), m.size(), now);
            bridge.tick (now);
            for (auto& m : bridge.takeForMixology()) session.receive (m.data(), m.size(), now);
            session.tick (now);
            for (auto& m : session.takeOutgoing()) bridge.fromMixology (m, now);
            for (auto& m : bridge.takeForDaw()) host.receive (m);
            now += 0.01;
        }

        void run (double seconds)
        {
            const double end = now + seconds;
            while (now < end) step();
        }

        int slotOf (const std::string& name) const
        {
            for (int s = 0; s < session.slotCount(); ++s)
                if (session.slot (s).present && session.slot (s).title == name) return s;
            return -1;
        }
        ChannelRef ref (const std::string& name) const { return session.refOf (slotOf (name)); }
        FakeMcuHost::Track& track (const std::string& name) { return host.tracks.at (static_cast<std::size_t> (host.indexOf (name))); }
        bool logHas (const std::string& text) const
        {
            for (const auto& l : session.logLines())
                if (l.text.find (text) != std::string::npos) return true;
            return false;
        }
    };

    double tol (const Rig& r) { return r.host.showDbOnTouch ? 0.06 : r.host.dbOffset + 0.05; }
    const char* nameOf (Dialect d) { return d == Dialect::logic ? "Logic" : "Cakewalk"; }
}

TEST_CASE ("Mackie Control: texts the DAW shows", "[mcu]")
{
    double db = 0.0, pan = 0.0;
    CHECK ((parseDisplayDb ("-6.0", db) && db == -6.0));
    CHECK ((parseDisplayDb (" -12.5dB", db) && db == -12.5));
    CHECK ((parseDisplayDb ("+2.4", db) && db == Catch::Approx (2.4)));
    CHECK ((parseDisplayDb ("-inf", db) && db == FaderLaw::kMinusInf));
    CHECK_FALSE (parseDisplayDb ("+32", db));   // a pan
    CHECK_FALSE (parseDisplayDb ("Kick", db));
    CHECK ((parsePanText (Dialect::logic, " +32", pan) && pan == Catch::Approx (32.0 / 63.0)));
    CHECK ((parsePanText (Dialect::logic, "-64", pan) && pan == -1.0));
    CHECK ((parsePanText (Dialect::logic, "0", pan) && pan == 0.0));
    CHECK_FALSE (parsePanText (Dialect::logic, "-6.0", pan));
    CHECK ((parsePanText (Dialect::cakewalk, "L30", pan) && pan == Catch::Approx (-0.3)));
    CHECK ((parsePanText (Dialect::cakewalk, "30%R", pan) && pan == Catch::Approx (0.3)));
    CHECK ((parsePanText (Dialect::cakewalk, "C", pan) && pan == 0.0));
    CHECK_FALSE (parsePanText (Dialect::cakewalk, "Kick", pan));
}

TEST_CASE ("Mackie Control: short names on the display become the full track names", "[mcu]")
{
    const std::vector<std::string> names { "Lead Vocal", "Lead Vocal Double", "Background Vox 1", "Background Vox 2", "Kick", "Kick Sub" };
    CHECK (expandName ("Kick", names) == "Kick");
    CHECK (expandName ("LeadVcl", names) == "LeadVcl");             // fits "Lead Vocal" and "Lead Vocal Double": left short
    CHECK (expandName ("LdVclDb", names) == "Lead Vocal Double");   // only one has the 'D', 'b'
    CHECK (expandName ("BckgV1", names) == "Background Vox 1");
    CHECK (expandName ("BckgV2", names) == "Background Vox 2");
    CHECK (expandName ("KickSb", names) == "Kick Sub");
    CHECK (expandName ("Bass", names) == "Bass");                   // nothing known: as shown
    CHECK (expandName ("Bckgrnd", names) == "Bckgrnd");             // fits both backgrounds: can't tell, left short

    // A whole bank: "LdVclDb" is surely "Lead Vocal Double", so "LeadVcl" next to it is "Lead Vocal".
    std::map<std::string, std::string> seen;
    const auto bank = expandNames ({ "LeadVcl", "LdVclDb", "", "Kick" }, names, seen);
    CHECK (bank == std::vector<std::string> { "Lead Vocal", "Lead Vocal Double", "", "Kick" });
    // Later, in another bank, "LeadVcl" alone: "Lead Vocal Double" showed as "LdVclDb", so it is "Lead Vocal" again.
    CHECK (expandNames ({ "LeadVcl" }, names, seen).front() == "Lead Vocal");
    // Two tracks with the same name show the same text: both are that track.
    std::map<std::string, std::string> none;
    CHECK (expandNames ({ "BckgV1", "BckgV1" }, names, none) == std::vector<std::string> { "Background Vox 1", "Background Vox 1" });
}

TEST_CASE ("Mackie Control: the fader curve, built in and corrected by the display", "[mcu]")
{
    for (auto d : { Dialect::logic, Dialect::cakewalk })
    {
        McuLaw law (d);
        CHECK (law.toDb (1.0) == Catch::Approx (6.0));
        CHECK (law.toDb (0.0) == FaderLaw::kMinusInf);
        for (double db : { 6.0, 3.0, 0.0, -6.0, -20.0, -50.0 })
            CHECK (law.toDb (law.toNormalised (db)) == Catch::Approx (db).margin (0.01));
        double prev = -2000.0;
        for (int i = 1; i <= 100; ++i)
        {
            const double v = law.toDb (i / 100.0);
            CHECK (v > prev);
            prev = v;
        }
        const double n0 = law.toNormalised (0.0);
        law.learn (n0, 0.8);   // the DAW said +0.8 dB there
        CHECK (law.toDb (n0) == Catch::Approx (0.8));
        CHECK (law.toNormalised (0.8) == Catch::Approx (n0).margin (1.0 / 16000.0));
        McuLaw copy (d);
        copy.deserialise (law.serialise());
        CHECK (copy.toDb (n0) == Catch::Approx (0.8));
    }
}

TEST_CASE ("Mackie Control: connects after the DAW answers, with full names, faders, pans and lights", "[mcu]")
{
    for (auto d : { Dialect::logic, Dialect::cakewalk })
    {
        INFO (nameOf (d));
        Rig r (d);
        r.track ("Snare").mute = true;
        r.track ("Hats").solo = true;
        r.run (2.5);
        REQUIRE (r.session.link() == BridgeSession::Link::connected);
        CHECK (r.host.online);
        REQUIRE (r.session.slotCount() == 8);
        for (int i = 0; i < 8; ++i) CHECK (r.session.slot (i).title == kNames[static_cast<std::size_t> (i)]);
        CHECK (r.session.slot (1).mute);
        CHECK (r.session.slot (2).solo);
        CHECK_FALSE (r.session.slot (0).mute);
        CHECK (r.session.slot (0).hasPan);
        CHECK (r.session.slot (0).pan == Catch::Approx (0.375).margin (1.0e-6));   // -0.25, from the display's text
        CHECK (r.session.slot (2).pan == Catch::Approx (0.625).margin (0.003));   // Logic: +16 of 63
        CHECK (r.session.slot (3).volumeDb == Catch::Approx (r.host.dbOf ("Lead Vocal")).margin (r.host.dbOffset + 0.01));
        if (d == Dialect::cakewalk) CHECK (r.host.cakewalkValues);   // Mixology asked for values under the names
        CHECK (r.logHas ("Connected to " + std::string (r.bridge.dawName())));
    }
}

TEST_CASE ("Mackie Control: nothing is answered while the DAW hasn't spoken", "[mcu]")
{
    // The DAW never answers (no surface set up in it): Mixology must say it waits, not "connected".
    McuBridge lone (Dialect::logic);
    BridgeSession s (9);
    s.setPortsOpen (true, 0.0);
    lone.reset (0.0);
    for (double t = 0.0; t < 3.0; t += 0.01)
    {
        lone.tick (t);
        for (auto& m : lone.takeForMixology()) s.receive (m.data(), m.size(), t);
        s.tick (t);
        for (auto& m : s.takeOutgoing()) lone.fromMixology (m, t);
        (void) lone.takeForDaw();
    }
    CHECK (s.link() == BridgeSession::Link::waiting);
    CHECK_FALSE (lone.dawSeen());
}

TEST_CASE ("Mackie Control: exact dB moves land, the display's readings teach the curve", "[mcu]")
{
    for (auto d : { Dialect::logic, Dialect::cakewalk })
        for (bool shows : { true, false })
        {
            if (d == Dialect::cakewalk && shows) continue;   // Cakewalk shows nothing when a fader moves
            INFO (nameOf (d) << (shows ? ", shows dB on touch" : ", no dB on the display"));
            Rig r (d);
            r.host.showDbOnTouch = shows;
            r.run (2.5);
            REQUIRE (r.session.link() == BridgeSession::Link::connected);
            for (const char* name : { "Kick", "Lead Vocal", "Bass" })
                for (double target : { 0.0, -6.0, -12.0, 4.0, -30.0 })
                {
                    INFO (name << " " << target);
                    REQUIRE (r.session.setVolumeDb (r.ref (name), target, r.now).ok);
                    r.run (2.0);
                    CHECK_FALSE (r.session.moveInProgress());
                    const double step = d == Dialect::cakewalk ? 0.09 : 0.0;   // Cakewalk's 10-bit steps (about 0.08 dB near 0)
                    CHECK (r.host.dbOf (name) == Catch::Approx (target).margin (tol (r) + step));
                    CHECK (r.slotOf (name) >= 0);   // never renamed "Volume" (Logic's help tag)
                }
            REQUIRE (r.session.setVolumeDb (r.ref ("Kick"), FaderLaw::kMinusInf, r.now).ok);
            r.run (0.5);
            CHECK (r.track ("Kick").fader == 0);
            CHECK (r.track ("Snare").faderMoves == 0);   // nothing else moved
            if (shows) CHECK (r.bridge.law().learned() > 3);
            CHECK_FALSE (r.logHas ("renamed"));
        }
}

TEST_CASE ("Mackie Control: pan, mute and solo", "[mcu]")
{
    for (auto d : { Dialect::logic, Dialect::cakewalk })
    {
        INFO (nameOf (d));
        Rig r (d);
        r.host.showDbOnTouch = false;
        r.run (2.5);
        REQUIRE (r.session.setPan (r.ref ("Hats"), 0.25, r.now).ok);   // halfway left
        REQUIRE (r.session.setPan (r.ref ("Bass"), 1.0, r.now).ok);    // hard right
        REQUIRE (r.session.setPan (r.ref ("Snare"), 0.6, r.now).ok);   // a little right
        r.run (0.3);
        // Cakewalk shows the knob's value where the name goes: a fader move meanwhile still lands, nothing renamed.
        REQUIRE (r.session.setVolumeDb (r.ref ("Hats"), -8.0, r.now).ok);
        r.run (3.0);
        CHECK (r.host.panUnits ("Hats") == (d == Dialect::logic ? -32 : -50));
        CHECK (r.host.panUnits ("Bass") == (d == Dialect::logic ? 63 : 100));
        CHECK (std::abs (r.host.panUnits ("Snare") - (d == Dialect::logic ? 13 : 20)) <= 1);
        CHECK (r.session.slot (r.slotOf ("Hats")).pan == Catch::Approx (0.25).margin (0.01));
        CHECK (r.host.dbOf ("Hats") == Catch::Approx (-8.0).margin (r.host.dbOffset + 0.1));
        CHECK_FALSE (r.logHas ("renamed"));
        CHECK_FALSE (r.logHas ("Refused"));

        REQUIRE (r.session.setMute (r.ref ("Kick"), true, r.now).ok);
        REQUIRE (r.session.setSolo (r.ref ("Lead Vocal"), true, r.now).ok);
        r.run (0.5);
        CHECK (r.track ("Kick").mute);
        CHECK (r.track ("Lead Vocal").solo);
        REQUIRE (r.session.setMute (r.ref ("Kick"), true, r.now).ok);   // already muted: no second press (it toggles)
        r.run (0.5);
        CHECK (r.track ("Kick").mute);
        REQUIRE (r.session.setMute (r.ref ("Kick"), false, r.now).ok);
        r.run (0.5);
        CHECK_FALSE (r.track ("Kick").mute);
        CHECK_FALSE (r.session.slot (0).mute);
    }
}

TEST_CASE ("Mackie Control: Logic ignores buttons right after the handshake: they wait", "[mcu]")
{
    Rig r (Dialect::logic);
    r.run (0.3);
    REQUIRE (r.session.link() == BridgeSession::Link::connected);
    REQUIRE (r.host.now < r.host.buttonsFrom);
    REQUIRE (r.session.setMute (r.ref ("Kick"), true, r.now).ok);
    REQUIRE (r.session.bank (bridge::BankAction::next, r.now).ok);
    r.run (2.5);
    CHECK (r.host.ignoredButtons == 0);
    CHECK (r.track ("Kick").mute);
    CHECK (r.host.offset == 4);   // 12 tracks: the last bank is full
    CHECK (r.session.slot (0).title == "Lead Vocal Double");
}

TEST_CASE ("Mackie Control: the bank, and a move that races it is refused", "[mcu]")
{
    Rig r;
    r.host.showDbOnTouch = false;
    r.host.fullLastBank = false;
    r.run (2.5);
    REQUIRE (r.session.bank (bridge::BankAction::next, r.now).ok);
    r.run (1.0);
    CHECK (r.session.slot (0).title == "Piano");
    CHECK (r.session.slot (3).title == "Guitar Right");
    CHECK_FALSE (r.session.slot (4).present);
    REQUIRE (r.session.bank (bridge::BankAction::shiftLeft, r.now).ok);
    r.run (1.0);
    CHECK (r.session.slot (0).title == "Bass");
    REQUIRE (r.session.bank (bridge::BankAction::reset, r.now).ok);
    r.run (1.0);
    CHECK (r.session.slot (0).title == "Kick");

    // A move for "Kick" (slot 1) sent right after a bank press, before the DAW redrew the names: refused.
    bridge::Message bank;
    bank.cmd = bridge::Cmd::bank;
    bank.flag = static_cast<int> (bridge::BankAction::next);
    r.bridge.fromMixology (bridge::encode (bank), r.now);
    bridge::Message move;
    move.cmd = bridge::Cmd::setVolume;
    move.slot = 0;
    move.token = 77;
    move.hash = bridge::nameHash ("Kick");
    move.value = 0.2;
    r.bridge.fromMixology (bridge::encode (move), r.now);
    bool refused = false;
    for (auto& m : r.bridge.takeForMixology())
        if (const auto a = bridge::decode (m); a && a->cmd == bridge::Cmd::ack && a->token == 77)
            refused = a->flag == static_cast<int> (bridge::AckStatus::nameMismatch);
    CHECK (refused);
    r.run (1.0);
    CHECK (r.track ("Piano").faderMoves == 0);
    CHECK (r.track ("Kick").faderMoves == 0);
}

TEST_CASE ("Mackie Control: a plan across every channel, its undo, and Restore all", "[mcu]")
{
    for (int variant = 0; variant < 4; ++variant)
    {
        INFO ("variant " << variant);
        Rig r (variant == 3 ? Dialect::cakewalk : Dialect::logic);
        r.host.fullLastBank = variant != 1;
        r.host.dbOnlyForLastTouched = variant == 2;   // the others keep showing an old value: never learned
        r.run (2.5);
        REQUIRE (r.session.scanAllChannels (r.now).ok);
        r.run (6.0);
        REQUIRE (r.session.knownFaders().size() == kNames.size());

        std::vector<BridgeSession::PlanStep> steps;
        double db = -3.0;
        for (const auto& n : kNames)
        {
            BridgeSession::PlanStep st;
            st.hash = bridge::nameHash (n);
            st.title = n;
            st.setVolume = true;
            st.db = db;
            db -= 1.5;
            steps.push_back (st);
        }
        REQUIRE (r.session.applyPlan ("Balance", steps, r.now).ok);
        r.run (20.0);
        REQUIRE_FALSE (r.session.applying());
        db = -3.0;
        for (const auto& n : kNames)
        {
            INFO (n);
            CHECK (r.host.dbOf (n) == Catch::Approx (db).margin (variant >= 2 ? r.host.dbOffset + 0.1 : 0.06));
            db -= 1.5;
        }
        CHECK (r.host.offset == 0);   // back where it was

        const auto undo = r.session.planProgress().undo;
        REQUIRE (undo.size() == kNames.size());
        REQUIRE (r.session.applyPlan ("Undo Balance", undo, r.now).ok);
        r.run (20.0);
        for (std::size_t i = 0; i < kNames.size(); ++i)
            CHECK (std::abs (r.host.tracks[i].fader - startFader (i)) <= 2);
    }
}

TEST_CASE ("Mackie Control: moves made in the DAW, and renames, are seen", "[mcu]")
{
    Rig r;
    r.run (2.5);
    r.host.userMovesFader ("Snare", 5000);
    r.run (0.5);
    CHECK (r.session.slot (1).volume == Catch::Approx (5000.0 / 16383.0).margin (1.0e-6));
    r.host.rename ("Snare", "Snare Top");
    r.run (0.5);
    CHECK (r.session.slot (1).title == FakeMcuHost::abbreviate ("Snare Top", 6));   // no Link for it: as the display shows it
    r.bridge.setTrackNames ({ "Snare Top" });
    r.run (0.5);
    CHECK (r.session.slot (1).title == "Snare Top");
}

TEST_CASE ("Mackie Control: plan log (manual check)", "[.mcuprint]")
{
    Rig r;
    r.host.dbOnlyForLastTouched = true;
    r.run (2.5);
    REQUIRE (r.session.scanAllChannels (r.now).ok);
    r.run (6.0);
    std::vector<BridgeSession::PlanStep> steps;
    double db = -3.0;
    for (const auto& n : kNames)
    {
        BridgeSession::PlanStep st;
        st.hash = bridge::nameHash (n);
        st.title = n;
        st.setVolume = true;
        st.db = db;
        db -= 1.5;
        steps.push_back (st);
    }
    REQUIRE (r.session.applyPlan ("Balance", steps, r.now).ok);
    int lastOffset = -1;
    for (int k = 0; k < 2000; ++k)
    {
        r.step();
        if (r.host.offset != lastOffset) { lastOffset = r.host.offset; std::printf ("t=%.2f host offset %d\n", r.now, lastOffset); }
    }
    for (const auto& l : r.session.logLines()) std::printf ("%.2f %s\n", l.time, l.text.c_str());
    for (const auto& n : kNames) std::printf ("%s %.2f\n", n.c_str(), r.host.dbOf (n));
}

TEST_CASE ("Mackie Control: move log (manual check)", "[.mcuprint]")
{
    Rig r;
    r.host.showDbOnTouch = false;
    r.run (2.5);
    for (double target : { 0.0, -6.0, -12.0, 4.0 })
    {
        REQUIRE (r.session.setVolumeDb (r.ref ("Kick"), target, r.now).ok);
        r.run (1.5);
        std::printf ("target %.1f -> %.3f in progress %d\n", target, r.host.dbOf ("Kick"), r.session.moveInProgress() ? 1 : 0);
    }
    for (const auto& l : r.session.logLines()) std::printf ("%.2f %s\n", l.time, l.text.c_str());
}
