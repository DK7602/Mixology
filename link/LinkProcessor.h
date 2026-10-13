#pragma once

#include "mixology/AssistEq.h"
#include "mixology/AudioRing.h"
#include "mixology/LinkRegistry.h"
#include "mixology/TrackAnalyzer.h"
#include "mixology/TrackMeter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/** Mixology Link: one on each track. A Trim, meters, the track's identity, and (Milestone 3) analysis.

    - Measures its track (peaks per 50 ms, momentary and short-term loudness) and publishes it to the
      process-wide Link registry, where Mixology on the Stereo Out reads it.
    - Learns the track's name and colour from the host (VST3 channel context) and publishes those too, so
      Mixology can pair it with the matching MixConsole channel it sees through the Cubase bridge.
    - About five times a second a background thread (one per library, shared by every Link) analyses the
      latest audio (TrackAnalyzer: 1/3-octave bands, loudness, width) for Analyze Mix.
    - Trim: a clean gain (-24..+24 dB). Mixology may ask for a new Trim; the request is applied on the
      message thread like a user move, so the host sees it (automation, undo) and the project saves it.
    - Assist EQ (Milestone 4): low cut, mud dip and vocal-pocket dip, set by Mixology's Clean Up and Vocal
      Pocket the same way. At its neutral settings audio passes through bit for bit.
    No latency. */
class MixologyLinkProcessor final : public juce::AudioProcessor,
                                    private juce::Timer,
                                    private juce::TimeSliceClient
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
    mixology::AssistSettings assistSettings() const;
    /** Sets all five Assist EQ parameters like a user move (one gesture each). */
    void setAssist (const mixology::AssistSettings&);

private:
    template <typename Sample>
    void process (juce::AudioBuffer<Sample>&);
    void timerCallback() override;
    int useTimeSlice() override;   // analysis thread
    void publishIdentity();

    struct AnalysisThread : juce::TimeSliceThread
    {
        AnalysisThread() : juce::TimeSliceThread ("Mixology Link analysis") { startThread (juce::Thread::Priority::low); }
        ~AnalysisThread() override { stopThread (2000); }
    };
    juce::SharedResourcePointer<AnalysisThread> analysisThread;
    mixology::AudioRing ring;
    mixology::TrackAnalyzer analyzer;                 // analysis thread only
    std::vector<float> snapL, snapR;                  // analysis thread only
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<bool> transportPlaying { false };
    std::atomic<std::int64_t> songPosition { 0 };
    std::atomic<std::uint64_t> lastAudioMs { 0 };

    std::atomic<float>* trimParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* mudHzParam = nullptr;
    std::atomic<float>* mudDbParam = nullptr;
    std::atomic<float>* pocketHzParam = nullptr;
    std::atomic<float>* pocketDbParam = nullptr;
    mixology::AssistEq assist;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> trimGain;
    mixology::TrackMeter meter;
    std::uint32_t publishedWindows = 0;

    mixology::link::Registry& registry;
    std::uint64_t instanceId = 0;
    int slot = -1;
    std::uint32_t trimRequestSeen = mixology::link::kNoRequestSeen;
    std::uint32_t assistRequestSeen = mixology::link::kNoRequestSeen;

    Identity trackIdentity;
    juce::String trimNote;

    std::array<std::atomic<float>, 2> editorPeak { -100.0f, -100.0f };
    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixologyLinkProcessor)
};
