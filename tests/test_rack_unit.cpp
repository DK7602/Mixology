// Sonicology Rack: what a Mixology Link and Mixology on the Full Mix tell the Rack (mixology/RackUnit.h).

#include "mixology/RackUnit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using namespace mixology;
namespace pr = mixology::proc;

namespace
{
    const rack::Module* find (const std::vector<rack::Module>& ms, const char* key)
    {
        for (const auto& m : ms)
            if (m.key == key) return &m;
        return nullptr;
    }
    const rack::Value* value (const rack::Module& m, const char* name)
    {
        for (const auto& v : m.values)
            if (v.name == name) return &v;
        return nullptr;
    }
    void set (rackunit::State& st, int id, float v) { st.values[static_cast<std::size_t> (id)] = v; }
}

TEST_CASE ("Rack unit: a new Link shows every module, all off, in the order the sound goes through them", "[rack]")
{
    rackunit::State st;
    const auto ms = rackunit::modules (st, true);
    const char* order[] = { "mix.assist", "mix.gate", "mix.eq", "mix.comp", "mix.multi", "mix.trans", "mix.tone", "mix.loud", "mix.harm",
                            "mix.analog", "mix.clip", "mix.stereo", "mix.final", "mix.space", "mix.limit" };
    REQUIRE (ms.size() == 15);
    for (std::size_t i = 0; i < ms.size(); ++i)
    {
        CHECK (ms[i].key == order[i]);
        CHECK (ms[i].state == rack::State::off);
        CHECK (ms[i].summary == "Off");
    }
    CHECK (ms.size() <= static_cast<std::size_t> (rack::kMaxModules));

    st.link = false;   // Mixology on the Full Mix: no Assist EQ
    const auto fm = rackunit::modules (st, true);
    CHECK (fm.size() == 14);
    CHECK (fm.front().key == "mix.gate");
}

TEST_CASE ("Rack unit: a compressor, as Mixology's page writes it, with its gain reduction", "[rack]")
{
    rackunit::State st;
    set (st, pr::compOn, 1.0f);
    set (st, pr::compRatio, 4.0f);
    set (st, pr::compThr, -18.0f);
    set (st, pr::compKnee, 2.0f);
    st.hasMeters = true;
    st.meters.comp = 3.2f;
    const auto ms = rackunit::modules (st, true);
    const auto* comp = find (ms, "mix.comp");
    REQUIRE (comp != nullptr);
    CHECK (comp->state == rack::State::on);
    CHECK (comp->summary == "Ratio 4.0:1 \xC2\xB7 Threshold \xE2\x88\x92" "18.0 dB");
    REQUIRE (value (*comp, "Knee") != nullptr);
    CHECK (value (*comp, "Knee")->text == "Hard");
    CHECK (comp->meterKind == rack::MeterKind::gr);
    CHECK (comp->meter == Catch::Approx (-3.2f));
    REQUIRE (value (*comp, "Threshold") != nullptr);
    CHECK (value (*comp, "Threshold")->norm == Catch::Approx (42.0f / 60.0f));
    CHECK (value (*comp, "Threshold")->bi == false);   // -60..0: not around a middle
    CHECK (value (*comp, "Ratio")->norm == Catch::Approx (std::log (4.0) / std::log (20.0)).margin (1e-4));   // a log knob

    // Processing off: everything off, whatever the modules say.
    set (st, pr::chainOn, 0.0f);
    CHECK (find (rackunit::modules (st, true), "mix.comp")->state == rack::State::off);
}

TEST_CASE ("Rack unit: the EQ's bands draw its curve; flat bands are left out", "[rack]")
{
    rackunit::State st;
    set (st, pr::eqOn, 1.0f);
    set (st, pr::eqType (0), static_cast<float> (pr::EqShape::lowCut));
    set (st, pr::eqFreq (0), 80.0f);
    set (st, pr::eqFreq (2), 400.0f);
    set (st, pr::eqGain (2), -3.0f);
    set (st, pr::eqFreq (4), 3500.0f);
    set (st, pr::eqGain (4), 2.0f);
    const auto ms1 = rackunit::modules (st, true);
    const auto* eq = find (ms1, "mix.eq");
    REQUIRE (eq != nullptr);
    REQUIRE (eq->bands.size() == 3);
    CHECK (eq->bands[0].type == rack::BandType::lowCut);
    CHECK (eq->bands[0].hz == Catch::Approx (80.0f));
    CHECK (eq->bands[1].db == Catch::Approx (-3.0f));
    CHECK (eq->bands[2].name == "3.5 kHz");
    CHECK (eq->summary == "Low cut 80 Hz \xC2\xB7 \xE2\x88\x92" "3.0 dB at 400 Hz \xC2\xB7 +2.0 dB at 3.5 kHz");
    REQUIRE (eq->values.size() == 3);
    CHECK (eq->values[1].name == "400 Hz");
    CHECK (eq->values[1].text == "\xE2\x88\x92" "3.0 dB");
    CHECK (eq->values[1].bi);

    set (st, pr::eqType (0), static_cast<float> (pr::EqShape::lowShelf));
    set (st, pr::eqGain (2), 0.0f);
    set (st, pr::eqGain (4), 0.0f);
    CHECK (find (rackunit::modules (st, true), "mix.eq")->summary == "Flat");
}

