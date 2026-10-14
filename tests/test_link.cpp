#include "mixology/BridgeProtocol.h"
#include "mixology/LinkRegistry.h"
#include "mixology/TrackAnalyzer.h"
#include "mixology/SharedBlock.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

using namespace mixology;
using namespace mixology::link;

namespace
{
    std::string testName (const char* what) { return SharedBlock::perProcessName (std::string ("MixologyTest.") + what); }
}

TEST_CASE ("SharedBlock: two openings of the same name share memory, other names don't", "[link]")
{
    const auto name = testName ("share");
    SharedBlock a (name, 4096), b (name, 4096), other (testName ("other"), 4096);
    REQUIRE (a.isShared());
    REQUIRE (b.isShared());
    CHECK (a.data() != b.data());   // separate mappings, as two plug-in libraries would have
    CHECK (static_cast<unsigned char*> (a.data())[100] == 0);   // zero-filled

    std::memcpy (a.data(), "hello", 6);
    CHECK (std::strcmp (static_cast<const char*> (b.data()), "hello") == 0);
    CHECK (static_cast<const char*> (other.data())[0] == '\0');
    CHECK (name.find (".") != std::string::npos);
}

TEST_CASE ("SharedBlock: the name disappears with its last user (fresh memory next time)", "[link]")
{
    const auto name = testName ("lifetime");
    {
        SharedBlock a (name, 1024);
        static_cast<char*> (a.data())[0] = 'x';
    }
    SharedBlock again (name, 1024);
    CHECK (static_cast<const char*> (again.data())[0] == '\0');
}

TEST_CASE ("Registry: a Link in one library is seen by Mixology in another", "[link]")
{
    const auto name = testName ("registry");
    SharedBlock linkLib (name, Registry::bytes()), mainLib (name, Registry::bytes());
    Registry linkSide (linkLib.data()), mainSide (mainLib.data());

    const int slot = linkSide.claim (42, 1000);
    REQUIRE (slot >= 0);
    linkSide.setIdentity (slot, { "Lead Vox", true, 0x33AA55, true });
    linkSide.publishMeters (slot, -6.5f, -7.0f, -14.0f, -15.5f, 2, 1000);
    linkSide.publishTrim (slot, 1.5f);

    auto links = mainSide.links (1100);
    REQUIRE (links.size() == 1);
    CHECK (links[0].slot == slot);
    CHECK (links[0].instanceId == 42);
    CHECK (links[0].identity.name == "Lead Vox");
    CHECK (links[0].nameHash == bridge::nameHash ("Lead Vox"));   // same hash the Cubase bridge reports
    CHECK (links[0].identity.rgb == 0x33AA55);
    CHECK (links[0].audio);
    CHECK (links[0].peakL == -6.5f);
    CHECK (links[0].momentary == -14.0f);
    CHECK (links[0].channels == 2);
    CHECK (links[0].trimDb == 1.5f);

    linkSide.release (slot, 42);
    CHECK (mainSide.links (1100).empty());
}

TEST_CASE ("Registry: claim, release, generations, a full registry", "[link]")
{
    SharedBlock mem (testName ("claim"), Registry::bytes());
    Registry r (mem.data());

    const int a = r.claim (1, 10), b = r.claim (2, 10);
    CHECK (a != b);
    const auto genA = r.links (10)[0].generation;
    r.release (a, 999);                 // not the owner: ignored
    CHECK (r.links (10).size() == 2);
    r.release (a, 1);
    const int c = r.claim (3, 10);
    CHECK (c == a);                     // the slot is reused...
    for (auto& l : r.links (10))
        if (l.slot == c) CHECK (l.generation != genA);   // ...with a new generation

    for (int i = 0; i < kMaxLinks; ++i) r.claim (100 + static_cast<std::uint64_t> (i), 10);
    CHECK (r.claim (7, 10) == -1);
}

TEST_CASE ("Registry: silent and vanished Links", "[link]")
{
    SharedBlock mem (testName ("alive"), Registry::bytes());
    Registry r (mem.data());
    const int s = r.claim (5, 1000);
    r.publishMeters (s, -3.0f, -3.0f, -10.0f, -11.0f, 2, 1000);

    auto l = r.links (1000 + kAudioMs + 10);   // no audio for a while (stopped, bypassed)
    REQUIRE (l.size() == 1);
    CHECK_FALSE (l[0].audio);
    CHECK (l[0].peakL == link::kSilentDb);

    CHECK (r.links (1000 + kAliveMs + 10).empty());   // no heartbeat: gone
    r.heartbeat (s, 5000);
    CHECK (r.links (5100).size() == 1);
}

