// The setup check: a mono group on the way, vocals with Voxology but no Mixology Link, Mixology on a mono channel.

#include "mixology/SetupCheck.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace mixology;
using namespace mixology::setup;

namespace
{
    TrackNow track (const std::string& name, float loudness, float width)
    {
        TrackNow t;
        t.name = name;
        t.loudness = loudness;
        t.width = width;
        return t;
    }

    MixNow mixOf (float loudness, float width, float correlation)
    {
        MixNow m;
        m.playing = true;
        m.loudness = loudness;
        m.width = width;
        m.correlation = correlation;
        return m;
    }

    void play (MonoWatch& w, const MixNow& m, const std::vector<TrackNow>& tracks, int moments)
    {
        for (int i = 0; i < moments; ++i) w.add (m, tracks);
    }

    bool has (const std::vector<mix::Suggestion>& list, const std::string& id)
    {
        for (const auto& s : list)
            if (s.id == id) return true;
        return false;
    }
}

TEST_CASE ("Setup: pan width follows the constant-power pan law", "[setup]")
{
    CHECK (panWidth (0.0) == Catch::Approx (0.0).margin (1e-9));
    CHECK (panWidth (100.0) == Catch::Approx (0.5));
    CHECK (panWidth (-100.0) == Catch::Approx (0.5));
    CHECK (panWidth (50.0) == Catch::Approx (0.1464).margin (0.001));
}

TEST_CASE ("Setup: stereo tracks arriving in mono are found (the mono 'Beat' group), and the fix clears it", "[setup]")
{
    // The user's song: stereo instruments (pads, drum kits) through a mono group, the vocals in the middle.
    const std::vector<TrackNow> tracks { track ("HALion Sonic 01", -18.0f, 0.22f), track ("Pop Kit 1", -16.0f, 0.15f), track ("Lead Vox 1", -15.0f, 0.0f) };
    MonoWatch w;
    play (w, mixOf (-12.0f, 0.0004f, 0.9999f), tracks, MonoWatch::kEnough - 1);
    CHECK_FALSE (w.monoFold());   // not enough heard yet
    play (w, mixOf (-12.0f, 0.0004f, 0.9999f), tracks, 1);
    CHECK (w.monoFold());
    REQUIRE (w.stereoTracks().size() == 2);
    CHECK (w.stereoTracks()[0] == "Pop Kit 1");   // the most side energy first (louder, a bit narrower)
    CHECK (w.expectedWidth() > MonoWatch::kExpectedWidth);

    // Fixed (the group made stereo): the mix has width again, and within a few seconds the issue goes.
    play (w, mixOf (-12.0f, 0.06f, 0.82f), tracks, 5);
    CHECK_FALSE (w.monoFold());
}

TEST_CASE ("Setup: no false alarm for a mono production, quiet stereo tracks, a stopped song or muted tracks", "[setup]")
{
    MonoWatch w;
    // Every track mono and in the middle: the mix is mono and that's fine.
    play (w, mixOf (-12.0f, 0.0f, 1.0f), { track ("Kick", -14.0f, 0.0f), track ("Vox", -15.0f, 0.0f) }, 200);
    CHECK_FALSE (w.monoFold());
    // A faint stereo pad far under the mix: it wouldn't show in the mix's width anyway.
    play (w, mixOf (-12.0f, 0.0f, 1.0f), { track ("Kick", -14.0f, 0.0f), track ("Pad", -45.0f, 0.4f) }, 200);
    CHECK_FALSE (w.monoFold());
    // Stopped: nothing is judged.
    MixNow stopped = mixOf (-12.0f, 0.0f, 1.0f);
    stopped.playing = false;
    play (w, stopped, { track ("Pad", -12.0f, 0.4f) }, 200);
    CHECK_FALSE (w.monoFold());
    // Muted: its Link still hears it, the mix doesn't.
    auto muted = track ("Pad", -12.0f, 0.4f);
    muted.audible = false;
    play (w, mixOf (-12.0f, 0.0f, 1.0f), { muted }, 200);
    CHECK_FALSE (w.monoFold());
    // A pad that is stereo but pulled down 40 dB on its fader.
    auto down = track ("Pad", -12.0f, 0.4f);
    down.faderDb = -40.0;
    play (w, mixOf (-12.0f, 0.0f, 1.0f), { down }, 200);
    CHECK_FALSE (w.monoFold());
}

