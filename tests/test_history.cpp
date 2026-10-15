#include "mixology/History.h"
#include "mixology/ProcParams.h"

#include <catch2/catch_test_macros.hpp>

using namespace mixology;
using namespace mixology::history;

namespace
{
    Change fader (std::uint32_t hash, const std::string& name, double from, double to)
    {
        Change c;
        c.what = What::fader;
        c.hash = hash;
        c.track = name;
        c.before = from;
        c.after = to;
        return c;
    }

    Change control (std::uint32_t hash, const std::string& name, int index, double from, double to)
    {
        Change c;
        c.what = What::proc;
        c.hash = hash;
        c.track = name;
        c.index = index;
        c.before = from;
        c.after = to;
        return c;
    }

    const Change* findChange (const std::vector<Change>& v, What what, std::uint32_t hash, int index = 0)
    {
        for (const auto& c : v)
            if (c.what == what && c.hash == hash && (what != What::proc || c.index == index)) return &c;
        return nullptr;
    }
}

TEST_CASE ("History: a drag is one step; the same thing later is a new one", "[history]")
{
    History h;
    const auto a = h.record ("Fader 'Kick'", "fader:1", { fader (1, "Kick", -5.0, -4.0) }, 100.0);
    CHECK (h.record ("Fader 'Kick'", "fader:1", { fader (1, "Kick", -4.0, -3.0) }, 100.5) == a);
    CHECK (h.record ("Fader 'Kick'", "fader:1", { fader (1, "Kick", -3.0, -2.0) }, 101.0) == a);
    REQUIRE (h.steps().size() == 1);
    REQUIRE (h.steps().front().changes.size() == 1);
    CHECK (h.steps().front().changes.front().before == -5.0);   // the first 'before' stays
    CHECK (h.steps().front().changes.front().after == -2.0);

    const auto b = h.record ("Fader 'Kick'", "fader:1", { fader (1, "Kick", -2.0, -1.0) }, 110.0);   // seconds later
    CHECK (b != a);
    const auto c = h.record ("Fader 'Snare'", "fader:2", { fader (2, "Snare", 0.0, -1.0) }, 110.2);  // another channel
    CHECK (c != b);
    CHECK (h.steps().size() == 3);
}

TEST_CASE ("History: dragged away and back changes nothing, so it's no step", "[history]")
{
    History h;
    h.record ("Fader 'Kick'", "fader:1", { fader (1, "Kick", -5.0, -3.0) }, 10.0);
    h.record ("Fader 'Kick'", "fader:1", { fader (1, "Kick", -3.0, -5.0) }, 11.0);
    CHECK (h.steps().empty());
    CHECK (h.record ("Mute 'Kick'", "", {}, 12.0) == 0);   // nothing at all
    CHECK (h.steps().empty());
}