TEST_CASE ("Registry: Trim requests reach the right Link once", "[link]")
{
    SharedBlock mem (testName ("trim"), Registry::bytes());
    Registry r (mem.data());
    const int s = r.claim (9, 10);
    REQUIRE (r.links (10).size() == 1);
    const auto gen = r.links (10)[0].generation;
    std::uint32_t seen = mixology::link::kNoRequestSeen;
    float db = 0.0f;
    CHECK_FALSE (r.takeTrimRequest (s, seen, db));

    r.requestTrim (s, gen + 1, 3.0f);   // meant for an older owner of the slot: ignored
    CHECK_FALSE (r.takeTrimRequest (s, seen, db));
    r.requestTrim (s, gen, -2.5f);
    CHECK (r.takeTrimRequest (s, seen, db));
    CHECK (db == -2.5f);
    CHECK_FALSE (r.takeTrimRequest (s, seen, db));

    // A new Link in a reused slot never applies a request meant for the one before it.
    r.release (s, 9);
    const int s2 = r.claim (10, 20);
    REQUIRE (s2 == s);
    std::uint32_t seen2 = mixology::link::kNoRequestSeen;
    CHECK_FALSE (r.takeTrimRequest (s2, seen2, db));
}

TEST_CASE ("Registry: Assist EQ requests and readback", "[link]")
{
    SharedBlock mem (testName ("assist"), Registry::bytes());
    Registry r (mem.data());
    const int s = r.claim (9, 10);
    const auto gen = r.links (10)[0].generation;
    CHECK (r.links (10)[0].assist.neutral());

    std::uint32_t seen = mixology::link::kNoRequestSeen;
    mixology::AssistSettings got;
    CHECK_FALSE (r.takeAssistRequest (s, seen, got));
    mixology::AssistSettings want;
    want.lowCutHz = 90.0f;
    want.pocketHz = 2200.0f;
    want.pocketDb = -2.5f;
    r.requestAssist (s, gen + 1, want);   // an older owner: ignored
    CHECK_FALSE (r.takeAssistRequest (s, seen, got));
    r.requestAssist (s, gen, want);
    REQUIRE (r.takeAssistRequest (s, seen, got));
    CHECK (got == want);
    CHECK_FALSE (r.takeAssistRequest (s, seen, got));

    r.publishAssist (s, want);
    CHECK (r.links (20)[0].assist == want);
}

TEST_CASE ("Registry: Mixology tells each Link whether it is paired", "[link]")
{
    SharedBlock mem (testName ("match"), Registry::bytes());
    Registry r (mem.data());
    const int s = r.claim (1, 10);
    const auto gen = r.links (10)[0].generation;
    CHECK (r.matchSeen (s, 10) == MatchStatus::none);
    r.reportMatch (s, gen, MatchStatus::matched, 100);
    CHECK (r.matchSeen (s, 200) == MatchStatus::matched);
    CHECK (r.matchSeen (s, 100 + kAliveMs + 1) == MatchStatus::none);   // Mixology went quiet
    r.reportMatch (s, gen + 1, MatchStatus::ambiguous, 300);             // stale generation: ignored
    CHECK (r.matchSeen (s, 300) == MatchStatus::matched);
}

TEST_CASE ("Registry: analysis frames travel whole, with the transport state", "[link]")
{
    SharedBlock linkLib (testName ("analysis"), Registry::bytes()), mainLib (testName ("analysis"), Registry::bytes());
    Registry linkSide (linkLib.data()), mainSide (mainLib.data());
    const int s = linkSide.claim (3, 1000);
    CHECK_FALSE (mainSide.links (1000)[0].hasAnalysis);

    AnalysisFrame f;
    f.silent = false;
    for (int b = 0; b < kBands; ++b) { f.bandDb[static_cast<std::size_t> (b)] = -20.0f - b; f.sideShare[static_cast<std::size_t> (b)] = b / 100.0f; }
    f.loudness = -14.5f; f.rmsDb = -17.0f; f.peakDb = -3.0f; f.correlation = 0.8f; f.width = 0.2f;
    linkSide.publishAnalysis (s, f, true, 123456, 1100);

    auto l = mainSide.links (1200);
    REQUIRE (l.size() == 1);
    CHECK (l[0].hasAnalysis);
    CHECK (l[0].playing);
    CHECK (l[0].songSample == 123456);
    CHECK (l[0].analysis.bandDb[30] == -50.0f);
    CHECK (l[0].analysis.sideShare[10] == 0.1f);
    CHECK (l[0].analysis.loudness == -14.5f);
    CHECK_FALSE (l[0].analysis.silent);
    CHECK_FALSE (mainSide.links (1100 + kAudioMs + 1)[0].hasAnalysis);   // stale: not used
}

