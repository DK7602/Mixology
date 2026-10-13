#include "LinkProcessor.h"
#include "LinkEditor.h"

namespace
{
    constexpr float kTrimRange = 24.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "trim", 1 }, "Trim",
            juce::NormalisableRange<float> (-kTrimRange, kTrimRange, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; })));
        return layout;
    }

    void storeMax (std::atomic<float>& a, float v) noexcept
    {
        auto cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
}

MixologyLinkProcessor::MixologyLinkProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MixologyLinkParams", makeLayout()),
      registry (mixology::link::processRegistry())
{
    trimParam = parameters.getRawParameterValue ("trim");
    instanceId = (static_cast<std::uint64_t> (static_cast<std::uint32_t> (juce::Random::getSystemRandom().nextInt())) << 32)
                 ^ static_cast<std::uint32_t> (juce::Random::getSystemRandom().nextInt()) ^ static_cast<std::uint64_t> (juce::Time::getHighResolutionTicks());
    slot = registry.claim (instanceId, mixology::link::nowMs());
    startTimerHz (10);
}

MixologyLinkProcessor::~MixologyLinkProcessor()
{
    stopTimer();
    registry.release (slot, instanceId);
}

bool MixologyLinkProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MixologyLinkProcessor::prepareToPlay (double sampleRate, int)
{
    meter.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    publishedWindows = meter.windowCount();
    trimGain.reset (sampleRate, 0.02);
    trimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (static_cast<double> (trimParam->load())));
}

template <typename Sample>
void MixologyLinkProcessor::process (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (channels <= 0 || n <= 0) return;

    trimGain.setTargetValue (juce::Decibels::decibelsToGain (static_cast<double> (trimParam->load())));
    if (trimGain.isSmoothing())
    {
        for (int i = 0; i < n; ++i)
        {
            const auto g = static_cast<Sample> (trimGain.getNextValue());
            for (int c = 0; c < channels; ++c)
                buffer.getWritePointer (c)[i] *= g;
        }
    }
    else if (std::abs (trimGain.getTargetValue() - 1.0) > 1.0e-9)
    {
        buffer.applyGain (static_cast<Sample> (trimGain.getTargetValue()));
    }

    // Measured after the Trim: what this track sends on to the fader.
    meter.process (buffer.getArrayOfReadPointers(), channels, n);
    if (meter.windowCount() != publishedWindows)
    {
        publishedWindows = meter.windowCount();
        const float l = meter.peakDb (0), r = meter.peakDb (1);
        storeMax (editorPeak[0], l);
        storeMax (editorPeak[1], r);
        momentary.store (meter.momentaryLufs(), std::memory_order_relaxed);
        shortTerm.store (meter.shortTermLufs(), std::memory_order_relaxed);
        registry.publishMeters (slot, l, r, meter.momentaryLufs(), meter.shortTermLufs(), meter.channels(), mixology::link::nowMs());
    }
}

void MixologyLinkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { process (buffer); }
void MixologyLinkProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { process (buffer); }

void MixologyLinkProcessor::updateTrackProperties (const TrackProperties& p)
{
    trackIdentity.hasName = p.name.has_value() && p.name->isNotEmpty();
    trackIdentity.name = trackIdentity.hasName ? *p.name : juce::String();
    trackIdentity.hasColour = p.colourARGB.has_value();
    trackIdentity.colour = trackIdentity.hasColour ? juce::Colour (*p.colourARGB) : juce::Colour();
    publishIdentity();
}

void MixologyLinkProcessor::publishIdentity()
{
    mixology::link::Identity id;
    id.hasName = trackIdentity.hasName;
    id.name = trackIdentity.name.toStdString();
    id.hasColour = trackIdentity.hasColour;
    id.rgb = trackIdentity.colour.getARGB() & 0xFFFFFFu;
    registry.setIdentity (slot, id);
}

bool MixologyLinkProcessor::mixologyOpen() const
{
    return registry.mainAlive (mixology::link::nowMs());
}

mixology::link::MatchStatus MixologyLinkProcessor::pairing() const
{
    return registry.matchSeen (slot, mixology::link::nowMs());
}

void MixologyLinkProcessor::setTrimDb (float db)
{
    if (auto* p = parameters.getParameter ("trim"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-kTrimRange, kTrimRange, db)));
}

void MixologyLinkProcessor::timerCallback()
{
    registry.heartbeat (slot, mixology::link::nowMs());
    registry.publishTrim (slot, trimParam->load());

    float requested = 0.0f;
    if (registry.takeTrimRequest (slot, trimRequestSeen, requested))
    {
        if (auto* p = parameters.getParameter ("trim"))
        {
            p->beginChangeGesture();
            setTrimDb (requested);
            p->endChangeGesture();
        }
        trimNote = "Mixology set the Trim to " + juce::String (requested, 1) + " dB";
    }
}

void MixologyLinkProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("version", JucePlugin_VersionString, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MixologyLinkProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MixologyLinkProcessor::createEditor()
{
    if (LinkEditor::isSupported())
        return new LinkEditor (*this);
    return new juce::GenericAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixologyLinkProcessor();
}
