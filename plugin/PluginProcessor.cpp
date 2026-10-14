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

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        mixology::proc::host::addParameters (layout, 1);   // Full Mix processing (Milestone 5)
        return layout;
    }

    float meterDefault (int i) { return i < 4 || i == 13 ? -100.0f : 0.0f; }   // peaks (and the Space level), then dB taken off
}

MixologyAudioProcessor::MixologyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MixologyParams", makeLayout())
{
    procRaw.bind (parameters);
    for (int i = 0; i < mixology::proc::kMeterValues; ++i) procMeters[static_cast<std::size_t> (i)].store (meterDefault (i));
    // Polish / Auto Mix can propose the Full Mix's tone: they read and set it through the hub.
    bridgeHub->setMixProcessing (this, { [this] { return procRaw.read(); }, [this] (int index, float value) { setProc (index, value); } });
    startTimerHz (10);
}

MixologyAudioProcessor::~MixologyAudioProcessor()
{
    bridgeHub->clearMixProcessing (this);
}

void MixologyAudioProcessor::timerCallback()
{
    const auto values = procRaw.read();
    const int latency = chain.latencyFor (values);
    if (latency != getLatencySamples()) setLatencySamples (latency);   // the Limiter's lookahead, while it is on
    if (mixology::proc::isOn (values, mixology::proc::Module::space)) chain.ensureSpace();   // never on the audio thread
    if (const double tail = mixology::proc::host::reportedTail (values); mixology::proc::host::tailChanged (tail, tailTold.load()))
    {
        tailTold.store (tail);
        updateHostDisplay (ChangeDetails().withLatencyChanged (true));   // the host asks for latency and tail again
    }
}

mixology::proc::Meters MixologyAudioProcessor::takeProcMeters()
{
    float v[mixology::proc::kMeterValues];
    for (int i = 0; i < mixology::proc::kMeterValues; ++i)
        v[i] = procMeters[static_cast<std::size_t> (i)].exchange (meterDefault (i));
    return mixology::proc::metersFrom (v, procRunning.load());
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
    const auto values = procRaw.read();
    chain.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    if (mixology::proc::isOn (values, mixology::proc::Module::space)) chain.ensureSpace();
    chain.setValues (values);
    setLatencySamples (chain.latencyFor (values));
    tailTold.store (mixology::proc::host::reportedTail (values));
}

template <typename Sample>
void MixologyAudioProcessor::process (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (channels <= 0 || n <= 0) return;

    // Analyze Mix hears what the tracks add up to (before the Full Mix processing) ...
    analysisRing.push (buffer.getArrayOfReadPointers(), channels, n);

    chain.setHost (beat.bpm.load (std::memory_order_relaxed), -1.0);   // the tempo as of the last block
    chain.setValues (procRaw.read());
    chain.process (buffer.getArrayOfWritePointers(), channels, n);
    {
        const auto m = chain.takeMeters();
        float v[mixology::proc::kMeterValues];
        mixology::proc::meterValues (m, v);
        for (int i = 0; i < mixology::proc::kMeterValues; ++i)
        {
            auto& a = procMeters[static_cast<std::size_t> (i)];
            if (i == 8) { if (std::abs (v[i]) > std::abs (a.load (std::memory_order_relaxed))) a.store (v[i], std::memory_order_relaxed); }   // transient: largest move
            else storeMax (a, v[i]);
        }
        procRunning.store (m.running, std::memory_order_relaxed);
    }

    // ... the meters and scopes show the mix as it leaves (Mono Check, below, only changes what you hear).
    const auto* const* data = buffer.getArrayOfReadPointers();
    loudness.process (data, channels, n);
    spectrumTap.push (data, channels, n);
    stereoTap.push (data, channels, n);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            transportPlaying.store (pos->getIsPlaying(), std::memory_order_relaxed);
            if (auto bpm = pos->getBpm()) beat.bpm.store (*bpm, std::memory_order_relaxed);
            if (auto ppq = pos->getPpqPosition()) beat.ppq.store (*ppq, std::memory_order_relaxed);
            if (auto bar = pos->getPpqPositionOfLastBarStart()) beat.barStartPpq.store (*bar, std::memory_order_relaxed);
            if (auto sig = pos->getTimeSignature()) beat.numerator.store (sig->numerator, std::memory_order_relaxed);
            beat.whenMs.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
        }

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

    // Mono Check: you hear the mix folded to mono; the scope and meters above still show it as it really is.
    if (monoListen.load (std::memory_order_relaxed) && channels >= 2)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const auto m = static_cast<Sample> (0.5) * (l[i] + r[i]);
            l[i] = r[i] = m;
        }
    }
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
    xml.setAttribute ("tracks", bridgeHub->trackSettingsState());  // your track roles, tracks left out of Analyze Mix
    xml.setAttribute ("prefs", bridgeHub->mixPrefsState());         // how you like your vocal
    if (auto params = parameters.copyState().createXml())           // the Full Mix processing
        xml.addChildElement (params.release());
    copyXmlToBinary (xml, destData);
}

void MixologyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName ("MixologyState"))
        {
            setUiState (xml->getStringAttribute ("ui"));
            bridgeHub->setProtectedState (xml->getStringAttribute ("protected"));
            bridgeHub->setTrackSettingsState (xml->getStringAttribute ("tracks"));
            bridgeHub->setMixPrefsState (xml->getStringAttribute ("prefs"));
            if (auto* params = xml->getChildByName (parameters.state.getType()))
                parameters.replaceState (juce::ValueTree::fromXml (*params));
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