TEST_CASE ("Registry: Mixology's heartbeat", "[link]")
{
    SharedBlock mem (testName ("main"), Registry::bytes());
    Registry r (mem.data());
    CHECK_FALSE (r.mainAlive (100));
    r.mainHeartbeat (100);
    CHECK (r.mainAlive (200));
    CHECK_FALSE (r.mainAlive (100 + kAliveMs + 1));
}

TEST_CASE ("Registry: long names are cut on a UTF-8 boundary, the hash covers the full name", "[link]")
{
    SharedBlock mem (testName ("long"), Registry::bytes());
    Registry r (mem.data());
    const int s = r.claim (1, 10);
    std::string name;
    while (name.size() < 200) name += "Vocal \xC3\xA9t\xC3\xA9 ";
    r.setIdentity (s, { name, true, 0, false });
    auto l = r.links (10);
    REQUIRE (l.size() == 1);
    CHECK (l[0].identity.name.size() < kNameBytes);
    CHECK (name.compare (0, l[0].identity.name.size(), l[0].identity.name) == 0);
    CHECK ((static_cast<unsigned char> (l[0].identity.name.back()) & 0xC0) != 0xC0);   // no dangling lead byte
    CHECK (l[0].nameHash == bridge::nameHash (name));
}

TEST_CASE ("Registry: a reader never sees half of a rename", "[link]")
{
    SharedBlock mem (testName ("seqlock"), Registry::bytes());
    Registry writer (mem.data()), reader (mem.data());
    const int s = writer.claim (1, 0);
    const std::string a (100, 'A'), b (60, 'B');
    std::atomic<bool> stop { false };
    std::thread t ([&] {
        for (int i = 0; ! stop.load(); ++i)
        {
            writer.setIdentity (s, i % 2 ? Identity { a, true, 0xAAAAAA, true } : Identity { b, true, 0xBBBBBB, true });
            if (i % 4 == 0) std::this_thread::yield();   // let the reader in now and then on a busy machine
        }
    });
    int checked = 0, torn = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds (3);
    while (checked < 5000 && std::chrono::steady_clock::now() < until)
    {
        const auto id = reader.readIdentity (s);
        if (! id.hasName) continue;
        ++checked;
        const bool isA = id.name == a && id.rgb == 0xAAAAAA;
        const bool isB = id.name == b && id.rgb == 0xBBBBBB;
        if (! isA && ! isB) ++torn;
    }
    stop = true;
    t.join();
    CHECK (checked > 0);
    CHECK (torn == 0);
}

TEST_CASE ("Matching: names first, colours only when names repeat, never a guess", "[link]")
{
    auto link = [] (const char* name, std::uint32_t rgb, bool colour = true) {
        LinkInfo l;
        l.identity = { name, true, rgb, colour };
        l.nameHash = bridge::nameHash (name);
        return l;
    };
    auto chan = [] (const char* name, std::uint32_t rgb, bool colour = true) {
        return ChannelKey { true, bridge::nameHash (name), colour, rgb };
    };

    SECTION ("one channel, one Link")
    {
        auto m = matchLinks ({ chan ("Kick", 0xFF0000), chan ("Snare", 0x00FF00) }, { link ("Snare", 0x000000), link ("Kick", 0x123456) });
        CHECK (m[0].status == MatchStatus::matched);
        CHECK (m[0].link == 1);   // colour doesn't matter when the name is unique
        CHECK (m[1].link == 0);
    }
    SECTION ("no Link on a channel")
    {
        auto m = matchLinks ({ chan ("Kick", 0), ChannelKey {} }, { link ("Hats", 0) });
        CHECK (m[0].status == MatchStatus::none);
        CHECK (m[1].status == MatchStatus::none);
    }
    SECTION ("two channels called Vox, different colours")
    {
        auto m = matchLinks ({ chan ("Vox", 0xD03030), chan ("Vox", 0x3050D0) }, { link ("Vox", 0x3252D2), link ("Vox", 0xD23131) });
        CHECK (m[0].status == MatchStatus::matched);
        CHECK (m[0].link == 1);
        CHECK (m[1].link == 0);
    }
    SECTION ("two channels called Vox, same colour: no guess")
    {
        auto m = matchLinks ({ chan ("Vox", 0xD03030), chan ("Vox", 0xD03030) }, { link ("Vox", 0xD03030), link ("Vox", 0xD03030) });
        CHECK (m[0].status == MatchStatus::ambiguous);
        CHECK (m[1].status == MatchStatus::ambiguous);
    }
    SECTION ("one channel but two Links with its name (e.g. another track outside the bank)")
    {
        auto m = matchLinks ({ chan ("808", 0x00FF00) }, { link ("808", 0xFF0000), link ("808", 0x00FF00) });
        CHECK (m[0].status == MatchStatus::matched);
        CHECK (m[0].link == 1);
        auto n = matchLinks ({ chan ("808", 0x00FF00, false) }, { link ("808", 0xFF0000), link ("808", 0x00FF00) });
        CHECK (n[0].status == MatchStatus::ambiguous);
    }
}