TEST_CASE ("History: back to any point, forward again, and a new change ends the redo", "[history]")
{
    History h;
    const auto s1 = h.record ("Fader 'Kick'", "", { fader (1, "Kick", -5.0, -3.0) }, 1.0);
    const auto s2 = h.record ("EQ on 'Kick'", "", { control (1, "Kick", proc::eq1Gain, 0.0, 3.0) }, 2.0);
    const auto s3 = h.record ("Auto Mix", "", { fader (1, "Kick", -3.0, -6.0), fader (2, "Snare", 0.0, -2.0) }, 3.0);
    CHECK (h.current() == s3);
    CHECK (h.canUndo());
    CHECK_FALSE (h.canRedo());

    // Back to right after step 1: steps 3 and 2 undone; the kick's fader goes from where Auto Mix put it (-6) to
    // where step 1 left it (-3); one change per value.
    auto back = h.moveTo (s1);
    CHECK (h.current() == s1);
    CHECK (h.canRedo());
    REQUIRE (back.size() == 3);
    const auto* kick = findChange (back, What::fader, 1);
    REQUIRE (kick != nullptr);
    CHECK (kick->before == -6.0);
    CHECK (kick->after == -3.0);
    CHECK (findChange (back, What::fader, 2)->after == 0.0);
    CHECK (findChange (back, What::proc, 1, proc::eq1Gain)->after == 0.0);
    CHECK (h.steps()[1].undone);
    CHECK (h.steps()[2].undone);

    // Forward to step 2 only.
    auto fwd = h.moveTo (s2);
    REQUIRE (fwd.size() == 1);
    CHECK (fwd.front().what == What::proc);
    CHECK (fwd.front().after == 3.0);
    CHECK (h.current() == s2);

    // Back to the very start: everything undone.
    auto all = h.moveTo (0);
    CHECK (h.current() == 0);
    CHECK_FALSE (h.canUndo());
    REQUIRE (findChange (all, What::fader, 1) != nullptr);
    CHECK (findChange (all, What::fader, 1)->after == -5.0);
    CHECK (findChange (all, What::fader, 2) == nullptr);   // step 3 was undone already

    // One step forward, then something new: the rest of the redo is gone.
    auto r = h.redo();
    CHECK (h.current() == s1);
    REQUIRE (r.size() == 1);
    CHECK (r.front().after == -3.0);
    const auto s4 = h.record ("Pan 'Kick'", "", { [] { Change c; c.what = What::pan; c.hash = 1; c.before = 0.5; c.after = 0.3; return c; }() }, 9.0);
    CHECK (h.current() == s4);
    CHECK_FALSE (h.canRedo());
    REQUIRE (h.steps().size() == 2);
    CHECK (h.find (s2) == nullptr);

    // Undo one: the pan goes back.
    auto u = h.undo();
    REQUIRE (u.size() == 1);
    CHECK (u.front().what == What::pan);
    CHECK (u.front().after == 0.5);
    CHECK (h.current() == s1);
}

TEST_CASE ("History: a plan's faders arrive when Cubase has moved them", "[history]")
{
    History h;
    const auto id = h.record ("Auto Mix", "", { control (0, "", proc::compOn, 0.0, 1.0) }, 1.0, true);
    REQUIRE (id != 0);
    CHECK (h.busy());
    CHECK (h.moveTo (0).empty());   // nothing moves while a plan is still running
    CHECK (h.amend (id, { fader (1, "Kick", -5.0, -3.0) }));
    CHECK_FALSE (h.busy());
    CHECK (h.find (id)->changes.size() == 2);

    // A plan whose channels were all skipped, and nothing else: no step left.
    const auto empty = h.record ("Balance", "", {}, 2.0, true);
    REQUIRE (empty != 0);
    CHECK (h.amend (empty, {}));
    CHECK (h.find (empty) == nullptr);
    CHECK_FALSE (h.amend (12345, {}));
}

TEST_CASE ("History: plain words for each change and step", "[history]")
{
    CHECK (describe (fader (1, "Kick", -3.2, -5.0)) == "'Kick' fader \xe2\x88\x92" "3.2 \xe2\x86\x92 \xe2\x88\x92" "5.0 dB");
    CHECK (describe (control (1, "Lead Vox 1", proc::eq2Gain, 2.0, 3.5)) == "'Lead Vox 1' EQ 2 Gain +2.0 dB \xe2\x86\x92 +3.5 dB");
    CHECK (describe (control (1, "Lead Vox 1", proc::eqOn, 0.0, 1.0)) == "'Lead Vox 1' EQ on");
    Change pan;
    pan.what = What::pan;
    pan.track = "Hats";
    pan.before = 0.5;
    pan.after = 0.35;
    CHECK (describe (pan) == "'Hats' pan C \xe2\x86\x92 L30");
    CHECK (panText (1.0) == "R100");

    Step s;
    s.changes = { fader (1, "Kick", 0, -1), fader (2, "Snare", 0, -2), control (3, "Bass", proc::compOn, 0, 1), control (4, "Vox", proc::eqOn, 0, 1) };
    CHECK (summary (s) == "2 faders and processing on 2 tracks");
    Step one;
    one.changes = { control (3, "Bass", proc::eq1Gain, 0, 1), control (3, "Bass", proc::eq1Freq, 100, 120) };
    CHECK (summary (one).find ("'Bass' EQ 1 Gain") == 0);
    CHECK (std::string (pageOf (proc::compThr)) == "Dynamics");
    CHECK (std::string (pageOf (proc::spLevel)) == "Space");
}
