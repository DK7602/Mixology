// Presets: Mixology's own (every control real, every value in range) and yours (saved, read back, kept tolerant).

#include "mixology/Presets.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace mixology;
using namespace mixology::presets;

TEST_CASE ("Presets: Mixology's own name real controls, in range, each once", "[presets]")
{
    const auto& list = factory();
    REQUIRE (list.size() >= 15);
    std::set<std::string> names;
    for (const auto& p : list)
    {
        INFO (p.name);
        CHECK (p.factory);
        CHECK (names.insert (p.name).second);   // unique names
        CHECK_FALSE (p.about.empty());
        CHECK (p.name.size() <= 60);
        std::set<std::string> ids;
        for (const auto& [id, value] : p.values)
        {
            INFO (id);
            const int i = proc::indexOf (id);
            REQUIRE (i >= 0);                                   // a real control
            CHECK (ids.insert (id).second);                     // named once
            CHECK (proc::sanitise (i, value) == Catch::Approx (value).margin (1e-4));   // in range, on its step
        }
        // Every preset switches on what it sets (a module's settings without its switch would do nothing).
        const auto v = valuesOf (p);
        CHECK (v[proc::chainOn] == 1.0f);
        bool anyOn = false;
        for (int m = 0; m < proc::kModules; ++m) anyOn = anyOn || proc::isOn (v, static_cast<proc::Module> (m));
        CHECK (anyOn);
    }
    // One for each kind of track Auto Mix knows, the FX returns and the Full Mix.
    for (const auto* role : { "vox", "adlibs", "kick", "bass", "snare", "hats", "perc", "keys", "synth", "guitar" })
        CHECK (std::any_of (list.begin(), list.end(), [role] (const Preset& p) { return p.kind == Kind::track && p.suits (role) && ! p.roles.empty(); }));
    CHECK (std::count_if (list.begin(), list.end(), [] (const Preset& p) { return p.kind == Kind::fx; }) >= 2);
    CHECK (std::count_if (list.begin(), list.end(), [] (const Preset& p) { return p.kind == Kind::mix; }) >= 2);
}

TEST_CASE ("Presets: loading sets every control; a track keeps its routing, an FX preset makes an FX return", "[presets]")
{
    const auto* vocal = &factory().front();
    REQUIRE (vocal->name == "Lead Vocal: Sit in the Beat");
    auto current = proc::defaults();
    current[proc::compOn] = 1.0f;     // what the track had: a compressor, its Space as an FX return
    current[proc::spRoute] = 1.0f;
    const auto v = forTarget (*vocal, current);
    CHECK (v[proc::eqOn] == 1.0f);
    CHECK (v[proc::compOn] == 0.0f);  // not in the preset: back to its default (off)
    CHECK (v[proc::spRoute] == 1.0f); // the track's own routing stays
    CHECK (v[proc::eq1Type] == 3.0f); // a low cut
    CHECK (v[proc::eq1Freq] == Catch::Approx (100.0f));

    const Preset* plate = nullptr;
    for (const auto& p : factory()) if (p.name == "FX Return: Vocal Plate") plate = &p;
    REQUIRE (plate != nullptr);
    const auto fx = forTarget (*plate, proc::defaults());
    CHECK (fx[proc::spRoute] == 1.0f);
    CHECK (fx[proc::spOn] == 1.0f);
    CHECK (fx[proc::spLevel] == Catch::Approx (-3.0f));
}

TEST_CASE ("Presets: yours are captured, written and read back; the file is read tolerantly", "[presets]")
{
    auto v = proc::defaults();
    v[proc::eqOn] = 1.0f;
    v[proc::eq4Gain] = 2.5f;
    v[proc::compOn] = 1.0f;
    v[proc::compRatio] = 4.5f;
    const auto mine = capture ("  My Lead\nVox  ", Kind::track, "vox", v);
    CHECK (mine.name == "My Lead Vox");
    CHECK_FALSE (mine.factory);
    CHECK (mine.values.size() == 4);   // only what differs from the defaults

    const auto text = serialise ({ mine, factory().front() });   // Mixology's own are never written
    const auto back = parse (text);
    REQUIRE (back.size() == 1);
    CHECK (back[0].name == "My Lead Vox");
    CHECK (back[0].kind == Kind::track);
    CHECK (back[0].roles == "vox");
    CHECK (valuesOf (back[0]) == v);

    // A newer version's control, junk lines and a nameless block: skipped, the rest kept.
    const auto odd = parse ("Mixology presets 1\n[preset]\nname=Mix A\nkind=mix\nfin_on=1\nfin_glue=25\nnew_thing=3\nnonsense\neq_on=abc\n"
                            "[preset]\nkind=track\neq_on=1\n");
    REQUIRE (odd.size() == 1);
    CHECK (odd[0].kind == Kind::mix);
    CHECK (odd[0].values.size() == 2);
    CHECK (valuesOf (odd[0])[proc::finGlue] == Catch::Approx (25.0f));
}

TEST_CASE ("Presets: names are one tidy line of at most 60 characters", "[presets]")
{
    CHECK (cleanName ("   ") == "");
    CHECK (cleanName ("a\tb") == "a b");
    const auto longName = cleanName (std::string (59, 'x') + "\xC3\xA9" + "yyy");
    CHECK (longName.size() == 59);   // "é" would be cut in half: left out
}
