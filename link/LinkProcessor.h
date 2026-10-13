#pragma once

#include "mixology/LinkRegistry.h"
#include "mixology/TrackMeter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/** Mixology Link: one on each track. v1 (Milestone 2): a Trim, meters, and the track's identity.

    - Measures its track (peaks per 50 ms, momentary and short-term loudness) and publishes it to the
      process-wide Link registry, where Mixology on the Stereo Out reads it.
    - Learns the track's name and colour from the host (VST3 channel context) and publishes those too, so
      Mixology can pair it with the matching MixConsole channel it sees through the Cubase bridge.
    - Trim: a clean gain (-24..+24 dB). Mixology may ask for a new Trim; the request is applied on the
      message thread like a user move, so the host sees it (automation, undo) and the project saves it.
    Audio is otherwise untouched (no latency). */
class MixologyLinkProcessor final : public juce::AudioProcessor,
                                    private juce::Timer
{
public:
    MixologyLinkProcessor();
    ~MixologyLinkProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void updateTrackProperties (const TrackProperties&) override;

    juce::AudioProcessorValueTreeState parameters;

    // ---- for the editor (message thread) -------------------------------------------------------
    struct Identity
    {
        juce::String name;
        bool hasName = false;
        juce::Colour colour;
        bool hasColour = false;
    };
    Identity identity() const { return trackIdentity; }
    bool mixologyOpen() const;
    mixology::link::MatchStatus pairing() const;
    bool registryShared() const { return mixology::link::processRegistryShared(); }
    int registrySlot() const { return slot; }
    float takePeak (int channel) { return editorPeak[channel & 1].exchange (-100.0f); }
    float momentaryLufs() const { return momentary.load(); }
    float shortTermLufs() const { return shortTerm.load(); }
    float trimDb() const { return trimParam->load(); }
    void setTrimDb (float db);
    juce::String lastTrimRequest() const { return trimNote; }

private:
    template <typename Sample>
    void process (juce::AudioBuffer<Sample>&);
    void timerCallback() override;
    void publishIdentity();

    std::atomic<float>* trimParam = nullptr;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> trimGain;
    mixology::TrackMeter meter;
    std::uint32_t publishedWindows = 0;

    mixology::link::Registry& registry;
    std::uint64_t instanceId = 0;
    int slot = -1;
    std::uint32_t trimRequestSeen = 0;

    Identity trackIdentity;
    juce::String trimNote;

    std::array<std::atomic<float>, 2> editorPeak { -100.0f, -100.0f };
    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixologyLinkProcessor)
};
