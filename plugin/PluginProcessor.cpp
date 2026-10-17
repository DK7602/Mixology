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

    // The DAWs Mixology moves faders in: there it opens its bridge as soon as the project plays, window or not.
    bool bridgeDaw (const std::string& key) { return key == "cubase" || key == "fl" || key == "ableton" || key == "logic" || key == "cakewalk"; }

    /** The id of a project saved before Mixology kept one: the same for every Mixology of that project (they all
        saved the same state), a new one when there is nothing to keep apart. */
    std::uint64_t idFromState (const BridgeHub::ProjectState& ps)
    {
        if (ps.empty()) return 0;
        std::uint64_t h = 14695981039346656037ull;
        for (const auto* text : { &ps.protectedText, &ps.tracks, &ps.prefs, &ps.session })
        {
            for (const auto c : text->toStdString()) { h ^= static_cast<unsigned char> (c); h *= 1099511628211ull; }
            h ^= 0xFF;
            h *= 1099511628211ull;
        }
        return h | 1u;
    }
}

MixologyAudioProcessor::MixologyAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "MixologyParams", makeLayout())
{
    procRaw.bind (parameters);
    daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));   // until you pick one (here or in Voxology / Masterology)
    for (int i = 0; i < mixology::proc::kMeterValues; ++i) procMeters[static_cast<std::size_t> (i)].store (meterDefault (i));
    mixL.resize (static_cast<std::size_t> (mixology::TrackAnalyzer::kMaxSize));
    mixR.resize (mixL.size());
    projectId.store (bridgeHub->idForNewInstance());
    // Polish / Auto Mix can propose the Full Mix's tone: they read and set it through the hub.
    BridgeHub::Instance me;
    me.processing = { [this] { return procRaw.read(); }, [this] (int index, float value) { setProc (index, value); } };
    me.project = [this] { return projectId.load(); };
    me.adopt = [this] (std::uint64_t id) { projectId.store (id); };
    me.lastAudioMs = [this] { return lastAudioMs.load (std::memory_order_relaxed); };
    me.lastSeenMs = [this] { return lastSeenMs.load (std::memory_order_relaxed); };
    me.trackName = [this] { const juce::ScopedLock sl (stateLock); return trackName; };
    bridgeHub->registerInstance (this, std::move (me));
    startTimerHz (10);
}

MixologyAudioProcessor::~MixologyAudioProcessor()
{
    stopTimer();
    bridgeHub->unregisterInstance (this);
    {
        const juce::ScopedLock sl (stateLock);
        songStyle.leave (suite::Link::process(), instanceId);
        daw.leave (suite::Link::process(), instanceId);
    }
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
    if (--handoffCountdown <= 0)
    {
        handoffCountdown = 4;
        syncStyle();
        publishHandoff();
        // The bridge speaks to the DAW this really is (Logic Pro / Cakewalk: Mixology is their Mackie Control
        // surface); a DAW picked in Voxology or Masterology only changes the words, unless the host isn't known.
        bridgeHub->setDaw (detectedDaw != "other" ? detectedDaw : dawNow().key);
        if (prepared.load() && ! bridgeHub->isEnabled() && bridgeDaw (detectedDaw))
            bridgeHub->enable();   // the bridge connects with the project, before the window first opens
    }
    if (--analysisCountdown <= 0)
    {
        analysisCountdown = 2;
        feedAnalysis();
    }
}

void MixologyAudioProcessor::feedAnalysis()
{
    // Analyze Mix and the setup check hear the hub's Full Mix (the Mixology on the master), window open or not.
    if (! bridgeHub->isMixOwner (this) || ! bridgeHub->isEnabled()) return;
    const double rate = juce::jmax (8000.0, getSampleRate());
    analysisRing.copyLatest (mixL.data(), mixR.data(), mixology::TrackAnalyzer::sizeFor (rate));
    const auto frame = mixAnalyzer.analyse (mixL.data(), mixR.data(), rate);
    bridgeHub->analysisTick (frame, transportPlaying.load(), getTotalNumOutputChannels());
}

void MixologyAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    {
        const juce::ScopedLock sl (stateLock);
        trackName = properties.name.value_or (juce::String());
    }
    // the hub picks the Mixology on the master again on its next look (message thread)
}

void MixologyAudioProcessor::pickStyle (const std::string& key, const std::string& name, bool byYou)
{
    {
        const juce::ScopedLock sl (stateLock);
        if (! byYou)
        {
            if (songStyle.style().key.empty()) songStyle.setUnpicked (key, name);   // the default: the others' choice wins
            return;
        }
        if (key == songStyle.style().key) return;
        songStyle.pick (key, name);
    }
    syncStyle();   // the others follow at once
}

