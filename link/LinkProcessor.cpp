#include "LinkProcessor.h"
#include "LinkEditor.h"

namespace
{
    constexpr float kTrimRange = 24.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        auto dbText = [] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; };
        auto hzText = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "trim", 1 }, "Trim",
            juce::NormalisableRange<float> (-kTrimRange, kTrimRange, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

        // Assist EQ (Milestone 4): Mixology's Clean Up and Vocal Pocket set these.
        auto hzRange = [] (float lo, float hi) {
            juce::NormalisableRange<float> r (lo, hi, 1.0f);
            r.setSkewForCentre (std::sqrt (lo * hi));
            return r;
        };
        juce::NormalisableRange<float> lowCutRange (0.0f, 500.0f, 1.0f);
        lowCutRange.setSkewForCentre (100.0f);
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "lowcut", 2 }, "Low Cut", lowCutRange, 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
                [hzText] (float v, int n) { return v < mixology::AssistSettings::kLowCutOff ? juce::String ("Off") : hzText (v, n); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mudfreq", 2 }, "Mud Frequency", hzRange (120.0f, 800.0f), 300.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "mudgain", 2 }, "Mud Dip",
            juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "pocketfreq", 2 }, "Pocket Frequency", hzRange (800.0f, 8000.0f), 2500.0f,
            juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "pocketgain", 2 }, "Pocket Dip",
            juce::NormalisableRange<float> (-12.0f, 0.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

        // Processing (Milestone 5): set from Mixology's EQ, Dynamics, Tone and Stereo pages.
        mixology::proc::host::addParameters (layout, 3);
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
    lowCutParam = parameters.getRawParameterValue ("lowcut");
    mudHzParam = parameters.getRawParameterValue ("mudfreq");
    mudDbParam = parameters.getRawParameterValue ("mudgain");
    pocketHzParam = parameters.getRawParameterValue ("pocketfreq");
    pocketDbParam = parameters.getRawParameterValue ("pocketgain");
    procRaw.bind (parameters);
    instanceId = (static_cast<std::uint64_t> (static_cast<std::uint32_t> (juce::Random::getSystemRandom().nextInt())) << 32)
                 ^ static_cast<std::uint32_t> (juce::Random::getSystemRandom().nextInt()) ^ static_cast<std::uint64_t> (juce::Time::getHighResolutionTicks());
    slot = registry.claim (instanceId, mixology::link::nowMs());
    registry.learnProcRequests (slot, procRequestSeen);
    snapL.resize (static_cast<std::size_t> (mixology::TrackAnalyzer::kMaxSize));
    snapR.resize (static_cast<std::size_t> (mixology::TrackAnalyzer::kMaxSize));
    startTimerHz (30);
    analysisThread->addTimeSliceClient (this);
}

MixologyLinkProcessor::~MixologyLinkProcessor()
{
    analysisThread->removeTimeSliceClient (this);   // waits if it is analysing this Link right now
    stopTimer();
    registry.release (slot, instanceId);
}

// Analysis thread: about five times a second, while audio is flowing.
int MixologyLinkProcessor::useTimeSlice()
{
    constexpr int kIntervalMs = 200;
    const auto now = mixology::link::nowMs();
    const auto last = lastAudioMs.load (std::memory_order_relaxed);
    if (last == 0 || now - std::min (now, last) > 300) return kIntervalMs;   // stopped or bypassed: nothing new

    const double rate = currentRate.load();
    ring.copyLatest (snapL.data(), snapR.data(), mixology::TrackAnalyzer::sizeFor (rate));
    const auto frame = analyzer.analyse (snapL.data(), snapR.data(), rate);
    registry.publishAnalysis (slot, frame, transportPlaying.load(), songPosition.load(), now);
    return kIntervalMs;
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
    currentRate = sampleRate;
    publishedWindows = meter.windowCount();
    trimGain.reset (sampleRate, 0.02);
    trimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (static_cast<double> (trimParam->load())));
    assist.setTarget (assistSettings());
    assist.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    const auto values = procRaw.read();
    chain.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    if (mixology::proc::isOn (values, mixology::proc::Module::space)) chain.ensureSpace();
    chain.setValues (values);
    setLatencySamples (chain.latencyFor (values));
    keyEnv = 0.0;
}

