#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "mixology/LoudnessMeter.h"
#include "BridgeHub.h"
#include "SpectrumTap.h"
#include "StereoTap.h"

#include <atomic>

/** Mixology main plug-in. v0.1: passes audio through untouched (bit-exact), measures the mix
    (peaks, loudness, spectrum) for the interface, and owns the Cubase bridge (shared hub). */
class MixologyAudioProcessor final : public juce::AudioProcessor
{
public:
    MixologyAudioProcessor();
    ~MixologyAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

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
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    struct Meters
    {
        std::atomic<float> peakLeft { -100.0f };    // highest sample peak since last read (dBFS)
        std::atomic<float> peakRight { -100.0f };
        std::atomic<float> momentary { -100.0f };   // LUFS
        std::atomic<float> shortTerm { -100.0f };
        std::atomic<float> integrated { -100.0f };
        std::atomic<float> truePeak { -100.0f };    // dBTP, max since reset
    };
    Meters meters;
    SpectrumTap spectrumTap;
    StereoTap stereoTap;

    /** UI state saved with the project (last page, etc.). */
    juce::String getUiState() const { const juce::ScopedLock sl (stateLock); return uiState; }
    void setUiState (const juce::String& s) { const juce::ScopedLock sl (stateLock); uiState = s; }

    juce::SharedResourcePointer<BridgeHub> bridgeHub;

private:
    template <typename Sample>
    void process (juce::AudioBuffer<Sample>&);

    mixology::LoudnessMeter loudness;
    juce::CriticalSection stateLock;
    juce::String uiState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixologyAudioProcessor)
};