void MixologyAudioProcessor::syncStyle()
{
    // The song's style: the newest choice in Voxology, Mixology or Masterology wins; the interface follows.
    const juce::ScopedLock sl (stateLock);
    if (songStyle.sync (suite::Link::process(), instanceId, suite::nowMs())) ++styleVersion;
    if (daw.sync (suite::Link::process(), instanceId, suite::nowMs())) ++styleVersion;
}

void MixologyAudioProcessor::pickDaw (const std::string& key)
{
    {
        const juce::ScopedLock sl (stateLock);
        if (key == daw.style().key && daw.style().pickedAt != 0) return;
        if (key.empty()) daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));   // back to the one found
        else daw.pick (key, suite::dawName (key));
        ++styleVersion;
    }
    syncStyle();
}

void MixologyAudioProcessor::publishHandoff()
{
    // One Mixology publishes: the one on the Stereo Out (the hub's Full Mix).
    if (! bridgeHub->isMixOwner (this)) return;
    std::string styleKey, styleName;
    {
        const juce::ScopedLock sl (stateLock);
        styleKey = songStyle.style().key;
        styleName = songStyle.style().name;
    }
    suite::MixInfo m;
    m.styleKey = styleKey;
    m.styleName = styleName;
    m.peakDbtp = meters.truePeak.load();
    m.integratedLufs = meters.integrated.load();
    m.shortTermLufs = meters.shortTerm.load();
    if (mixology::proc::isOn (procRaw.read(), mixology::proc::Module::limit)) m.flags |= suite::MixInfo::limited;
    bridgeHub->handoffTone (styleKey, m);   // after Analyze Mix: the mix's tone against the style, a mono mix
    m.version = static_cast<std::uint32_t> (JucePlugin_VersionCode);
    suite::Link::process().publishMix (m, suite::nowMs());
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
    prepared.store (true);
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
    lastAudioMs.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            // Peaks and loudness count from the moment you press play (a clip before a fix doesn't stay forever).
            if (pos->getIsPlaying() && ! wasPlaying) loudness.resetStatistics();
            wasPlaying = pos->getIsPlaying();
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
    // This project's part of the hub (another open project's is kept apart): as the hub has it, else as loaded.
    const auto id = projectId.load();
    bool known = false;
    auto ps = bridgeHub->saveProject (id, known);
    {
        const juce::ScopedLock sl (stateLock);
        if (known) projectCopy = ps;
        else ps = projectCopy;
    }
    xml.setAttribute ("project", juce::String::toHexString (static_cast<juce::int64> (id)));
    xml.setAttribute ("protected", ps.protectedText);   // the padlocks on the Tracks page
    xml.setAttribute ("tracks", ps.tracks);             // your track roles, tracks left out of Analyze Mix
    xml.setAttribute ("prefs", ps.prefs);               // how you like your vocal
    xml.setAttribute ("session", ps.session);           // the History, the advice, the restore points
    {
        const juce::ScopedLock sl (stateLock);
        xml.setAttribute ("songStyle", juce::String::fromUTF8 (songStyle.save().c_str()));   // + when you picked it
        if (daw.style().pickedAt != 0)   // your DAW, when you picked one (else it is found from the host each time)
            xml.setAttribute ("daw", juce::String::fromUTF8 (daw.save().c_str()));
    }
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
            {
                // The song's style. Projects from before the suite style kept it in the interface's state only:
                // that was your choice, so it counts as picked (just older than any choice made since).
                const juce::ScopedLock sl (stateLock);
                if (! songStyle.restore (xml->getStringAttribute ("songStyle").toStdString()))
                {
                    const auto ui = juce::JSON::parse (uiState);
                    const auto key = ui.getProperty ("preset", juce::var());
                    if (key.isString() && key.toString().isNotEmpty())
                        songStyle.restore (key.toString().toStdString() + "|" + ui.getProperty ("presetName", "").toString().toStdString() + "|1|2");
                }
                daw = suite::SharedChoice { suite::StyleInfo::From::mixology, suite::Choice::daw };
                if (! daw.restore (xml->getStringAttribute ("daw").toStdString()))
                    daw.setUnpicked (detectedDaw, suite::dawName (detectedDaw));
                ++styleVersion;   // an open interface shows it
            }
            const BridgeHub::ProjectState ps { xml->getStringAttribute ("protected"), xml->getStringAttribute ("tracks"),
                                               xml->getStringAttribute ("prefs"), xml->getStringAttribute ("session") };
            auto id = static_cast<std::uint64_t> (xml->getStringAttribute ("project").getHexValue64());
            if (id == 0) id = idFromState (ps);   // a project saved before Mixology kept its id
            if (id == 0) id = projectId.load();   // nothing to keep apart: this Mixology's own
            const auto previous = projectId.exchange (id);
            {
                const juce::ScopedLock sl (stateLock);
                projectCopy = ps;
            }
            bridgeHub->loadProject (this, previous, id, ps);
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
