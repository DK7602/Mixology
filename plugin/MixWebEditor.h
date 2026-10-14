#pragma once

#include "PluginProcessor.h"

#include "mixology/TrackAnalyzer.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <map>
#include <optional>

/** The Mixology interface: HTML / CSS / JS (plugin/ui) embedded in the binary and shown in the
    platform web view (WebView2 on Windows, WKWebView on macOS).

    C++ -> JS: the "mixFrame" event at 30 Hz (meters, spectrum, and the bridge state when it changed).
    JS -> C++: native functions bridgeCommand(name, arg), getBridgeState(), getSettings(),
               setSetting(key, value), setUiState(json), setPage(id),
               procTarget(key), procSet(key, { id: value, ... }), setMonoListen(on) (Milestone 5 pages).

    Processing targets: "mix" (Mixology's own Full Mix processing) or "link:<slot>:<generation>" (a track's
    Mixology Link, reached through the registry). On the EQ / Dynamics / Tone / Stereo pages each frame
    carries the selected target's controls and meters ("proc"), twice a second the list of targets. */
class MixWebEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit MixWebEditor (MixologyAudioProcessor&);
    ~MixWebEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static bool isSupported();

    static constexpr int kDesignWidth = 1440;
    static constexpr int kDesignHeight = 1080;

private:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kSpectrumPoints = 120;

    void timerCallback() override;
    juce::var makeSpectrum();
    void addStereo (juce::DynamicObject&);
    juce::var makeProc();
    juce::var makeProcTargets();
    juce::var trackSpectrum (const mixology::AnalysisFrame&) const;
    static juce::WebBrowserComponent::Options makeBaseOptions();
    juce::WebBrowserComponent::Options makeEditorOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    MixologyAudioProcessor& processor;
    BridgeHub& hub;
    juce::WebBrowserComponent webView;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { static_cast<size_t> (kFftSize), juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, 2 * kFftSize> fftBuffer {};
    std::array<float, kSpectrumPoints> smoothed {};
    bool primed = false;

    std::array<float, 2048> stereoL {}, stereoR {};
    float correlationSmoothed = 0.0f;
    juce::String currentPage { "overview" };

    mixology::TrackAnalyzer mixAnalyzer;
    std::vector<float> mixL, mixR;
    int frameCount = 0;

    juce::String procKey { "mix" };
    int procListCountdown = 0;
    struct TargetInfo { double sampleRate = 48000.0; int latency = 0; int channels = 2; juce::String name; };
    std::map<juce::String, TargetInfo> targetInfo;

    std::uint64_t sentBridgeVersion = ~0ull;
    juce::int64 sentLogVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixWebEditor)
};

/** Shown when the platform can't host the web interface (e.g. no WebView2 runtime on Windows). */
class MixFallbackEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MixFallbackEditor (MixologyAudioProcessor&);
    void paint (juce::Graphics&) override;
};
