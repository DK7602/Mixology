// Mixology Link end-to-end check across libraries.
//
// Mixology and Mixology Link are separate plug-in libraries, so they can only meet through the named
// shared block (mixology::SharedBlock). This program plays both parts the way Cubase runs them:
//   - it hosts two real Mixology Link instances (the .vst3 under test) on "Kick" and "808 Bass",
//     tells them their track name and colour through the VST3 channel context, and plays audio through them;
//   - it reads the registry through its OWN copy of the engine (a different module, like Mixology does)
//     and checks names, hashes, levels, Trim, pairing, and that a Trim request comes back as a real
//     parameter change.
// Exit code 0 = every check passed.

#include "mixology/BridgeProtocol.h"
#include "mixology/LinkRegistry.h"
#include "suite/SuiteLink.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    }

    juce::AudioProcessorParameter* findParameter (juce::AudioProcessor& p, const juce::String& name)
    {
        for (auto* param : p.getParameters())
            if (param->getName (64) == name) return param;
        return nullptr;
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    // Plays `seconds` of a 997 Hz sine at `gain` through the plug-in; returns the output's peak.
    float play (juce::AudioPluginInstance& p, double sr, int block, double seconds, float gain)
    {
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        float peak = 0.0f;
        const int blocks = static_cast<int> (seconds * sr / block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const auto x = gain * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (b * block + i) / sr));
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, x);
            }
            p.processBlock (buffer, midi);
            if (b > blocks / 2) peak = std::max (peak, buffer.getMagnitude (0, 0, block));
        }
        return peak;
    }

    const suite::VocalInfo* vocalNamed (const std::vector<suite::VocalInfo>& vocals, const std::string& name)
    {
        for (auto& v : vocals)
            if (v.trackName == name) return &v;
        return nullptr;
    }

    // Voxology on "Lead Vox": the suite link tells what it does there (its compressor, its reverb), and forgets it
    // when it is removed.
    void checkVoxology (juce::VST3PluginFormat& format, const juce::String& path, double sr, int block)
    {
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, path);
        check (! types.isEmpty(), "found Voxology in " + path);
        if (types.isEmpty()) return;
        juce::String error;
        auto vox = format.createInstanceFromDescription (*types[0], sr, block, error);
        check (vox != nullptr, "Voxology created " + error);
        if (vox == nullptr) return;
        vox->prepareToPlay (sr, block);
        juce::AudioProcessor::TrackProperties track;
        track.name = juce::String ("Lead Vox");
        vox->updateTrackProperties (track);
        pump (800);
        auto vocals = suite::Link::process().vocals (suite::nowMs());
        const auto* v = vocalNamed (vocals, "Lead Vox");
        check (v != nullptr && v->nameHash == mixology::bridge::nameHash ("Lead Vox") && (v->flags & suite::VocalInfo::processing) != 0,
               "Voxology tells Mixology the vocal it sits on ('Lead Vox')");
        check (v != nullptr && (v->flags & (suite::VocalInfo::compressor | suite::VocalInfo::reverb)) == 0, "... neutral at first: no compressor, no reverb");

        auto set = [&] (const char* name, float normalised) {
            if (auto* p = findParameter (*vox, name)) { p->setValueNotifyingHost (normalised); return true; }
            check (false, juce::String ("Voxology parameter '") + name + "' found");
            return false;
        };
        set ("Compressor Ratio", 0.5f);
        set ("Reverb Mix", 0.2f);
        pump (800);
        vocals = suite::Link::process().vocals (suite::nowMs());
        v = vocalNamed (vocals, "Lead Vox");
        check (v != nullptr && (v->flags & suite::VocalInfo::compressor) != 0 && (v->flags & suite::VocalInfo::reverb) != 0
                   && std::abs (v->reverbMix - 0.2f) < 0.01f,
               "... its compressor and its reverb (mix " + juce::String (v != nullptr ? v->reverbMix * 100.0f : -1.0f, 0) + " %) once they are on");
        vox->releaseResources();
        vox.reset();
        pump (100);
        check (vocalNamed (suite::Link::process().vocals (suite::nowMs()), "Lead Vox") == nullptr, "... and nothing once Voxology is removed");
    }

    // Masterology after Mixology: its goal reaches Mixology, Mixology's hand-off reaches it.
    void checkMasterology (juce::VST3PluginFormat& format, const juce::String& path, double sr, int block)
    {
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, path);
        check (! types.isEmpty(), "found Masterology in " + path);
        if (types.isEmpty()) return;
        juce::String error;
        auto master = format.createInstanceFromDescription (*types[0], sr, block, error);
        check (master != nullptr, "Masterology created " + error);
        if (master == nullptr) return;
        master->prepareToPlay (sr, block);
        pump (800);
        auto goals = suite::Link::process().masters (suite::nowMs());
        check (goals.size() == 1 && std::abs (goals[0].targetLufs + 9.0f) < 0.01f && goals[0].goalName == "Loud release" && goals[0].version != 0,
               "Masterology tells Mixology its goal (" + juce::String (goals.empty() ? std::string ("none") : goals[0].goalName) + ", "
                   + juce::String (goals.empty() ? 0.0f : goals[0].targetLufs, 1) + " LUFS, ceiling " + juce::String (goals.empty() ? 0.0f : goals[0].ceilingDbtp, 1) + " dBTP)");
        if (auto* target = findParameter (*master, "Auto-Master Target")) target->setValueNotifyingHost (0.0f);   // Stream -14
        pump (800);
        goals = suite::Link::process().masters (suite::nowMs());
        check (goals.size() == 1 && std::abs (goals[0].targetLufs + 14.0f) < 0.01f && goals[0].goalName == "Streaming", "... and follows its target (Streaming, -14 LUFS)");
        master->releaseResources();
        master.reset();
        pump (100);
        check (suite::Link::process().masters (suite::nowMs()).empty(), "... and nothing once Masterology is removed");
    }

    const mixology::link::LinkInfo* byName (const std::vector<mixology::link::LinkInfo>& links, const std::string& name)
    {
        for (auto& l : links)
            if (l.identity.name == name) return &l;
        return nullptr;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::printf ("usage: MixologyLinkCheck <path to Mixology Link.vst3> [path to Mixology.vst3] [Voxology.vst3] [Masterology.vst3]\n");
        return 2;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, juce::String::fromUTF8 (argv[1]));
    check (types.size() == 1, "found Mixology Link in " + juce::String (argv[1]));
    if (types.isEmpty()) return 1;

    const double sr = 48000.0;
    const int block = 512;
    juce::String error;
    auto kick = format.createInstanceFromDescription (*types[0], sr, block, error);
    auto bass = format.createInstanceFromDescription (*types[0], sr, block, error);
    check (kick != nullptr && bass != nullptr, "two instances created " + error);
    if (kick == nullptr || bass == nullptr) return 1;

    for (auto* p : { kick.get(), bass.get() })
    {
        p->setPlayConfigDetails (2, 2, sr, block);
        p->prepareToPlay (sr, block);
    }

    // What Cubase does when the plug-in sits on a track (VST3 channel context: name + colour).
    juce::AudioProcessor::TrackProperties kp, bp;
    kp.name = juce::String ("Kick");
    kp.colourARGB = 0xffd23b3bu;
    bp.name = juce::String ("808 Bass");
    bp.colourARGB = 0xffd6b23au;
    kick->updateTrackProperties (kp);
    bass->updateTrackProperties (bp);

    auto* kickTrim = findParameter (*kick, "Trim");
    auto* bassTrim = findParameter (*bass, "Trim");
    check (kickTrim != nullptr && bassTrim != nullptr, "Trim parameter exposed to the host");
    if (kickTrim == nullptr || bassTrim == nullptr) return 1;
    kickTrim->setValueNotifyingHost (( -6.0f + 24.0f) / 48.0f);   // -6 dB

    const float kickOut = play (*kick, sr, block, 1.0, 0.5f);   // -6 dBFS in, -6 dB trim -> -12 dBFS out
    const float bassOut = play (*bass, sr, block, 1.0, 0.5f);
    pump (400);   // the Links' message-thread timers: heartbeat, identity, Trim

    check (std::abs (juce::Decibels::gainToDecibels (kickOut) + 12.04f) < 0.2f, "Trim -6 dB applied to the audio: " + juce::String (juce::Decibels::gainToDecibels (kickOut), 2) + " dBFS");
    check (std::abs (juce::Decibels::gainToDecibels (bassOut) + 6.02f) < 0.2f, "no Trim, audio untouched: " + juce::String (juce::Decibels::gainToDecibels (bassOut), 2) + " dBFS");

    // ---- Mixology's side: this program's own copy of the engine -------------------------------
    using namespace mixology::link;
    auto& registry = processRegistry();
    check (processRegistryShared(), "shared memory available");
    auto links = registry.links (nowMs());
    check (links.size() == 2, "both Links visible from another library (" + juce::String (static_cast<int> (links.size())) + ")");
    const LinkInfo* k = byName (links, "Kick");
    const LinkInfo* b = byName (links, "808 Bass");
    check (k != nullptr && b != nullptr, "names arrived through the VST3 channel context");
    if (k == nullptr || b == nullptr) return 1;

    check (k->nameHash == mixology::bridge::nameHash ("Kick"), "name hash matches the Cubase bridge's");
    check (k->identity.hasColour && k->identity.rgb == 0xd23b3bu, "colour arrived: #" + juce::String::toHexString (static_cast<int> (k->identity.rgb)));
    check (k->audio && b->audio, "audio seen");
    check (std::abs (k->peakL + 12.04f) < 0.3f, "Kick level -12 dBFS after its Trim: " + juce::String (k->peakL, 2));
    check (std::abs (b->peakR + 6.02f) < 0.3f, "808 level -6 dBFS: " + juce::String (b->peakR, 2));
    check (b->momentary < -5.0f && b->momentary > -9.0f, "808 loudness about -6 LUFS: " + juce::String (b->momentary, 2));
    check (std::abs (k->trimDb + 6.0f) < 0.05f, "Kick reports its Trim: " + juce::String (k->trimDb, 2));

    // Milestone 3: each Link analyses its track on a background thread while audio flows (about 5 x a second).
    {
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        bool analysed = false;
        for (int blk = 0; blk < 200 && ! analysed; ++blk)   // up to ~2 s of "real time"
        {
            for (auto* p : { kick.get(), bass.get() })
            {
                for (int i = 0; i < block; ++i)
                {
                    const auto x = 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (blk * block + i) / sr));
                    buffer.setSample (0, i, x);
                    buffer.setSample (1, i, x);
                }
                p->processBlock (buffer, midi);
            }
            juce::Thread::sleep (10);
            if (blk > 40)
            {
                const auto now = registry.links (nowMs());
                analysed = ! now.empty() && std::all_of (now.begin(), now.end(), [] (const LinkInfo& l) { return l.hasAnalysis; });
            }
        }
        check (analysed, "analysis frames arrive from both Links");
        links = registry.links (nowMs());
        k = byName (links, "Kick");
        b = byName (links, "808 Bass");
        if (k == nullptr || b == nullptr) return 1;
        check (std::abs (k->analysis.bandDb[17] + 12.0f) < 0.8f, "Kick's 1 kHz band reads its level after the Trim: " + juce::String (k->analysis.bandDb[17], 2) + " dB");
        check (k->analysis.bandDb[10] < -40.0f, "nothing in the 200 Hz band: " + juce::String (k->analysis.bandDb[10], 1));
        check (k->analysis.correlation > 0.99f && k->analysis.width < 0.01f, "a centred sound reads mono");
        check (std::abs (b->analysis.loudness + 6.0f) < 1.0f, "808 loudness about -6: " + juce::String (b->analysis.loudness, 2));
    }

    std::vector<ChannelKey> bank { { true, mixology::bridge::nameHash ("808 Bass"), true, 0xd6b23au },
                                   { true, mixology::bridge::nameHash ("Snare"), false, 0 },
                                   { true, mixology::bridge::nameHash ("Kick"), true, 0xd23b3bu } };
    const auto matches = matchLinks (bank, links);
    check (matches[0].status == MatchStatus::matched && &links[static_cast<std::size_t> (matches[0].link)] == b, "808 Bass channel paired with its Link");
    check (matches[1].status == MatchStatus::none, "Snare (no Link) not paired");
    check (matches[2].status == MatchStatus::matched && &links[static_cast<std::size_t> (matches[2].link)] == k, "Kick channel paired with its Link");

    // Mixology asks the 808's Link for +3 dB; the Link applies it as a parameter change (host sees it).
    registry.requestTrim (b->slot, b->generation, 3.0f);
    pump (400);
    const float bassTrimDb = bassTrim->getValue() * 48.0f - 24.0f;
    check (std::abs (bassTrimDb - 3.0f) < 0.06f, "Trim request applied as a parameter change: " + juce::String (bassTrimDb, 2) + " dB");
    links = registry.links (nowMs());
    if (const auto* b2 = byName (links, "808 Bass"))
        check (std::abs (b2->trimDb - 3.0f) < 0.06f, "and reported back: " + juce::String (b2->trimDb, 2));

    // Milestone 4: Mixology sets the Kick Link's Assist EQ (a pocket dip right on the test tone), then clears it.
    {
        check (findParameter (*kick, "Low Cut") != nullptr && findParameter (*kick, "Pocket Dip") != nullptr, "Assist EQ parameters exposed to the host");
        links = registry.links (nowMs());
        const auto* kl = byName (links, "Kick");
        check (kl != nullptr, "Kick Link listed");
        if (kl == nullptr) return 1;
        const int kSlot = kl->slot;
        const auto kGen = kl->generation;
        const float before = play (*kick, sr, block, 0.5, 0.5f);
        mixology::AssistSettings a;
        a.pocketHz = 997.0f;
        a.pocketDb = -6.0f;
        a.lowCutHz = 60.0f;
        registry.requestAssist (kSlot, kGen, a);
        pump (400);
        const float dipped = play (*kick, sr, block, 0.5, 0.5f);
        const float change = juce::Decibels::gainToDecibels (dipped / before);
        check (std::abs (change + 6.0f) < 0.4f, "Assist EQ request applied: the 997 Hz tone is " + juce::String (change, 2) + " dB");
        links = registry.links (nowMs());
        if (const auto* k2 = byName (links, "Kick"))
            check (std::abs (k2->assist.pocketDb + 6.0f) < 0.06f && std::abs (k2->assist.lowCutHz - 60.0f) < 1.0f, "and reported back");
        registry.requestAssist (kSlot, kGen, mixology::AssistSettings {});
        pump (400);
        play (*kick, sr, block, 0.2, 0.5f);   // glides out
        const float cleared = play (*kick, sr, block, 0.5, 0.5f);
        check (std::abs (cleared - before) < 1.0e-6f, "cleared: audio untouched again");
    }

    // Milestone 5: Mixology switches on the Kick Link's EQ (+6 dB right on the test tone) through the registry,
    // the Link applies it as host parameters and reports it back; the Limiter's lookahead reaches the host.
    {
        using namespace mixology::proc;
        check (findParameter (*kick, "EQ 3 Gain") != nullptr && findParameter (*kick, "Limiter Ceiling") != nullptr,
               "processing parameters exposed to the host (" + juce::String (static_cast<int> (kick->getParameters().size())) + " in all)");
        links = registry.links (nowMs());
        const auto* kl = byName (links, "Kick");
        check (kl != nullptr && kl->hasProcessing, "Kick Link publishes its processing");
        if (kl == nullptr) return 1;
        const int kSlot = kl->slot;
        const auto kGen = kl->generation;
        Values v {};
        bool allOff = registry.readProc (kSlot, kGen, v);
        for (int m = 0; m < kModules; ++m)
            allOff = allOff && v[static_cast<std::size_t> (onParam (static_cast<Module> (m)))] < 0.5f;
        check (allOff && v[chainOn] > 0.5f, "every module starts off");
        const float before = play (*kick, sr, block, 0.5, 0.5f);

        registry.requestProc (kSlot, kGen, eqOn, 1.0f);
        registry.requestProc (kSlot, kGen, eq3Freq, 997.0f);
        registry.requestProc (kSlot, kGen, eq3Gain, 6.0f);
        pump (300);
        const float boosted = play (*kick, sr, block, 0.5, 0.5f);
        const float change = juce::Decibels::gainToDecibels (boosted / before);
        check (std::abs (change - 6.0f) < 0.3f, "EQ request applied: the 997 Hz tone is +" + juce::String (change, 2) + " dB");
        check (registry.readProc (kSlot, kGen, v) && v[eqOn] > 0.5f && std::abs (v[eq3Gain] - 6.0f) < 0.01f, "and reported back");
        mixology::proc::Meters m;
        check (registry.readProcMeters (kSlot, kGen, m, nowMs()) && (m.running & (1u << static_cast<int> (Module::eq))) != 0, "the EQ reports it is running");

        check (kick->getLatencySamples() == 0, "no latency without the Limiter");
        registry.requestProc (kSlot, kGen, limOn, 1.0f);
        pump (300);
        check (kick->getLatencySamples() > 0, "Limiter on: the host is told its lookahead (" + juce::String (kick->getLatencySamples()) + " samples)");

        registry.requestProc (kSlot, kGen, limOn, 0.0f);
        registry.requestProc (kSlot, kGen, eqOn, 0.0f);
        pump (300);
        play (*kick, sr, block, 0.3, 0.5f);   // fades out
        const float off = play (*kick, sr, block, 0.5, 0.5f);
        check (std::memcmp (&off, &before, sizeof off) == 0, "modules off again: audio untouched, bit for bit");
        check (kick->getLatencySamples() == 0, "and no latency");
    }

    // Milestone 5, round 2: Space. A reverb on the Kick (the host hears of its tail, the tail rings on after
    // the sound stops); the 808's Link as an FX channel ducking under the Kick; Freeze tells the host "endless".
    {
        using namespace mixology::proc;
        links = registry.links (nowMs());
        const auto* kl = byName (links, "Kick");
        const auto* bl = byName (links, "808 Bass");
        if (kl == nullptr || bl == nullptr) return 1;
        const int kSlot = kl->slot, bSlot = bl->slot;
        const auto kGen = kl->generation, bGen = bl->generation;

        check (kick->getTailLengthSeconds() <= 0.0, "no tail while Space is off");
        registry.requestProc (kSlot, kGen, spOn, 1.0f);
        registry.requestProc (kSlot, kGen, spType, static_cast<float> (SpaceType::reverb));
        registry.requestProc (kSlot, kGen, revMix, 60.0f);
        pump (300);
        check (kick->getTailLengthSeconds() >= 29.0, "Space on: the host is told about a long tail (" + juce::String (kick->getTailLengthSeconds(), 1) + " s)");
        play (*kick, sr, block, 0.5, 0.5f);
        juce::AudioBuffer<float> silence (2, block);
        juce::MidiBuffer midi;
        float tail = 0.0f;
        for (int b2 = 0; b2 < static_cast<int> (0.3 * sr / block); ++b2)
        {
            silence.clear();
            kick->processBlock (silence, midi);
            if (b2 > 10) tail = std::max (tail, silence.getMagnitude (0, 0, block));
        }
        check (tail > 1.0e-3f, "the reverb rings on after the sound stops (" + juce::String (juce::Decibels::gainToDecibels (tail), 1) + " dBFS)");
        play (*kick, sr, block, 0.3, 0.5f);
        mixology::proc::Meters km;
        const bool kmOk = registry.readProcMeters (kSlot, kGen, km, nowMs());
        check (kmOk && km.spaceWet > -60.0f, "the Space meter shows the effect: " + juce::String (km.spaceWet, 1) + " dBFS");

        // The 808's Link becomes an FX channel reverb that ducks 24 dB under the Kick.
        registry.requestProc (bSlot, bGen, spOn, 1.0f);
        registry.requestProc (bSlot, bGen, spRoute, 1.0f);
        registry.requestProc (bSlot, bGen, revMix, 100.0f);
        registry.requestProc (bSlot, bGen, duckAmount, 24.0f);
        registry.requestProc (kSlot, kGen, spOn, 0.0f);
        DuckSources under;
        under.count = 1;
        under.hashes[0] = mixology::bridge::nameHash ("Kick");
        registry.requestDuck (bSlot, bGen, under);
        pump (500);
        links = registry.links (nowMs());
        const auto* bl2 = byName (links, "808 Bass");
        check (bl2 != nullptr && bl2->duck == under, "the 808's Link ducks under the Kick (and says so)");
        // Kick (a tone) and the FX channel (noise: a reverb's level then stays steady) play side by side, block by
        // block, like a host; returns the FX channel's level (dB) over the second half.
        juce::Random noise (7);
        auto pair = [&] (float kickGain, double seconds) {
            juce::AudioBuffer<float> kb (2, block), bb (2, block);
            double sum = 0.0;
            int count = 0;
            const int blocks = static_cast<int> (seconds * sr / block);
            for (int b2 = 0; b2 < blocks; ++b2)
            {
                for (int i = 0; i < block; ++i)
                {
                    const auto x = static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (b2 * block + i) / sr));
                    const auto n = 0.3f * (noise.nextFloat() * 2.0f - 1.0f);
                    for (int c = 0; c < 2; ++c) { kb.setSample (c, i, kickGain * x); bb.setSample (c, i, n); }
                }
                kick->processBlock (kb, midi);
                bass->processBlock (bb, midi);
                if (b2 > blocks / 2)
                    for (int i = 0; i < block; ++i) { sum += bb.getSample (0, i) * bb.getSample (0, i); ++count; }
            }
            return 10.0 * std::log10 (sum / std::max (1, count) + 1.0e-20);
        };
        const double alone = pair (0.0f, 1.5);
        const double ducked = pair (0.5f, 1.5);
        check (alone > -60.0, "the FX reverb plays (" + juce::String (alone, 1) + " dB)");
        check (alone - ducked > 12.0, "and ducks while the Kick plays: " + juce::String (alone - ducked, 1) + " dB down");
        const double back = pair (0.0f, 1.5);
        check (std::abs (back - alone) < 1.5, "and comes back when the Kick stops (" + juce::String (back - alone, 1) + " dB)");

        // The duck list is saved with the project.
        juce::MemoryBlock bassState;
        bass->getStateInformation (bassState);
        registry.requestDuck (bSlot, bGen, DuckSources {});
        pump (300);
        links = registry.links (nowMs());
        check (byName (links, "808 Bass") != nullptr && byName (links, "808 Bass")->duck.count == 0, "duck list cleared");
        bass->setStateInformation (bassState.getData(), static_cast<int> (bassState.getSize()));
        pump (300);
        links = registry.links (nowMs());
        check (byName (links, "808 Bass") != nullptr && byName (links, "808 Bass")->duck == under, "and comes back with the project");

        // Freeze: an endless tail. Space off: none.
        registry.requestProc (bSlot, bGen, spType, static_cast<float> (SpaceType::special));
        registry.requestProc (bSlot, bGen, spxMode, static_cast<float> (SpecialMode::freeze));
        pump (300);
        check (std::isinf (bass->getTailLengthSeconds()), "Freeze: the host is told the tail is endless");
        registry.requestProc (bSlot, bGen, spOn, 0.0f);
        pump (300);
        check (bass->getTailLengthSeconds() <= 0.0 && kick->getTailLengthSeconds() <= 0.0, "Space off: no tail");
    }

    // A renamed track is seen under its new name.
    bp.name = juce::String ("808 Sub");
    bass->updateTrackProperties (bp);
    pump (200);
    links = registry.links (nowMs());
    check (byName (links, "808 Sub") != nullptr && byName (links, "808 Bass") == nullptr, "rename seen");

    kick->releaseResources();
    bass->releaseResources();
    kick.reset();
    bass.reset();
    pump (100);
    check (registry.links (nowMs()).empty(), "removing the plug-ins frees their slots");

    // Mixology itself (when its path is given): the Full Mix processing changes the sound through its host
    // parameters, the way the EQ / Dynamics / Tone / Stereo pages set them.
    if (argc > 2)
    {
        juce::OwnedArray<juce::PluginDescription> mainTypes;
        format.findAllTypesForFile (mainTypes, juce::String::fromUTF8 (argv[2]));
        check (mainTypes.size() == 1, "found Mixology in " + juce::String (argv[2]));
        if (mainTypes.isEmpty()) return 1;
        auto mix = format.createInstanceFromDescription (*mainTypes[0], sr, block, error);
        check (mix != nullptr, "Mixology created " + error);
        if (mix == nullptr) return 1;
        mix->setPlayConfigDetails (2, 2, sr, block);
        mix->prepareToPlay (sr, block);

        auto set = [&] (const char* name, float normalised) {
            if (auto* p = findParameter (*mix, name)) { p->setValueNotifyingHost (normalised); return true; }
            check (false, juce::String ("parameter '") + name + "' exposed");
            return false;
        };
        // Plays a sine (left, and right = side * left) and returns the output's left and side levels in dB.
        auto measure = [&] (double hz, float side) {
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            double sumL = 0.0, sumS = 0.0;
            int count = 0;
            const int blocks = static_cast<int> (1.0 * sr / block);
            for (int b2 = 0; b2 < blocks; ++b2)
            {
                for (int i = 0; i < block; ++i)
                {
                    const auto x = 0.1f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hz * (b2 * block + i) / sr));
                    buffer.setSample (0, i, x);
                    buffer.setSample (1, i, x * side);
                }
                mix->processBlock (buffer, midi);
                if (b2 > blocks / 2)
                    for (int i = 0; i < block; ++i)
                    {
                        const double l = buffer.getSample (0, i), r = buffer.getSample (1, i);
                        sumL += l * l;
                        sumS += 0.25 * (l - r) * (l - r);
                        ++count;
                    }
            }
            return std::pair { 10.0 * std::log10 (sumL / count + 1.0e-20), 10.0 * std::log10 (sumS / count + 1.0e-20) };
        };

        const auto plain = measure (60.0, 1.0f);
        set ("Tone Balance", 1.0f);
        set ("Warmth", 1.0f);   // +6 dB
        measure (60.0, 1.0f);    // glides in
        const auto warm = measure (60.0, 1.0f);
        check (std::abs (warm.first - plain.first - 5.8) < 0.6, "Full Mix Tone Balance: Warmth +6 lifts 60 Hz by " + juce::String (warm.first - plain.first, 2) + " dB");
        set ("Tone Balance", 0.0f);
        measure (60.0, 1.0f);
        const auto off = measure (60.0, 1.0f);
        check (std::abs (off.first - plain.first) < 0.01, "switched off again: " + juce::String (off.first - plain.first, 3) + " dB");

        const auto wideBefore = measure (1000.0, -0.5f);   // a stereo sound (left and right differ)
        set ("Stereo", 1.0f);
        set ("Width", 1.0f);    // 200 %
        measure (1000.0, -0.5f);
        const auto wide = measure (1000.0, -0.5f);
        check (std::abs (wide.second - wideBefore.second - 6.02) < 0.3, "Full Mix Width 200 %: side +" + juce::String (wide.second - wideBefore.second, 2) + " dB");
        const auto monoBefore = measure (1000.0, 1.0f);
        check (monoBefore.second < -100.0, "a mono sound has nothing to widen (side stays silent)");
        set ("Stereo", 0.0f);

        // Space on the Full Mix: the host hears of the tail; switching off takes it back.
        check (mix->getTailLengthSeconds() <= 0.0, "Mixology: no tail while Space is off");
        set ("Space", 1.0f);
        pump (300);
        measure (1000.0, 1.0f);
        check (mix->getTailLengthSeconds() >= 29.0, "Mixology: Space on, long tail (" + juce::String (mix->getTailLengthSeconds(), 1) + " s)");
        set ("Space", 0.0f);
        pump (300);
        check (mix->getTailLengthSeconds() <= 0.0, "Mixology: Space off, no tail");

        // The settings are saved with the project.
        set ("Tone Balance", 1.0f);
        juce::MemoryBlock state;
        mix->getStateInformation (state);
        set ("Tone Balance", 0.0f);
        mix->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        auto* tb = findParameter (*mix, "Tone Balance");
        check (tb != nullptr && tb->getValue() > 0.5f, "Full Mix settings come back with the project");

        // M6: the hand-off to Masterology reaches another library (this program) through the suite's shared memory.
        measure (1000.0, 1.0f);
        pump (800);   // Mixology publishes a few times a second
        suite::MixInfo handoff;
        const bool seen = suite::Link::process().readMix (handoff, suite::nowMs());
        check (seen && suite::Link::processShared(), "Mixology's hand-off reaches another plug-in (suite link)");
        check (seen && handoff.integratedLufs > -40.0f && handoff.peakDbtp > -40.0f && handoff.version != 0,
               "... with the mix's loudness and peaks (" + juce::String (handoff.integratedLufs, 1) + " LUFS, " + juce::String (handoff.peakDbtp, 1) + " dBTP)");

        // Optional (local): the real Voxology and Masterology next to Mixology, all three in this one process.
        if (argc > 3) checkVoxology (format, juce::String::fromUTF8 (argv[3]), sr, block);
        if (argc > 4) checkMasterology (format, juce::String::fromUTF8 (argv[4]), sr, block);
        mix->releaseResources();
    }

    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
