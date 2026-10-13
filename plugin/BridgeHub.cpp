#include "BridgeHub.h"

#include <set>

namespace
{
    constexpr double kReopenInterval = 3.0;   // seconds between attempts to find the ports
    constexpr int kLogLinesForUi = 200;

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Mixology";
        o.filenameSuffix = ".settings";
        o.folderName = "Mixology";
        o.osxLibrarySubFolder = "Application Support";
        o.commonToAllUsers = false;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return o;
    }

    // Exact name first; otherwise a device whose name contains it (Windows MIDI Services and some
    // drivers add prefixes or numbers, e.g. "2- Mixology to Cubase").
    template <typename DeviceList>
    juce::MidiDeviceInfo findDevice (const DeviceList& devices, const juce::String& wanted)
    {
        for (const auto& d : devices)
            if (d.name.trim().equalsIgnoreCase (wanted)) return d;
        for (const auto& d : devices)
            if (d.name.containsIgnoreCase (wanted)) return d;
        return {};
    }

    juce::String stamp (double seconds)
    {
        const auto ms = static_cast<juce::int64> (seconds * 1000.0);
        return juce::String::formatted ("%02d:%02d.%03d", static_cast<int> ((ms / 60000) % 60),
                                        static_cast<int> ((ms / 1000) % 60), static_cast<int> (ms % 1000));
    }
}

BridgeHub::BridgeHub()
    : bridge (static_cast<std::uint16_t> (juce::Random::getSystemRandom().nextInt (0x3FFF) + 1)),
      settingsFile (std::make_unique<juce::PropertiesFile> (settingsOptions()))
{
    // Curves saved before format 2 could hold pairs learned mid-move (new position, old text): start fresh.
    if (settingsFile->getIntValue ("faderLawFormat", 1) >= 2)
        bridge.faderLaw().deserialise (settingsFile->getValue ("faderLaw").toStdString());
    settingsFile->setValue ("faderLawFormat", 2);
    lawPointsSaved = bridge.faderLaw().size();
}

BridgeHub::~BridgeHub()
{
    stopTimer();
    closePorts ("Mixology closed");
    settingsFile->setValue ("faderLaw", juce::String (bridge.faderLaw().serialise()));
    settingsFile->saveIfNeeded();
}

double BridgeHub::now()
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void BridgeHub::enable()
{
    if (enabled) return;
    enabled = true;
    openPorts();
    startTimerHz (100);
}

void BridgeHub::openPorts()
{
    lastOpenAttempt = now();
    const auto outInfo = findDevice (juce::MidiOutput::getAvailableDevices(), kToCubase);
    const auto inInfo = findDevice (juce::MidiInput::getAvailableDevices(), kFromCubase);

    juce::String problem;
    if (outInfo.identifier.isEmpty() || inInfo.identifier.isEmpty())
    {
        problem = "Can't find the virtual MIDI port";
        if (outInfo.identifier.isEmpty() && inInfo.identifier.isEmpty())
            problem << "s '" << kToCubase << "' and '" << kFromCubase << "'";
        else
            problem << " '" << (outInfo.identifier.isEmpty() ? kToCubase : kFromCubase) << "'";
        problem << ". Create them in loopMIDI (see Setup), then press Reconnect.";
    }
    else
    {
        toCubase = juce::MidiOutput::openDevice (outInfo.identifier);
        fromCubase = juce::MidiInput::openDevice (inInfo.identifier, this);
        if (toCubase == nullptr || fromCubase == nullptr)
        {
            problem = "Found the ports but couldn't open '";
            problem << (toCubase == nullptr ? outInfo.name : inInfo.name)
                    << "'. Another program may be holding it, or the driver allows only one user.";
            toCubase.reset();
            fromCubase.reset();
        }
        else
        {
            fromCubase->start();
            portProblem.clear();
            bridge.setPortsOpen (true, now(), (outInfo.name + " / " + inInfo.name).toStdString());
            return;
        }
    }

    if (problem != portProblem)
    {
        portProblem = problem;
        bridge.setPortsOpen (false, now(), problem.toStdString());
    }
}

