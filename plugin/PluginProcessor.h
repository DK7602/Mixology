#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "mixology/AudioRing.h"
#include "mixology/LoudnessMeter.h"
#include "mixology/ProcChain.h"
#include "mixology/TrackAnalyzer.h"
#include "BridgeHub.h"
#include "ProcHost.h"
#include "SpectrumTap.h"
#include "StereoTap.h"
#include "suite/SuiteLink.h"

#include <atomic>

/** Mixology main plug-in: measures the mix (peaks, loudness, spectrum) for the interface, owns the Cubase
    bridge (shared hub), and (Milestone 5) runs the "Full Mix" processing: the same chain as each Mixology
    Link, all modules off by default (audio untouched, bit for bit). Analyze Mix hears the mix before that
    processing (what the tracks add up to); the meters show it after. */
class MixologyAudioProcessor final : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    MixologyAudioProcessor();
    ~MixologyAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    /** The host stops or jumps (AU Reset, VST3 processing off): reverb and delay tails stop, at the start of the
        next block (instant, so any thread may call this). */
    void reset() override { resetWanted.store (true); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    /** Bypassed by the host: the mix passes, late by the latency the host was told (so it stays in time). */
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return mixology::proc::Chain::kTailSeconds; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }   // Steinberg's validator rejects an unnamed program
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    void updateTrackProperties (const TrackProperties&) override;

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
    mixology::AudioRing analysisRing;              // for Analyze Mix (analysed 5 times a second on the message thread)
    std::atomic<bool> transportPlaying { false };
    std::atomic<juce::int64> lastSeenMs { 0 };     // the window was on screen (the editor says so)

    /** The song's tempo and position from the host (the Space pictures move with the beat). */
    struct Beat
    {
        std::atomic<double> bpm { 0.0 };           // 0 = the host didn't say
        std::atomic<double> ppq { 0.0 };           // quarter notes since the song's start, at the last block
        std::atomic<double> barStartPpq { 0.0 };
        std::atomic<int> numerator { 4 };
        std::atomic<std::int64_t> whenMs { 0 };    // when that block was processed (juce::Time::currentTimeMillis)
    };
    Beat beat;

    /** UI state saved with the project (last page, etc.). */
    juce::String getUiState() const { const juce::ScopedLock sl (stateLock); return uiState; }
    void setUiState (const juce::String& s) { const juce::ScopedLock sl (stateLock); uiState = s; }

    juce::SharedResourcePointer<BridgeHub> bridgeHub;

    // ---- the song's style, shared with Voxology and Masterology (suite link; message thread) ----------
    /** You picked a style in the header. */
    void pickStyle (const std::string& key, const std::string& name, bool byYou = true);
    /** The style this project uses ("" = none yet: the interface's own default) and a counter that goes up when
        another plug-in's choice replaced it (the interface then follows). */
    std::string songStyleKey() const { const juce::ScopedLock sl (stateLock); return songStyle.style().key; }
    std::uint32_t songStyleVersion() const { return styleVersion.load(); }

    // ---- the DAW you work in, shared the same way: found from the host, or picked in any of the three --------
    void pickDaw (const std::string& key);
    suite::StyleInfo dawNow() const { const juce::ScopedLock sl (stateLock); return daw.style(); }
    std::string dawDetected() const { return detectedDaw; }

    // ---- Full Mix processing (message thread) ----------------------------------------------------
    mixology::proc::Values procValues() const { return procRaw.read(); }
    void setProc (int index, float value) { mixology::proc::host::set (parameters, index, value); }
    /** The processing meters since the last call (largest values; message thread). */
    mixology::proc::Meters takeProcMeters();
    /** Mono Check: you hear the full mix in mono. Never saved; the window switches it off when you leave
        the Stereo page or close Mixology, so a mixdown can't come out mono by mistake. */
    std::atomic<bool> monoListen { false };

private:
    void timerCallback() override;   // the Limiter's latency, Space's engine, the hand-off to Masterology
    /** M6: what Masterology should know about this mix (the style, peaks, loudness, tone), a few times a second. */
    void publishHandoff();
    int handoffCountdown = 0;
    suite::SongStyle songStyle { suite::StyleInfo::From::mixology };   // under stateLock
    std::atomic<std::uint32_t> styleVersion { 0 };
    const std::string detectedDaw = suite::dawFromHostPath (juce::PluginHostType::getHostPath().toStdString());
    suite::SharedChoice daw { suite::StyleInfo::From::mixology, suite::Choice::daw };   // under stateLock
    const std::uint64_t instanceId = (static_cast<std::uint64_t> (juce::Random::getSystemRandom().nextInt64()) | 1u);
    void syncStyle();

    // The project this Mixology belongs to (the hub keeps each open project's state apart), and what it last
    // loaded or saved for it.
    std::atomic<std::uint64_t> projectId { 0 };
    BridgeHub::ProjectState projectCopy;   // under stateLock
    juce::String trackName;                // under stateLock: the channel it sits on, when the DAW says
    std::atomic<juce::int64> lastAudioMs { 0 };
    std::atomic<bool> prepared { false };
    bool wasPlaying = false;               // audio thread

    // Analyze Mix hears the mix through the Full Mix's Mixology (the hub's), five times a second.
    mixology::TrackAnalyzer mixAnalyzer;
    std::vector<float> mixL, mixR;
    int analysisCountdown = 0;
    void feedAnalysis();

    template <typename Sample>
    void process (juce::AudioBuffer<Sample>&);
    template <typename Sample>
    void bypass (juce::AudioBuffer<Sample>&);

    mixology::LoudnessMeter loudness;
    mixology::proc::host::Raw procRaw;
    mixology::proc::Chain chain;
    std::atomic<int> latencyTold { 0 };   // what setLatencySamples last told the host (the bypass keeps to it)
    std::atomic<bool> resetWanted { false };
    std::array<std::atomic<float>, mixology::proc::kMeterValues> procMeters {};
    std::atomic<std::uint32_t> procRunning { 0 };
    juce::CriticalSection stateLock;
    juce::String uiState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixologyAudioProcessor)
};
