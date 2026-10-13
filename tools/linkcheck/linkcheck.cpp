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

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>

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
        std::printf ("usage: MixologyLinkCheck <path to Mixology Link.vst3>\n");
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
    const auto* k = byName (links, "Kick");
    const auto* b = byName (links, "808 Bass");
    check (k != nullptr && b != nullptr, "names arrived through the VST3 channel context");
    if (k == nullptr || b == nullptr) return 1;

    check (k->nameHash == mixology::bridge::nameHash ("Kick"), "name hash matches the Cubase bridge's");
    check (k->identity.hasColour && k->identity.rgb == 0xd23b3bu, "colour arrived: #" + juce::String::toHexString (static_cast<int> (k->identity.rgb)));
    check (k->audio && b->audio, "audio seen");
    check (std::abs (k->peakL + 12.04f) < 0.3f, "Kick level -12 dBFS after its Trim: " + juce::String (k->peakL, 2));
    check (std::abs (b->peakR + 6.02f) < 0.3f, "808 level -6 dBFS: " + juce::String (b->peakR, 2));
    check (b->momentary < -5.0f && b->momentary > -9.0f, "808 loudness about -6 LUFS: " + juce::String (b->momentary, 2));
    check (std::abs (k->trimDb + 6.0f) < 0.05f, "Kick reports its Trim: " + juce::String (k->trimDb, 2));

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

    std::printf ("%s (%d failed)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