void BridgeHub::closePorts (const juce::String& why)
{
    if (fromCubase != nullptr) fromCubase->stop();
    fromCubase.reset();
    toCubase.reset();
    if (bridge.link() != mixology::BridgeSession::Link::offline)
        bridge.setPortsOpen (false, now(), why.toStdString());
}

void BridgeHub::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    if (! message.isSysEx()) return;
    const auto* raw = message.getRawData();
    const auto size = static_cast<std::size_t> (message.getRawDataSize());
    const juce::ScopedLock sl (inboxLock);
    if (inbox.size() < 4096)
        inbox.emplace_back (raw, raw + size);
}

void BridgeHub::timerCallback()
{
    const double t = now();
    applyPendingProtected();   // a project loaded its padlocks off the message thread
    {
        juce::String pending;
        bool has = false;
        {
            const juce::ScopedLock sl (protectedLock);
            if (hasPendingTrackSettings) { pending = trackSettingsPending; has = true; hasPendingTrackSettings = false; }
        }
        if (has)
        {
            trackSettings.clear();
            for (const auto& line : juce::StringArray::fromLines (pending))
            {
                juce::StringArray f;
                f.addTokens (line, "\t", "");
                const auto hash = static_cast<std::uint32_t> (f[0].getLargeIntValue());
                if (hash == 0) continue;
                auto& ts = trackSettings[hash];
                ts.role = mixology::mix::roleFromKey (f[1].toStdString());
                ts.excluded = f[2] == "1";
            }
            cacheTrackSettings();
            ++analysisVersion;
        }
    }

    if (toCubase == nullptr && t - lastOpenAttempt > kReopenInterval)
        openPorts();

    std::vector<std::vector<std::uint8_t>> received;
    {
        const juce::ScopedLock sl (inboxLock);
        received.swap (inbox);
    }
    for (const auto& m : received)
        bridge.receive (m.data(), m.size(), t);

    bridge.tick (t);

    if (t - lastLinkUpdate >= 1.0 / 30.0)
    {
        lastLinkUpdate = t;
        updateLinks();
    }

    for (const auto& m : bridge.takeOutgoing())
        if (toCubase != nullptr)
            toCubase->sendMessageNow (juce::MidiMessage::createSysExMessage (m.data() + 1, static_cast<int> (m.size()) - 2));

    // Save the learned fader curve now and then (it is the same for every project).
    if (bridge.faderLaw().size() != lawPointsSaved && t - lastLawSave > 5.0)
    {
        lawPointsSaved = bridge.faderLaw().size();
        lastLawSave = t;
        settingsFile->setValue ("faderLaw", juce::String (bridge.faderLaw().serialise()));
        settingsFile->saveIfNeeded();
    }
}

juce::String BridgeHub::logText() const
{
    juce::String text;
    for (const auto& line : bridge.logLines())
        text << stamp (line.time) << "  " << juce::String::fromUTF8 (line.text.c_str()) << "\n";
    return text;
}