TEST_CASE ("Rack unit: Space names the effects that play and whom it ducks under", "[rack]")
{
    rackunit::State st;
    set (st, pr::spOn, 1.0f);
    set (st, pr::hallOn, 1.0f);
    set (st, pr::choOn, 1.0f);
    set (st, pr::hallMix, 16.0f);
    set (st, pr::duckAmount, 6.0f);
    st.duckNames = "Lead Vox";
    const auto ms2 = rackunit::modules (st, true);
    const auto* sp = find (ms2, "mix.space");
    REQUIRE (sp != nullptr);
    CHECK (sp->layers == (rack::SpaceLayer::hall | rack::SpaceLayer::chorus));
    CHECK (sp->summary == "Chorus + Hall \xC2\xB7 ducks under Lead Vox");
    REQUIRE (value (*sp, "Hall Mix") != nullptr);
    CHECK (value (*sp, "Hall Mix")->text == "16 %");
    CHECK (value (*sp, "Chorus Mix") != nullptr);
    CHECK (value (*sp, "Ducking")->text == "+6.0 dB");

    set (st, pr::spOn, 0.0f);   // switched off: nothing plays
    CHECK (find (rackunit::modules (st, true), "mix.space")->layers == 0u);
}

TEST_CASE ("Rack unit: the Assist EQ, as Clean Up and Vocal Pocket set it", "[rack]")
{
    rackunit::State st;
    st.assist.lowCutHz = 80.0f;
    st.assist.mudHz = 300.0f;
    st.assist.mudDb = -3.0f;
    st.assistByMixology = true;
    const auto ms3 = rackunit::modules (st, true);
    const auto* a = find (ms3, "mix.assist");
    REQUIRE (a != nullptr);
    CHECK (a->state == rack::State::on);
    CHECK (a->summary == "Low cut 80 Hz \xC2\xB7 Mud \xE2\x88\x92" "3.0 dB at 300 Hz");
    CHECK (a->by == "Mixology's Clean Up / Vocal Pocket");
    REQUIRE (a->bands.size() == 2);
    CHECK (a->bands[0].type == rack::BandType::lowCut);
    CHECK (a->bands[1].name == "Mud");
    CHECK (value (*a, "Pocket")->text == "Off");
}

TEST_CASE ("Rack unit: brief units carry keys, states and summaries only", "[rack]")
{
    rackunit::State st;
    set (st, pr::compOn, 1.0f);
    set (st, pr::eqOn, 1.0f);
    set (st, pr::eqGain (2), -3.0f);
    for (const auto& m : rackunit::modules (st, false))
    {
        CHECK (m.values.empty());
        CHECK (m.bands.empty());
        CHECK_FALSE (m.summary.empty());
    }
}

TEST_CASE ("Rack unit: values written the way Mixology's pages write them", "[rack]")
{
    CHECK (rackunit::valueText (pr::eqFreq (0), 3200.0f) == "3.2 kHz");
    CHECK (rackunit::valueText (pr::eqFreq (0), 1000.0f) == "1 kHz");
    CHECK (rackunit::valueText (pr::eqFreq (0), 12000.0f) == "12.0 kHz");
    CHECK (rackunit::valueText (pr::eqFreq (0), 80.4f) == "80 Hz");
    CHECK (rackunit::valueText (pr::eqGain (0), 2.0f) == "+2.0 dB");
    CHECK (rackunit::valueText (pr::eqGain (0), -0.01f) == "0.0 dB");
    CHECK (rackunit::valueText (pr::compAtt, 2.5f) == "2.5 ms");
    CHECK (rackunit::valueText (pr::compRel, 120.0f) == "120 ms");
    CHECK (rackunit::valueText (pr::hallDecay, 1.8f) == "1.8 s");
    CHECK (rackunit::valueText (pr::compOn, 1.0f) == "On");
    CHECK (rackunit::valueText (pr::spType, 4.0f) == "Hall");
    CHECK (rackunit::valueText (pr::spxShift, 12.0f) == "+12 st");
    CHECK (rackunit::valueNorm (pr::finOut, 0.0f) == Catch::Approx (0.5f));   // zeroMid: 0 dB at 12 o'clock
    CHECK (rackunit::valueNorm (pr::eqGain (0), 12.0f) == Catch::Approx (1.0f));
}

TEST_CASE ("Rack unit: a Link's modules through the Rack link, and what they do with Voxology's", "[rack]")
{
    rackunit::State st;
    set (st, pr::compOn, 1.0f);
    rack::Unit link;
    link.product = rack::Product::link;
    link.track = "Lead Vox";
    link.version = suite::packVersion (0, 26, 0);
    link.modules = rackunit::modules (st, true);

    rack::Unit vox;
    vox.product = rack::Product::voxology;
    vox.role = rack::Role::vocal;
    vox.track = "Lead Vox";
    vox.hash = suite::nameHash ("Lead Vox");
    vox.version = suite::packVersion (0, 55, 0);
    rack::Module vc;
    vc.key = "vox.comp";
    vc.state = rack::State::on;
    vox.modules = { vc };

    auto mem = std::make_unique<unsigned char[]> (rack::Link::bytes() + 64);
    void* aligned = mem.get() + (64 - reinterpret_cast<std::uintptr_t> (mem.get()) % 64) % 64;
    rack::Link shared (aligned);
    const int a = shared.claim (1, 10), b = shared.claim (2, 10);
    REQUIRE (shared.publish (a, 1, link, 10));
    REQUIRE (shared.publish (b, 2, vox, 10));
    const auto units = shared.units (10);
    REQUIRE (units.size() == 2);
    bool twoComps = false;
    for (const auto& l : rack::together (units)) twoComps |= l.text.find ("Two compressors") != std::string::npos;
    CHECK (twoComps);
}