mixology::AssistSettings MixologyLinkProcessor::assistSettings() const
{
    mixology::AssistSettings a;
    a.lowCutHz = lowCutParam->load (std::memory_order_relaxed);
    if (! a.lowCutOn()) a.lowCutHz = 0.0f;
    a.mudHz = mudHzParam->load (std::memory_order_relaxed);
    a.mudDb = mudDbParam->load (std::memory_order_relaxed);
    a.pocketHz = pocketHzParam->load (std::memory_order_relaxed);
    a.pocketDb = pocketDbParam->load (std::memory_order_relaxed);
    return a;
}

void MixologyLinkProcessor::setAssist (const mixology::AssistSettings& a)
{
    for (auto [id, v] : { std::pair { "lowcut", a.lowCutOn() ? a.lowCutHz : 0.0f }, std::pair { "mudfreq", a.mudHz },
                          std::pair { "mudgain", a.mudDb }, std::pair { "pocketfreq", a.pocketHz }, std::pair { "pocketgain", a.pocketDb } })
        if (auto* p = parameters.getParameter (id))
        {
            const auto n = p->convertTo0to1 (v);
            if (std::abs (n - p->getValue()) < 1.0e-6f) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (n);
            p->endChangeGesture();
        }
}

template <typename Sample>
void MixologyLinkProcessor::process (juce::AudioBuffer<Sample>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (channels <= 0 || n <= 0) return;
    mixology::proc::host::silenceNonFinite (buffer, channels);

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

    assist.setTarget (assistSettings());
    assist.process (buffer.getArrayOfWritePointers(), channels, n);

    // This track's level before its processing (a Space effect elsewhere can duck under it): instant up,
    // about 60 ms down, so a Link that runs its block a little earlier or later still sees it.
    const auto now = mixology::link::nowMs();
    {
        double peak = 0.0;
        for (int c = 0; c < channels; ++c)
        {
            const auto* x = buffer.getReadPointer (c);
            for (int i = 0; i < n; ++i) peak = std::max (peak, std::abs (static_cast<double> (x[i])));
        }
        const double rate = currentRate.load (std::memory_order_relaxed);
        keyEnv = std::max (peak, keyEnv * std::exp (-static_cast<double> (n) / (0.06 * rate)));
        if (keyEnv < 1.0e-6) keyEnv = 0.0;
        registry.publishKey (slot, static_cast<float> (keyEnv), now);
    }

    // The song's tempo (the Delay's note values) and the level of the tracks this one ducks under.
    double bpm = 0.0;
    juce::Optional<juce::AudioPlayHead::PositionInfo> position;
    if (auto* ph = getPlayHead()) position = ph->getPosition();
    if (position.hasValue())
        if (auto b = position->getBpm()) bpm = *b;
    double key = -1.0;
    if (const int sources = duckSlotCount.load (std::memory_order_acquire); sources > 0)
    {
        key = 0.0;
        for (int i = 0; i < std::min (sources, mixology::link::kMaxDuckSources); ++i)
            key = std::max (key, static_cast<double> (registry.keyLevel (duckSlots[static_cast<std::size_t> (i)].load (std::memory_order_relaxed), now)));
    }
    chain.setHost (bpm, key);
    chain.setValues (procRaw.read());
    chain.process (buffer.getArrayOfWritePointers(), channels, n);

    // Measured after the Trim, the Assist EQ and the processing: what this track sends on to the fader.
    meter.process (buffer.getArrayOfReadPointers(), channels, n);
    ring.push (buffer.getArrayOfReadPointers(), channels, n);
    if (position.hasValue())
    {
        transportPlaying.store (position->getIsPlaying(), std::memory_order_relaxed);
        if (auto t = position->getTimeInSamples()) songPosition.store (*t, std::memory_order_relaxed);
    }
    if (meter.windowCount() != publishedWindows)
    {
        publishedWindows = meter.windowCount();
        const float l = meter.peakDb (0), r = meter.peakDb (1);
        storeMax (editorPeak[0], l);
        storeMax (editorPeak[1], r);
        momentary.store (meter.momentaryLufs(), std::memory_order_relaxed);
        shortTerm.store (meter.shortTermLufs(), std::memory_order_relaxed);
        registry.publishMeters (slot, l, r, meter.momentaryLufs(), meter.shortTermLufs(), meter.channels(), now);
        const auto pm = chain.takeMeters();
        running.store (pm.running, std::memory_order_relaxed);
        spaceWet.store (pm.spaceWet, std::memory_order_relaxed);
        registry.publishProcMeters (slot, pm, now);
        lastAudioMs.store (now, std::memory_order_relaxed);
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

    registry.publishAssist (slot, assistSettings());
    mixology::AssistSettings wanted;
    if (registry.takeAssistRequest (slot, assistRequestSeen, wanted))
    {
        setAssist (wanted);
        trimNote = wanted.neutral() ? "Mixology cleared the Assist EQ" : "Mixology set the Assist EQ";
    }

    // Processing: Mixology's page moves, applied like your own (host parameters), then what we have now.
    int indices[mixology::proc::count];
    float values[mixology::proc::count];
    const int changed = registry.takeProcRequests (slot, procRequestSeen, indices, values, mixology::proc::count);
    for (int i = 0; i < changed; ++i) setProc (indices[i], values[i]);
    const auto now = procRaw.read();
    const int latency = chain.latencyFor (now);
    if (latency != getLatencySamples()) setLatencySamples (latency);   // the Limiter's lookahead, while it is on
    registry.publishProc (slot, now, static_cast<float> (currentRate.load()), latency);

    // Space: its delay lines are made the first time it is switched on (never on the audio thread).
    if (mixology::proc::isOn (now, mixology::proc::Module::space)) chain.ensureSpace();

    mixology::link::DuckSources wantedDuck;
    if (registry.takeDuckRequest (slot, duckRequestSeen, wantedDuck)) setDuck (wantedDuck);
    if (--resolveCountdown <= 0)   // about three times a second: tracks come and go, slots change
    {
        resolveCountdown = 10;
        resolveDuck();
        registry.publishDuck (slot, duck);
    }
}

void MixologyLinkProcessor::setDuck (const mixology::link::DuckSources& d)
{
    mixology::link::DuckSources clean;
    for (int i = 0; i < std::min (d.count, mixology::link::kMaxDuckSources); ++i)
        if (const auto h = d.hashes[static_cast<std::size_t> (i)]; h != 0)
            clean.hashes[static_cast<std::size_t> (clean.count++)] = h;
    duck = clean;
    juce::StringArray saved;
    for (int i = 0; i < duck.count; ++i) saved.add (juce::String::toHexString (static_cast<int> (duck.hashes[static_cast<std::size_t> (i)])));
    parameters.state.setProperty ("duck", saved.joinIntoString (","), nullptr);   // saved with the project
    resolveDuck();
    registry.publishDuck (slot, duck);
    resolveCountdown = 10;
}

void MixologyLinkProcessor::resolveDuck()
{
    int found[mixology::link::kMaxDuckSources];
    const int n = registry.findSlots (duck, slot, mixology::link::nowMs(), found, mixology::link::kMaxDuckSources);
    for (int i = 0; i < n; ++i) duckSlots[static_cast<std::size_t> (i)].store (found[i], std::memory_order_relaxed);
    duckSlotCount.store (n, std::memory_order_release);
}

juce::StringArray MixologyLinkProcessor::duckNames() const
{
    juce::StringArray names;
    const int n = duckSlotCount.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
    {
        const auto id = registry.readIdentity (duckSlots[static_cast<std::size_t> (i)].load (std::memory_order_relaxed));
        if (id.hasName) names.addIfNotAlreadyThere (juce::String::fromUTF8 (id.name.c_str()));
    }
    return names;
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
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            mixology::link::DuckSources d;
            for (const auto& h : juce::StringArray::fromTokens (parameters.state.getProperty ("duck").toString(), ",", ""))
                if (d.count < mixology::link::kMaxDuckSources && h.isNotEmpty())
                    d.hashes[static_cast<std::size_t> (d.count++)] = static_cast<std::uint32_t> (h.getHexValue32());
            setDuck (d);
        }
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