void BridgeHub::updateLinks()
{
    using namespace mixology::link;
    auto& registry = processRegistry();
    const auto ms = nowMs();
    registry.mainHeartbeat (ms);

    const auto links = registry.links (ms);
    std::vector<ChannelKey> keys;
    for (int i = 0; i < bridge.slotCount(); ++i)
    {
        const auto& s = bridge.slot (i);
        keys.push_back ({ s.present, s.hash, s.hasColour, s.rgb });
    }
    const auto matches = matchLinks (keys, links);

    std::vector<StripLink> next (keys.size());
    std::vector<bool> told (links.size(), false);
    for (std::size_t i = 0; i < matches.size(); ++i)
    {
        auto& sl = next[i];
        sl.status = matches[i].status;
        if (matches[i].status == MatchStatus::matched)
        {
            const auto& l = links[static_cast<std::size_t> (matches[i].link)];
            sl.slot = l.slot;
            sl.generation = l.generation;
            sl.peakL = l.peakL;
            sl.peakR = l.channels == 1 ? l.peakL : l.peakR;
            sl.momentary = l.momentary;
            sl.trimDb = l.trimDb;
            sl.audio = l.audio;
            registry.reportMatch (l.slot, l.generation, MatchStatus::matched, ms);
            told[static_cast<std::size_t> (matches[i].link)] = true;
        }
    }
    // Links whose name is in view more than once (or twice among the Links) hear why they aren't paired.
    for (std::size_t i = 0; i < matches.size(); ++i)
        if (matches[i].status == MatchStatus::ambiguous)
            for (std::size_t l = 0; l < links.size(); ++l)
                if (! told[l] && links[l].nameHash == keys[i].hash)
                {
                    registry.reportMatch (links[l].slot, links[l].generation, MatchStatus::ambiguous, ms);
                    told[l] = true;
                }

    // Only what the strips show as text / badges bumps the state version; meters go with every frame.
    bool structural = next.size() != stripLinks.size() || static_cast<int> (links.size()) != linksSeen;
    for (std::size_t i = 0; ! structural && i < next.size(); ++i)
        structural = next[i].status != stripLinks[i].status || next[i].slot != stripLinks[i].slot
                     || std::abs (next[i].trimDb - stripLinks[i].trimDb) > 0.05f || next[i].audio != stripLinks[i].audio;
    stripLinks = std::move (next);
    linksSeen = static_cast<int> (links.size());
    if (structural) ++linkVersion;
}

juce::var BridgeHub::trackMeters() const
{
    // The Link sits before the fader; the strip shows the level after it (Link peak + fader dB), like the
    // MixConsole's own meters, so pulling a fader down in Mixology moves its meter too.
    juce::Array<juce::var> out;
    for (std::size_t i = 0; i < stripLinks.size() && static_cast<int> (i) < bridge.slotCount(); ++i)
    {
        const auto& sl = stripLinks[i];
        if (sl.status != mixology::link::MatchStatus::matched) { out.add (juce::var()); continue; }
        const auto& s = bridge.slot (static_cast<int> (i));
        const float fader = s.hasVolume ? static_cast<float> (std::max (s.volumeDb, -200.0)) : 0.0f;
        juce::Array<juce::var> lr;
        lr.add (std::round (std::max (-100.0f, sl.peakL + fader) * 10.0f) / 10.0f);
        lr.add (std::round (std::max (-100.0f, sl.peakR + fader) * 10.0f) / 10.0f);
        out.add (lr);
    }
    return out;
}

juce::String BridgeHub::protectedState() const
{
    const juce::ScopedLock sl (protectedLock);
    return hasPendingProtected ? protectedPending : protectedCache;
}

void BridgeHub::setProtectedState (const juce::String& text)
{
    {
        const juce::ScopedLock sl (protectedLock);
        protectedPending = text;
        hasPendingProtected = true;
    }
    if (juce::MessageManager::existsAndIsCurrentThread())
        applyPendingProtected();
}

void BridgeHub::applyPendingProtected()
{
    juce::String text;
    {
        const juce::ScopedLock sl (protectedLock);
        if (! hasPendingProtected) return;
        text = protectedPending;
        hasPendingProtected = false;
    }
    std::vector<mixology::BridgeSession::Protected> list;
    for (const auto& line : juce::StringArray::fromLines (text))
    {
        const auto hash = static_cast<std::uint32_t> (line.upToFirstOccurrenceOf ("\t", false, false).getLargeIntValue());
        if (hash != 0)
            list.push_back ({ hash, line.fromFirstOccurrenceOf ("\t", false, false).toStdString() });
    }
    bridge.setProtectedList (list);
    cacheProtected();
}

void BridgeHub::cacheProtected()
{
    juce::StringArray lines;
    for (const auto& p : bridge.protectedList())
        lines.add (juce::String (static_cast<juce::int64> (p.hash)) + "\t" + juce::String::fromUTF8 (p.title.c_str()));
    const juce::ScopedLock sl (protectedLock);
    protectedCache = lines.joinIntoString ("\n");
}