TEST_CASE ("Setup: mono tracks panned left and right count; a channel Cubase doesn't pan doesn't", "[setup]")
{
    auto left = track ("Gtr L", -14.0f, 0.0f), right = track ("Gtr R", -14.0f, 0.0f);
    left.panKnown = right.panKnown = true;
    left.pan = -80.0;
    right.pan = 80.0;
    MonoWatch w;
    play (w, mixOf (-11.0f, 0.0f, 1.0f), { left, right }, 40);
    CHECK (w.monoFold());

    left.panWorks = right.panWorks = false;   // "No pan": Cubase never took the pan, so nothing says it should be wide
    MonoWatch w2;
    play (w2, mixOf (-11.0f, 0.0f, 1.0f), { left, right }, 40);
    CHECK_FALSE (w2.monoFold());
}

TEST_CASE ("Setup: two tracks with one name are flagged, with the fix", "[setup]")
{
    Context c;
    c.sharedNames = { "Pop Kit 1" };
    const auto one = issues (c);
    REQUIRE (one.size() == 1);
    CHECK (one[0].id == "setup-shared-name");
    CHECK (one[0].kind == "warn");
    CHECK (one[0].title == "Two tracks are called 'Pop Kit 1'");
    CHECK (one[0].text.find ("can't tell which fader and which Mixology Link") != std::string::npos);
    CHECK (one[0].steps[0].find ("Rename one of them") != std::string::npos);
    CHECK (one[0].tracks == std::vector<std::string> { "Pop Kit 1" });

    c.sharedNames = { "Pop Kit 1", "Vox" };
    CHECK (issues (c)[0].title == "2 names are used by more than one track");
}

TEST_CASE ("Setup: the issues name the likely group and the vocals without a Link", "[setup]")
{
    Context c;
    CHECK (issues (c).empty());

    c.monoFold = true;
    c.stereoTracks = { "Pop Kit 1", "HALion Sonic 01" };
    c.busCandidates = { "Beat" };
    c.vocalsWithoutLink = { "Lead Vox 1", "Lead Vox 1.5", "Lead Vox 2" };
    const auto list = issues (c);
    REQUIRE (list.size() == 2);
    CHECK (list[0].id == "setup-mono-group");
    CHECK (list[0].kind == "warn");
    CHECK (list[0].text.find ("Most likely 'Beat' is a mono group") != std::string::npos);
    CHECK (list[0].text.find ("'Pop Kit 1' and 'HALion Sonic 01'") != std::string::npos);
    CHECK (list[0].steps[0].find ("Find 'Beat' in {daw:mixer}") != std::string::npos);   // the window says how, in your DAW's words
    CHECK (list[0].tracks == std::vector<std::string> { "Beat" });
    CHECK (list[1].id == "setup-vocal-link");
    CHECK (list[1].title == "3 vocals have no Mixology Link");
    CHECK (list[1].text.find ("'Lead Vox 1', 'Lead Vox 1.5' and 'Lead Vox 2' have Voxology") != std::string::npos);

    c.vocalsWithoutLink = { "Lead Vox" };
    c.busCandidates.clear();
    const auto one = issues (c);
    CHECK (one[0].text.find ("usually a group channel") != std::string::npos);
    CHECK (one[1].title == "'Lead Vox' has no Mixology Link");

    // Mixology itself on a mono channel: that says it all (no separate mono-group issue).
    c.mixChannels = 1;
    const auto mono = issues (c);
    CHECK (has (mono, "setup-mono-mixology"));
    CHECK_FALSE (has (mono, "setup-mono-group"));
}
