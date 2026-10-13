#include "PluginProcessor.h"
#include "MixWebEditor.h"

#include <cmath>

namespace
{
    void storeMax (std::atomic<float>& a, float v) noexcept
    {
        auto cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }

    float toDb (double gain) noexcept
    {
        return gain > 1.0e-5 ? static_cast<float> (20.0 * std::log10 (gain)) : -100.0f;
    }
}

MixologyAudioProcessor::MixologyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MixologyParams", {})
{
}

bool MixologyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MixologyAudioProcessor::prepareToPlay (double sampleRate, int)
{
    loudness.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
}

template <typename Sample>
void MixologyAudioProcessor::process (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (channels <= 0 || n <= 0) return;

    // Audio passes through untouched. Everything below only measures.
    const auto* const* data = buffer.getArrayOfReadPointers();
    loudness.process (data, channels, n);
    spectrumTap.push (data, channels, n);
    stereoTap.push (data, channels, n);

    for (int c = 0; c < juce::jmin (2, channels); ++c)
    {
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::abs (static_cast<double> (data[c][i])));
        storeMax (c == 0 ? meters.peakLeft : meters.peakRight, toDb (peak));
    }
    if (channels == 1)
        meters.peakRight.store (meters.peakLeft.load (std::memory_order_relaxed), std::memory_order_relaxed);

    const auto lufs = [] (double v) { return std::isfinite (v) ? static_cast<float> (v) : -100.0f; };
    meters.momentary.store (lufs (loudness.momentaryLufs()), std::memory_order_relaxed);
    meters.shortTerm.store (lufs (loudness.shortTermLufs()), std::memory_order_relaxed);
    meters.integrated.store (lufs (loudness.integratedLufs()), std::memory_order_relaxed);
    meters.truePeak.store (lufs (loudness.truePeakDbtp()), std::memory_order_relaxed);
}

void MixologyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer);
}

void MixologyAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    process (buffer);
}

void MixologyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("MixologyState");
    xml.setAttribute ("version", JucePlugin_VersionString);
    xml.setAttribute ("ui", getUiState());
    xml.setAttribute ("protected", bridgeHub->protectedState());   // the padlocks on the Tracks page
    copyXmlToBinary (xml, destData);
}

void MixologyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName ("MixologyState"))
        {
            setUiState (xml->getStringAttribute ("ui"));
            bridgeHub->setProtectedState (xml->getStringAttribute ("protected"));
        }
}

juce::AudioProcessorEditor* MixologyAudioProcessor::createEditor()
{
    if (MixWebEditor::isSupported())
        return new MixWebEditor (*this);
    return new MixFallbackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixologyAudioProcessor();
}