juce::String BridgeHub::trackSettingsState() const
{
    const juce::ScopedLock sl (protectedLock);
    return hasPendingTrackSettings ? trackSettingsPending : trackSettingsCache;
}

void BridgeHub::setTrackSettingsState (const juce::String& text)
{
    const juce::ScopedLock sl (protectedLock);
    trackSettingsPending = text;
    hasPendingTrackSettings = true;   // applied on the message thread (next timer tick)
}

void BridgeHub::cacheTrackSettings()
{
    juce::StringArray lines;
    for (const auto& [hash, ts] : trackSettings)
        if (ts.role || ts.excluded)
            lines.add (juce::String (static_cast<juce::int64> (hash)) + "\t" + (ts.role ? mixology::mix::roleKey (*ts.role) : "") + "\t" + (ts.excluded ? "1" : "0"));
    const juce::ScopedLock sl (protectedLock);
    trackSettingsCache = lines.joinIntoString ("\n");
}

// ---------------------------------------------------------------------------------------------
// Analyze Mix

std::optional<mixology::mix::Role> BridgeHub::roleFor (std::uint32_t hash, const std::string& name) const
{
    if (auto it = trackSettings.find (hash); it != trackSettings.end() && it->second.role) return it->second.role;
    for (const auto& t : report.tracks)
        if (t.hash == hash) return t.role;
    return mixology::mix::roleFromName (name);
}

void BridgeHub::analysisTick (const mixology::AnalysisFrame& mixFrame, bool playing)
{
    using namespace mixology;
    if (playing != transportPlaying) { transportPlaying = playing; ++analysisVersion; }
    if (analyzeState == Analyze::reading && ! bridge.scanning()) { analyzeState = Analyze::listening; ++analysisVersion; }

    const auto ms = link::nowMs();
    std::vector<mix::TrackSample> tracks;
    for (const auto& l : link::processRegistry().links (ms))
    {
        if (! l.hasAnalysis || ms - std::min (ms, l.analysisMs) > 500) continue;
        mix::TrackSample t;
        t.hash = l.identity.hasName ? l.nameHash : 0u;
        t.name = l.identity.hasName ? l.identity.name : "Unnamed track " + std::to_string (l.slot + 1);
        if (t.hash != 0)
            if (auto it = bridge.knownFaders().find (t.hash); it != bridge.knownFaders().end())
            {
                t.faderDb = std::max (it->second.db, -200.0);
                t.faderKnown = true;
            }
        t.frame = l.analysis;
        tracks.push_back (std::move (t));
    }
    if (static_cast<int> (tracks.size()) != tracksHeard) { tracksHeard = static_cast<int> (tracks.size()); ++analysisVersion; }

    if (analyzeState != Analyze::listening || ! playing) return;
    mix::Moment m;
    m.mix = mixFrame;
    m.tracks = std::move (tracks);
    analysis.add (std::move (m));
    ++analysisVersion;
    if (analysis.secondsHeard() >= 60.0 || analysis.full()) finishAnalysis();
}

void BridgeHub::finishAnalysis()
{
    // Channels Cubase showed that no Link reported: listed, so the advice is honest about what it heard.
    std::vector<std::string> unheard;
    std::set<std::uint32_t> heard;
    for (const auto& l : mixology::link::processRegistry().links (mixology::link::nowMs()))
        if (l.identity.hasName) heard.insert (l.nameHash);
    for (const auto& [hash, f] : bridge.knownFaders())
    {
        const auto ts = trackSettings.find (hash);
        if (heard.count (hash) == 0 && f.db > mixology::FaderLaw::kMinusInf + 1.0 && (ts == trackSettings.end() || ! ts->second.excluded))
            unheard.push_back (f.title);
    }

    report = analysis.evaluate (trackSettings, unheard);
    analyzeState = Analyze::done;
    ++analysisVersion;
}