#include "mixology/TrackMeter.h"
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>

TEST_CASE ("TrackMeter: peaks per 50 ms window and BS.1770 loudness", "[link]")
{
    const double sr = 48000.0;
    TrackMeter m;
    m.prepare (sr, 2);
    std::vector<float> l (4800 * 40), r (l.size());
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        l[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979323846 * 997.0 * static_cast<double> (i) / sr));   // -6 dBFS
        r[i] = 0.5f * l[i];                                                                                // -12 dBFS
    }
    const float* ch[] = { l.data(), r.data() };
    m.process (ch, 2, static_cast<int> (l.size()));

    CHECK (m.windowCount() == 80);
    CHECK (m.peakDb (0) == Catch::Approx (-6.02).margin (0.05));
    CHECK (m.peakDb (1) == Catch::Approx (-12.04).margin (0.05));
    // Full-scale 997 Hz sine in one channel = -3.01 LUFS. Here: -6.02 dB in L plus -12.04 dB in R.
    const double expected = 10.0 * std::log10 (std::pow (10.0, (-3.01 - 6.02) / 10.0) + std::pow (10.0, (-3.01 - 12.04) / 10.0));
    CHECK (m.momentaryLufs() == Catch::Approx (expected).margin (0.1));
    CHECK (m.shortTermLufs() == Catch::Approx (expected).margin (0.1));

    std::vector<float> silence (48000, 0.0f);
    const float* s[] = { silence.data(), silence.data() };
    m.process (s, 2, 48000);
    CHECK (m.peakDb (0) == TrackMeter::kSilentDb);
    CHECK (m.momentaryLufs() == TrackMeter::kSilentDb);
    CHECK (m.shortTermLufs() > -40.0f);   // still remembers the last 3 s
}

TEST_CASE ("TrackMeter: mono tracks", "[link]")
{
    TrackMeter m;
    m.prepare (44100.0, 1);
    std::vector<double> x (44100, 0.25);
    const double* ch[] = { x.data() };
    m.process (ch, 1, 44100);
    CHECK (m.channels() == 1);
    CHECK (m.peakDb (0) == Catch::Approx (-12.04).margin (0.05));
    CHECK (m.peakDb (1) == m.peakDb (0));   // a mono track shows the same level on both bars
}

// ---------------------------------------------------------------------------------------------
// Assist EQ (Mixology Link's low cut, mud and pocket dips)

#include "mixology/AssistEq.h"