juce::var BridgeHub::analysisState() const
{
    static const char* names[] = { "idle", "reading", "listening", "done" };
    auto* o = new juce::DynamicObject();
    o->setProperty ("state", names[static_cast<int> (analyzeState)]);
    o->setProperty ("seconds", analysis.secondsHeard());
    o->setProperty ("enough", mixology::mix::Analysis::kEnoughSeconds);
    o->setProperty ("max", 60.0);
    o->setProperty ("playing", transportPlaying);
    o->setProperty ("heard", tracksHeard);

    if (report.valid)
    {
        auto* r = new juce::DynamicObject();
        r->setProperty ("seconds", report.secondsHeard);
        r->setProperty ("style", juce::String::fromUTF8 (report.style.name.c_str()));
        r->setProperty ("mixLoudness", report.mixLoudness);
        r->setProperty ("mixPeak", report.mixPeak);
        r->setProperty ("mixCorrelation", report.mixCorrelation);
        r->setProperty ("mixBalance", std::round (report.mixBalance * 10.0) / 10.0);
        auto list = [] (const std::vector<std::string>& v) {
            juce::Array<juce::var> a;
            for (const auto& x : v) a.add (juce::String::fromUTF8 (x.c_str()));
            return a;
        };
        auto numbers = [] (const std::vector<double>& v) {
            juce::Array<juce::var> a;
            for (double x : v) a.add (std::round (x * 10.0) / 10.0);
            return a;
        };
        r->setProperty ("unheard", list (report.unheard));
        r->setProperty ("mixBands", numbers (report.mixBands));
        r->setProperty ("targetBands", numbers (report.targetBands));

        juce::Array<juce::var> tracks;
        for (const auto& t : report.tracks)
        {
            auto* to = new juce::DynamicObject();
            to->setProperty ("hash", static_cast<juce::int64> (t.hash));
            to->setProperty ("name", juce::String::fromUTF8 (t.name.c_str()));
            to->setProperty ("role", mixology::mix::roleKey (t.role));
            to->setProperty ("roleFrom", juce::String (t.roleFrom));
            to->setProperty ("excluded", t.excluded);
            to->setProperty ("faderKnown", t.faderKnown);
            to->setProperty ("activity", std::round (t.activity * 100.0) / 100.0);
            to->setProperty ("loudness", std::round (t.loudness * 10.0) / 10.0);
            to->setProperty ("peak", std::round (t.peakBeforeFader * 10.0) / 10.0);
            to->setProperty ("width", std::round (t.width * 100.0) / 100.0);
            to->setProperty ("balance", std::round (t.balance * 10.0) / 10.0);
            tracks.add (juce::var (to));
        }
        r->setProperty ("tracks", tracks);

        juce::Array<juce::var> sugs;
        for (const auto& sg : report.suggestions)
        {
            auto* so = new juce::DynamicObject();
            so->setProperty ("id", juce::String (sg.id));
            so->setProperty ("kind", juce::String (sg.kind));
            so->setProperty ("icon", juce::String (sg.icon));
            so->setProperty ("title", juce::String::fromUTF8 (sg.title.c_str()));
            so->setProperty ("text", juce::String::fromUTF8 (sg.text.c_str()));
            so->setProperty ("need", juce::String::fromUTF8 (sg.need.c_str()));
            so->setProperty ("steps", list (sg.steps));
            so->setProperty ("tracks", list (sg.tracks));
            sugs.add (juce::var (so));
        }
        r->setProperty ("suggestions", sugs);
        o->setProperty ("report", juce::var (r));
    }
    return juce::var (o);
}

juce::var BridgeHub::roleCurves (double sampleRate) const
{
    using namespace mixology;
    std::array<std::array<double, kBands>, mix::kGroupCount> energy {};
    std::array<bool, mix::kGroupCount> any {};
    const auto ms = link::nowMs();
    for (const auto& l : link::processRegistry().links (ms))
    {
        if (! l.hasAnalysis || l.analysis.silent || ms - std::min (ms, l.analysisMs) > 500) continue;
        const auto hash = l.identity.hasName ? l.nameHash : 0u;
        if (auto it = trackSettings.find (hash); it != trackSettings.end() && it->second.excluded) continue;
        const auto role = roleFor (hash, l.identity.name).value_or (mix::Role::other);
        const auto g = static_cast<std::size_t> (mix::groupOf (role));
        double fader = 0.0;
        if (auto it = bridge.knownFaders().find (hash); hash != 0 && it != bridge.knownFaders().end()) fader = std::max (it->second.db, -200.0);
        any[g] = true;
        for (int b = 0; b < kBands; ++b)
            energy[g][static_cast<std::size_t> (b)] += std::pow (10.0, (l.analysis.bandDb[static_cast<std::size_t> (b)] + fader) / 10.0);
    }

    // Same units as the mix spectrum (per FFT bin of the 8192-point display, tilted 4.5 dB per octave).
    juce::Array<juce::var> out;
    const double binHz = std::max (8000.0, sampleRate) / 8192.0;
    for (int g = 0; g < mix::kGroupCount; ++g)
    {
        if (! any[static_cast<std::size_t> (g)]) { out.add (juce::var()); continue; }
        juce::Array<juce::var> curve;
        for (int b = 0; b < kBands; ++b)
        {
            const double hz = bandHz (b);
            const double e = energy[static_cast<std::size_t> (g)][static_cast<std::size_t> (b)];
            const double db = e > 1.0e-14 ? 10.0 * std::log10 (e) - 10.0 * std::log10 (std::max (1.0, 0.2316 * hz / binHz)) + 4.5 * std::log2 (hz / 1000.0) : -120.0;
            curve.add (std::round (db * 10.0) / 10.0);
        }
        out.add (curve);
    }
    return out;
}