namespace
{
    // Gain (dB) of a steady sine through the EQ, measured after it settles.
    double sineGainDb (mixology::AssistEq& eq, double hz, double rate = 48000.0)
    {
        const int n = static_cast<int> (rate);   // 1 s
        std::vector<double> l (static_cast<std::size_t> (n)), r (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
            l[static_cast<std::size_t> (i)] = r[static_cast<std::size_t> (i)] = 0.5 * std::sin (2.0 * 3.14159265358979 * hz * i / rate);
        double* ch[] = { l.data(), r.data() };
        eq.process (ch, 2, n);
        double in = 0.0, out = 0.0;
        for (int i = n / 2; i < n; ++i)
        {
            const double x = 0.5 * std::sin (2.0 * 3.14159265358979 * hz * i / rate);
            in += x * x;
            out += l[static_cast<std::size_t> (i)] * l[static_cast<std::size_t> (i)];
        }
        return 10.0 * std::log10 (out / in);
    }
}

TEST_CASE ("Assist EQ: neutral is bit for bit, each move does what it says", "[link]")
{
    using mixology::AssistEq;
    using mixology::AssistSettings;
    AssistEq eq;
    eq.prepare (48000.0, 2);
    {
        std::vector<float> l (512), r (512);
        for (std::size_t i = 0; i < l.size(); ++i) { l[i] = static_cast<float> (std::sin (0.01 * i)); r[i] = -l[i] * 0.5f; }
        const auto l0 = l, r0 = r;
        float* ch[] = { l.data(), r.data() };
        eq.process (ch, 2, 512);
        CHECK (l == l0);
        CHECK (r == r0);
        CHECK_FALSE (eq.active());
    }

    AssistSettings s;
    s.lowCutHz = 100.0f;
    s.mudHz = 300.0f;
    s.mudDb = -3.0f;
    s.pocketHz = 2500.0f;
    s.pocketDb = -4.0f;
    eq.setTarget (s);
    CHECK (sineGainDb (eq, 30.0) < -15.0);                                    // rumble gone
    CHECK (sineGainDb (eq, 100.0) == Catch::Approx (-3.0 - 0.35).margin (0.5));   // -3 dB at the cut (+ a little mud dip)
    CHECK (sineGainDb (eq, 300.0) == Catch::Approx (-3.0).margin (0.3));
    CHECK (sineGainDb (eq, 2500.0) == Catch::Approx (-4.0).margin (0.3));
    CHECK (sineGainDb (eq, 12000.0) == Catch::Approx (0.0).margin (0.4));
    for (double hz : { 30.0, 100.0, 300.0, 1000.0, 2500.0, 8000.0 })
        CHECK (AssistEq::responseDb (s, hz, 48000.0) == Catch::Approx (sineGainDb (eq, hz)).margin (0.3));

    // Back to neutral: glides out, then passes audio through untouched again.
    eq.setTarget (AssistSettings {});
    CHECK (sineGainDb (eq, 2500.0) == Catch::Approx (0.0).margin (0.05));
    CHECK_FALSE (eq.active());
}

TEST_CASE ("Assist EQ: changes glide (no jump in the output)", "[link]")
{
    mixology::AssistEq eq;
    eq.prepare (48000.0, 1);
    std::vector<double> x (4800);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = 0.5 * std::sin (2.0 * 3.14159265358979 * 2500.0 * static_cast<double> (i) / 48000.0);
    mixology::AssistSettings s;
    s.pocketDb = -12.0f;
    eq.setTarget (s);
    double* ch[] = { x.data() };
    eq.process (ch, 1, static_cast<int> (x.size()));
    double biggestStep = 0.0;
    for (std::size_t i = 1; i < 480; ++i)
        biggestStep = std::max (biggestStep, std::abs (x[i] - x[i - 1]));
    CHECK (biggestStep < 0.5 * 2.0 * 3.14159265358979 * 2500.0 / 48000.0 * 1.05);   // never steeper than the input sine
}

#include "mixology/AssistRestore.h"

TEST_CASE ("Assist EQ restore points: back to how it was, your own changes kept", "[link]")
{
    using mixology::AssistRestore;
    using mixology::AssistSettings;
    AssistRestore rp;
    AssistSettings off, cut, cutAndPocket, yours;
    cut.lowCutHz = 90.0f;
    cutAndPocket = cut;
    cutAndPocket.pocketDb = -3.0f;
    cutAndPocket.pocketHz = 2500.0f;
    yours = cutAndPocket;
    yours.pocketDb = -1.0f;

    rp.noteSet (1, off, cut);              // Clean Up
    rp.noteSet (1, cut, cutAndPocket);     // then Vocal Pocket: the first "before" stays
    REQUIRE (rp.has (1));
    auto d = rp.decide (1, cutAndPocket);
    CHECK (d.action == AssistRestore::Action::restore);
    CHECK (AssistRestore::same (d.to, off));

    // The Link's parameters round values a little: still Mixology's.
    AssistSettings rounded = cutAndPocket;
    rounded.pocketHz = 2501.0f;
    rounded.pocketDb = -3.04f;
    CHECK (rp.decide (1, rounded).action == AssistRestore::Action::restore);

    CHECK (rp.decide (1, yours).action == AssistRestore::Action::keptYours);   // you changed it since
    CHECK (rp.decide (1, off).action == AssistRestore::Action::none);          // already back
    CHECK (rp.decide (2, cut).action == AssistRestore::Action::none);          // never touched by Mixology

    // An Undo back to the start: nothing left to restore.
    rp.noteSet (3, off, cut);
    rp.noteUndo (3, off);
    CHECK_FALSE (rp.has (3));
    rp.clear();
    CHECK (rp.size() == 0);
}