juce::var BridgeHub::getState (bool includeLog)
{
    using mixology::link::MatchStatus;

    auto* o = new juce::DynamicObject();
    o->setProperty ("enabled", enabled);
    o->setProperty ("link", mixology::toString (bridge.link()));
    o->setProperty ("portProblem", portProblem);
    o->setProperty ("script", bridge.scriptVersion());
    o->setProperty ("toCubase", kToCubase);
    o->setProperty ("fromCubase", kFromCubase);

    int paired = 0;
    juce::Array<juce::var> slots;
    for (int i = 0; i < bridge.slotCount(); ++i)
    {
        const auto& s = bridge.slot (i);
        auto* so = new juce::DynamicObject();
        so->setProperty ("present", s.present);
        so->setProperty ("title", juce::String::fromUTF8 (s.title.c_str()));
        so->setProperty ("hash", static_cast<juce::int64> (s.hash));
        so->setProperty ("hasVolume", s.hasVolume);
        so->setProperty ("volume", s.volume);
        so->setProperty ("volumeText", juce::String::fromUTF8 (s.volumeText.c_str()));
        so->setProperty ("volumeDb", s.volumeDb <= mixology::FaderLaw::kMinusInf ? juce::var() : juce::var (s.volumeDb));
        so->setProperty ("hasPan", s.hasPan);
        so->setProperty ("pan", s.pan);
        so->setProperty ("panText", juce::String::fromUTF8 (s.panText.c_str()));
        so->setProperty ("mute", s.mute);
        so->setProperty ("solo", s.solo);
        so->setProperty ("read", s.automationRead);
        so->setProperty ("write", s.automationWrite);
        so->setProperty ("colour", s.hasColour ? juce::var (juce::String::formatted ("#%06X", s.rgb)) : juce::var());
        so->setProperty ("protected", s.present && bridge.isProtected (s.hash));
        so->setProperty ("duplicate", bridge.nameIsDuplicated (i));
        so->setProperty ("moving", bridge.moveInProgress (i));

        bool changed = false;
        if (s.present)
            if (auto it = bridge.restorePoints().find (s.hash); it != bridge.restorePoints().end())
            {
                int at = -1;
                changed = bridge.isChanged (it->second, &at) && at == i;
            }
        so->setProperty ("changed", changed);
        if (s.present)
        {
            if (auto role = roleFor (s.hash, s.title)) so->setProperty ("role", mixology::mix::roleKey (*role));
            const auto ts = trackSettings.find (s.hash);
            so->setProperty ("roleSet", ts != trackSettings.end() && ts->second.role.has_value());
            so->setProperty ("excluded", ts != trackSettings.end() && ts->second.excluded);
        }

        const auto sl = i < static_cast<int> (stripLinks.size()) ? stripLinks[static_cast<std::size_t> (i)] : StripLink {};
        so->setProperty ("linkStatus", sl.status == MatchStatus::matched ? "matched" : sl.status == MatchStatus::ambiguous ? "ambiguous" : "none");
        if (sl.status == MatchStatus::matched)
        {
            ++paired;
            so->setProperty ("trim", std::round (sl.trimDb * 10.0f) / 10.0f);
            so->setProperty ("linkAudio", sl.audio);
        }
        slots.add (juce::var (so));
    }
    o->setProperty ("slots", slots);
    o->setProperty ("changedCount", bridge.changedCount());
    o->setProperty ("restorePoints", static_cast<int> (bridge.restorePoints().size()));
    o->setProperty ("linksSeen", linksSeen);
    o->setProperty ("linksPaired", paired);
    o->setProperty ("linksShared", mixology::link::processRegistryShared());
    o->setProperty ("lastMove", juce::String::fromUTF8 (bridge.lastMoveResult().c_str()));
    o->setProperty ("scanning", bridge.scanning());
    o->setProperty ("analysis", analysisState());
    o->setProperty ("lawPoints", static_cast<int> (bridge.faderLaw().size()));

    // Where the fader scale marks sit on Cubase's fader (learned curve; a rough guess until learned).
    juce::Array<juce::var> marks;
    for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -36.0, -48.0 })
    {
        auto* mk = new juce::DynamicObject();
        mk->setProperty ("db", db);
        mk->setProperty ("pos", bridge.faderLaw().toNormalised (db));
        marks.add (juce::var (mk));
    }
    o->setProperty ("marks", marks);

    o->setProperty ("logVersion", static_cast<juce::int64> (bridge.logVersion()));
    if (includeLog)
    {
        juce::Array<juce::var> lines;
        const auto& log = bridge.logLines();
        const auto first = log.size() > kLogLinesForUi ? log.size() - kLogLinesForUi : 0;
        for (auto i = first; i < log.size(); ++i)
            lines.add (stamp (log[i].time) + "  " + juce::String::fromUTF8 (log[i].text.c_str()));
        o->setProperty ("log", lines);
    }
    return juce::var (o);
}

juce::var BridgeHub::command (const juce::String& name, const juce::var& arg)
{
    using Result = mixology::BridgeSession::Result;
    const double t = now();
    Result r { true, {} };
    std::string info;   // shown to the user even when all went well

    mixology::ChannelRef ref;
    ref.slot = static_cast<int> (arg.getProperty ("slot", -1));
    ref.hash = static_cast<std::uint32_t> (static_cast<juce::int64> (arg.getProperty ("hash", 0)));
    const auto value = arg.isObject() ? arg.getProperty ("value", 0) : arg;
    const double number = value.isVoid() ? 0.0 : static_cast<double> (value);

    if (name == "setDb")          r = bridge.setVolumeDb (ref, number, t);
    else if (name == "minusInf")  r = bridge.setVolumeDb (ref, mixology::FaderLaw::kMinusInf, t);
    else if (name == "nudge")     r = bridge.nudgeDb (ref, number, t);
    else if (name == "fader")     r = bridge.setVolumeNormalised (ref, number, t);
    else if (name == "faderEnd")  bridge.noteDragFinished (ref, t);
    else if (name == "pan")       r = bridge.setPan (ref, number, t);
    else if (name == "mute")      r = bridge.setMute (ref, static_cast<bool> (value), t);
    else if (name == "solo")      r = bridge.setSolo (ref, static_cast<bool> (value), t);
    else if (name == "protect")
    {
        r = bridge.setProtected (ref, static_cast<bool> (value), t);
        cacheProtected();
    }
    else if (name == "restore")   r = bridge.restoreChannel (ref, t);
    else if (name == "restoreAll")
    {
        info = bridge.restoreAll (t).message;
    }
    else if (name == "forget")    bridge.forgetRestorePoints (t);
    else if (name == "analyze")
    {
        const auto& st = value;
        mixology::mix::Style style;
        style.key = st.getProperty ("key", "hiphop.modern").toString().toStdString();
        style.name = st.getProperty ("name", "Hip Hop - Modern Clean").toString().toStdString();
        style.bass = st.getProperty ("bass", 4.0);
        style.scoop = st.getProperty ("scoop", 1.5);
        style.air = st.getProperty ("air", 2.0);
        if (mixology::link::processRegistry().links (mixology::link::nowMs()).empty())
            r = { false, "No Mixology Link found. Insert Mixology Link on your tracks (after their other plug-ins), then press Analyze Mix." };
        else
        {
            analysis.begin (style);
            if (bridge.link() == mixology::BridgeSession::Link::connected && bridge.scanAllChannels (t).ok)
                analyzeState = Analyze::reading;   // first every channel's fader, then listen
            else
                analyzeState = Analyze::listening;
            ++analysisVersion;
        }
    }
    else if (name == "analyzeFinish")
    {
        if (! analysis.hasMoments()) r = { false, "Nothing heard yet: press play in Cubase." };
        else finishAnalysis();
    }
    else if (name == "analyzeCancel")
    {
        analyzeState = report.valid ? Analyze::done : Analyze::idle;
        ++analysisVersion;
    }
    else if (name == "setRole" || name == "setExcluded")
    {
        const auto hash = static_cast<std::uint32_t> (static_cast<juce::int64> (arg.getProperty ("hash", 0)));
        if (hash == 0) r = { false, "That track has no name Mixology can keep track of." };
        else
        {
            auto& ts = trackSettings[hash];
            if (name == "setRole") ts.role = mixology::mix::roleFromKey (value.toString().toStdString());
            else ts.excluded = static_cast<bool> (value);
            cacheTrackSettings();
            if (report.valid && analysis.hasMoments()) report = analysis.evaluate (trackSettings, report.unheard);
            ++analysisVersion;
        }
    }
    else if (name == "trim")
    {
        const bool valid = ref.slot >= 0 && ref.slot < bridge.slotCount() && ref.slot < static_cast<int> (stripLinks.size())
                           && bridge.slot (ref.slot).present && bridge.slot (ref.slot).hash == ref.hash;
        const auto& sl = valid ? stripLinks[static_cast<std::size_t> (ref.slot)] : StripLink {};
        if (! valid) r = { false, "That strip shows another channel now." };
        else if (bridge.isProtected (ref.hash)) r = { false, "That channel is protected (padlock)." };
        else if (sl.status != mixology::link::MatchStatus::matched) r = { false, "No Mixology Link is paired with this channel." };
        else mixology::link::processRegistry().requestTrim (sl.slot, sl.generation, static_cast<float> (juce::jlimit (-24.0, 24.0, number)));
    }
    else if (name == "bank")      r = bridge.bank (static_cast<mixology::bridge::BankAction> (juce::jlimit (0, 4, static_cast<int> (number))), t);
    else if (name == "refresh")   bridge.requestDump (t);
    else if (name == "reconnect")
    {
        closePorts ("Reconnect pressed");
        portProblem.clear();
        openPorts();
    }
    else if (name == "copyLog")   juce::SystemClipboard::copyTextToClipboard (logText());
    else                          r = { false, "Unknown command." };

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", r.ok);
    o->setProperty ("message", juce::String::fromUTF8 (r.message.c_str()));
    o->setProperty ("warning", juce::String::fromUTF8 (r.warning.c_str()));
    o->setProperty ("info", juce::String::fromUTF8 (info.c_str()));
    return juce::var (o);
}
