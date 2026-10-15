#include "BridgeHub.h"

#include <regex>
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

    juce::var stringList (const std::vector<std::string>& items)
    {
        juce::Array<juce::var> a;
        for (const auto& s : items) a.add (juce::String::fromUTF8 (s.c_str()));
        return a;
    }

    juce::var suggestionVar (const mixology::mix::Suggestion& sg)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("id", juce::String (sg.id));
        so->setProperty ("kind", juce::String (sg.kind));
        so->setProperty ("icon", juce::String (sg.icon));
        so->setProperty ("title", juce::String::fromUTF8 (sg.title.c_str()));
        so->setProperty ("text", juce::String::fromUTF8 (sg.text.c_str()));
        so->setProperty ("need", juce::String::fromUTF8 (sg.need.c_str()));
        so->setProperty ("steps", stringList (sg.steps));
        so->setProperty ("tracks", stringList (sg.tracks));
        return juce::var (so);
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
                ts.reference = f[3] == "1";
                ts.musicBus = f[4] == "1";
                ts.noPan = f[5] == "1";
            }
            std::vector<std::uint32_t> noPan;   // the channels Cubase doesn't pan, as this project learned them
            for (const auto& [hash, ts] : trackSettings)
                if (ts.noPan) noPan.push_back (hash);
            bridge.setNoPanList (noPan);
            seenNoPanVersion = bridge.noPanVersion();
            cacheTrackSettings();
            ++analysisVersion;
        }
    }
    syncNoPan();

    {
        juce::String pending;
        bool has = false;
        {
            const juce::ScopedLock sl (protectedLock);
            if (hasPendingPrefs) { pending = prefsPending; has = true; hasPendingPrefs = false; }
        }
        if (has)
        {
            juce::StringPairArray kv;
            for (const auto& item : juce::StringArray::fromTokens (pending, ";", ""))
                kv.set (item.upToFirstOccurrenceOf ("=", false, false).trim(), item.fromFirstOccurrenceOf ("=", false, false).trim());
            vocalOffset = juce::jlimit (-24.0, 24.0, kv["vocalOffset"].getDoubleValue());
            vocalFromYou = kv["vocalFromYou"] == "1";
            analysis.setVocalOffset (vocalOffset, vocalFromYou);
            reevaluate();
            cachePrefs();
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
    if (bridge.planProgress().serial != seenPlanSerial)
    {
        seenPlanSerial = bridge.planProgress().serial;
        if (lastApply.valid && lastApply.waitingForBridge && seenPlanSerial >= lastApply.bridgeSerial)
        {
            lastApply.waitingForBridge = false;
            lastApply.bridgeUndo = bridge.planProgress().undo;
        }
        ++applyVersion;
    }

    if (t - lastLinkUpdate >= 1.0 / 30.0)
    {
        lastLinkUpdate = t;
        updateLinks();
    }
    if (t - lastSuiteUpdate >= 0.25)
    {
        lastSuiteUpdate = t;
        updateSuite (t);
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

// ---------------------------------------------------------------------------------------------
// Presets (yours): one text file beside Mixology's settings, shared by every project and every Mixology.

juce::File BridgeHub::userPresetFile() const
{
    return settingsFile->getFile().getSiblingFile ("Mixology Presets.txt");
}

const std::vector<mixology::presets::Preset>& BridgeHub::userPresets()
{
    if (! userPresetsLoaded)
    {
        userPresetsLoaded = true;
        const auto file = userPresetFile();
        if (file.existsAsFile()) userPresetList = mixology::presets::parse (file.loadFileAsString().toStdString());
    }
    return userPresetList;
}

void BridgeHub::saveUserPreset (const mixology::presets::Preset& p)
{
    userPresets();
    auto it = std::find_if (userPresetList.begin(), userPresetList.end(), [&p] (const auto& q) { return q.name == p.name; });
    if (it != userPresetList.end()) *it = p;
    else userPresetList.push_back (p);
    std::sort (userPresetList.begin(), userPresetList.end(), [] (const auto& a, const auto& b) { return juce::String (a.name).compareNatural (b.name) < 0; });
    const auto file = userPresetFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::String::fromUTF8 (mixology::presets::serialise (userPresetList).c_str()));
}

bool BridgeHub::deleteUserPreset (const std::string& name)
{
    userPresets();
    const auto before = userPresetList.size();
    userPresetList.erase (std::remove_if (userPresetList.begin(), userPresetList.end(), [&name] (const auto& q) { return q.name == name; }), userPresetList.end());
    if (userPresetList.size() == before) return false;
    userPresetFile().replaceWithText (juce::String::fromUTF8 (mixology::presets::serialise (userPresetList).c_str()));
    return true;
}

void BridgeHub::rememberPresetUndo (const juce::String& key, const std::string& name, const mixology::proc::Values& before)
{
    presetUndo[key] = { name, before };
}

bool BridgeHub::takePresetUndo (const juce::String& key, std::string& name, mixology::proc::Values& before)
{
    const auto it = presetUndo.find (key);
    if (it == presetUndo.end()) return false;
    name = it->second.name;
    before = it->second.before;
    presetUndo.erase (it);
    return true;
}

std::string BridgeHub::presetUndoName (const juce::String& key) const
{
    const auto it = presetUndo.find (key);
    return it == presetUndo.end() ? std::string() : it->second.name;
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
            sl.channels = l.channels;
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

juce::String BridgeHub::mixPrefsState() const
{
    const juce::ScopedLock sl (protectedLock);
    return hasPendingPrefs ? prefsPending : prefsCache;
}

void BridgeHub::setMixPrefsState (const juce::String& text)
{
    const juce::ScopedLock sl (protectedLock);
    prefsPending = text;
    hasPendingPrefs = true;   // applied on the message thread (next timer tick)
}

void BridgeHub::cachePrefs()
{
    const auto text = "vocalOffset=" + juce::String (vocalOffset, 2) + ";vocalFromYou=" + (vocalFromYou ? "1" : "0");
    const juce::ScopedLock sl (protectedLock);
    prefsCache = text;
}

bool BridgeHub::audibleNow (std::uint32_t hash) const
{
    return hash == 0 || bridge.isAudible (hash);
}

void BridgeHub::syncNoPan()
{
    // The bridge learned (or forgot) a channel Cubase doesn't pan: keep it with the project's track settings.
    if (bridge.noPanVersion() == seenNoPanVersion) return;
    seenNoPanVersion = bridge.noPanVersion();
    for (auto& [hash, ts] : trackSettings) ts.noPan = false;
    for (const auto hash : bridge.noPanList()) trackSettings[hash].noPan = true;
    cacheTrackSettings();
}

void BridgeHub::cacheTrackSettings()
{
    juce::StringArray lines;
    for (const auto& [hash, ts] : trackSettings)
        if (ts.role || ts.excluded || ts.reference || ts.musicBus || ts.noPan)
            lines.add (juce::String (static_cast<juce::int64> (hash)) + "\t" + (ts.role ? mixology::mix::roleKey (*ts.role) : "") + "\t" + (ts.excluded ? "1" : "0")
                       + "\t" + (ts.reference ? "1" : "0") + "\t" + (ts.musicBus ? "1" : "0") + "\t" + (ts.noPan ? "1" : "0"));
    const juce::ScopedLock sl (protectedLock);
    trackSettingsCache = lines.joinIntoString ("\n");
}

// ---------------------------------------------------------------------------------------------
// Analyze Mix

std::optional<mixology::mix::Role> BridgeHub::roleFor (std::uint32_t hash, const std::string& name) const
{
    if (auto it = trackSettings.find (hash); it != trackSettings.end() && it->second.role) return it->second.role;
    if (auto vox = voxRole (hash, name)) return vox;
    for (const auto& t : report.tracks)
        if (t.hash == hash) return t.role;
    return mixology::mix::roleFromName (name);
}

void BridgeHub::analysisTick (const mixology::AnalysisFrame& mixFrame, bool playing, int channels)
{
    using namespace mixology;
    if (playing != transportPlaying) { transportPlaying = playing; ++analysisVersion; }
    if (analyzeState == Analyze::reading && ! bridge.scanning()) { analyzeState = Analyze::listening; ++analysisVersion; }
    mixChannels = channels;

    const auto ms = link::nowMs();
    std::vector<mix::TrackSample> tracks;
    std::vector<setup::TrackNow> setupTracks;
    for (const auto& l : link::processRegistry().links (ms))
    {
        if (! l.hasAnalysis || ms - std::min (ms, l.analysisMs) > 500) continue;
        mix::TrackSample t;
        t.hash = l.identity.hasName ? l.nameHash : 0u;
        t.name = l.identity.hasName ? l.identity.name : "Unnamed track " + std::to_string (l.slot + 1);
        // A muted (or soloed-out) channel isn't in the mix, though its Link still hears it. The reference song is
        // muted on purpose: Reference Match needs it.
        const auto ts = trackSettings.find (t.hash);
        if (! audibleNow (t.hash) && (ts == trackSettings.end() || ! ts->second.reference)) continue;
        if (t.hash != 0)
            if (auto it = bridge.knownFaders().find (t.hash); it != bridge.knownFaders().end())
            {
                t.faderDb = std::max (it->second.db, -200.0);
                t.faderKnown = true;
            }
        t.frame = l.analysis;

        // The setup check: what this track should give the mix's width (its own width, or its pan if it's mono).
        setup::TrackNow s;
        s.name = t.name;
        s.loudness = t.frame.silent ? kSilentDb : t.frame.loudness;
        s.width = t.frame.width;
        s.faderDb = t.faderKnown ? t.faderDb : 0.0;
        if (t.hash != 0 && l.channels == 1)   // a stereo track's pan reads its left edge only (Combined Panner)
            if (auto it = bridge.knownFaders().find (t.hash); it != bridge.knownFaders().end() && it->second.hasPan)
            {
                s.panKnown = true;
                s.pan = (it->second.pan - 0.5) * 200.0;
                s.panWorks = ! bridge.panIgnored (t.hash);
            }
        setupTracks.push_back (std::move (s));
        tracks.push_back (std::move (t));
    }
    if (static_cast<int> (tracks.size()) != tracksHeard) { tracksHeard = static_cast<int> (tracks.size()); ++analysisVersion; }

    setup::MixNow heard;
    heard.playing = playing;
    heard.loudness = mixFrame.silent ? kSilentDb : mixFrame.loudness;
    heard.width = mixFrame.width;
    heard.correlation = mixFrame.correlation;
    monoWatch.add (heard, setupTracks);
    updateSetup();

    if (analyzeState != Analyze::listening || ! playing) return;
    mix::Moment m;
    m.mix = mixFrame;
    m.tracks = std::move (tracks);
    analysis.add (std::move (m));
    ++analysisVersion;
    if (analysis.secondsHeard() >= 60.0 || analysis.full()) finishAnalysis();
}

void BridgeHub::restoreAssist (std::uint32_t hash, int& restored, int& kept)
{
    auto& registry = mixology::link::processRegistry();
    const auto links = registry.links (mixology::link::nowMs());
    std::vector<std::uint32_t> done;
    for (const auto& [h, point] : eqRestore.all())
    {
        if (hash != 0 && h != hash) continue;
        if (bridge.isProtected (h)) continue;   // protected: never touched
        const auto* l = uniqueLink (links, h);
        if (l == nullptr) continue;             // its Link isn't there right now: keep the point
        const auto d = eqRestore.decide (h, l->assist);
        if (d.action == mixology::AssistRestore::Action::restore) { registry.requestAssist (l->slot, l->generation, d.to); ++restored; }
        else if (d.action == mixology::AssistRestore::Action::keptYours) ++kept;
        done.push_back (h);
    }
    for (auto h : done) eqRestore.forget (h);
}

int BridgeHub::changedTotal (int* faders, int* eq) const
{
    std::set<std::uint32_t> all;
    int f = 0;
    for (const auto& [h, p] : bridge.restorePoints())
        if (bridge.isChanged (p)) { all.insert (h); ++f; }
    for (const auto& [h, p] : eqRestore.all()) all.insert (h);
    if (faders != nullptr) *faders = f;
    if (eq != nullptr) *eq = static_cast<int> (eqRestore.size());
    return static_cast<int> (all.size());
}

void BridgeHub::applyMusicBus()
{
    for (const auto& [hash, ts] : trackSettings)
        if (ts.musicBus)
        {
            double db = 0.0;
            if (auto it = fadersAtAnalysis.find (hash); it != fadersAtAnalysis.end()) db = it->second;
            else if (auto kf = bridge.knownFaders().find (hash); kf != bridge.knownFaders().end()) db = kf->second.db;
            analysis.setMusicBus (true, std::max (db, -120.0));
            return;
        }
    analysis.setMusicBus (false, 0.0);
}

void BridgeHub::reevaluate()
{
    if (! report.valid || ! analysis.hasMoments()) return;
    applyMusicBus();
    // The music group isn't a channel of its own music: never "without a Link".
    std::vector<std::string> unheard;
    for (const auto& n : report.unheard)
    {
        const auto ts = trackSettings.find (mixology::bridge::nameHash (n));
        if (ts == trackSettings.end() || (! ts->second.musicBus && ! ts->second.excluded)) unheard.push_back (n);
    }
    report = analysis.evaluate (settingsForAnalysis(), unheard);
}

// ---------------------------------------------------------------------------------------------
// M6: the suite link. Every Voxology publishes the track it sits on and what it does there; every Masterology
// its goal. Mixology reads both a few times a second (and publishes its hand-off from the plug-in, see
// MixologyAudioProcessor::publishHandoff).

namespace
{
    const char* voxStyleName (suite::VocalInfo::Style s)
    {
        switch (s)
        {
            case suite::VocalInfo::Style::trapLead: return "Trap Lead";
            case suite::VocalInfo::Style::rap: return "Rap";
            case suite::VocalInfo::Style::melodic: return "Melodic";
            case suite::VocalInfo::Style::robot: return "Robot";
            case suite::VocalInfo::Style::rnb: return "R&B";
            case suite::VocalInfo::Style::pop: return "Pop";
            case suite::VocalInfo::Style::folk: return "Folk";
            case suite::VocalInfo::Style::naturalSinger: return "Natural Singer";
            case suite::VocalInfo::Style::unknown: break;
        }
        return "";
    }
}

void BridgeHub::updateSuite (double t)
{
    auto& link = suite::Link::process();
    const auto ms = suite::nowMs();
    std::map<std::uint32_t, suite::VocalInfo> merged;   // two Voxologys on one channel: what both do
    for (const auto& v : link.vocals (ms))
    {
        if (v.nameHash == 0) continue;
        auto [it, fresh] = merged.try_emplace (v.nameHash, v);
        if (fresh) continue;
        auto& m = it->second;
        m.flags |= v.flags;
        m.compressionDb = std::max (m.compressionDb, v.compressionDb);
        m.reverbMix = std::max (m.reverbMix, v.reverbMix);
        m.delayMix = std::max (m.delayMix, v.delayMix);
        if (m.style == suite::VocalInfo::Style::unknown) m.style = v.style;
    }
    for (const auto& [hash, v] : merged) suiteVocals[hash] = { v, t };
    // One that stops reporting stays a few seconds: a busy message thread holds up every plug-in's heartbeat.
    for (auto it = suiteVocals.begin(); it != suiteVocals.end();)
        it = t - it->second.lastSeen > 5.0 ? suiteVocals.erase (it) : std::next (it);
    suiteMasters = link.masters (ms);

    std::string roles, key;
    for (const auto& [hash, sv] : suiteVocals)
    {
        const auto role = voxRole (hash, sv.info.trackName);
        roles += std::to_string (hash) + ":" + (role ? mixology::mix::roleKey (*role) : "-") + ";";
        key += std::to_string (hash) + ":" + std::to_string (sv.info.flags) + ":" + std::to_string (static_cast<int> (sv.info.style)) + ";";
    }
    for (const auto& m : suiteMasters)
        key += "|" + m.goalName + "|" + m.genreName + "|" + std::to_string (std::lround (m.targetLufs * 10.0f)) + "|" + std::to_string (std::lround (m.ceilingDbtp * 10.0f));
    if (key + roles != suiteKey)
    {
        suiteKey = key + roles;
        ++suiteVersion;
    }
    if (roles != suiteRoleKey)
    {
        suiteRoleKey = roles;
        reevaluate();   // a vocal Voxology sits on is a vocal in the advice too
        ++analysisVersion;
    }
}

void BridgeHub::updateSetup()
{
    using namespace mixology;
    setup::Context c;
    c.mixChannels = mixChannels;
    c.monoFold = monoWatch.monoFold();
    c.stereoTracks = monoWatch.stereoTracks();

    std::set<std::uint32_t> linked;
    for (const auto& l : link::processRegistry().links (link::nowMs()))
        if (l.identity.hasName) linked.insert (l.nameHash);
    auto titleOf = [this] (std::uint32_t hash, const std::string& fallback) {
        const auto it = bridge.knownFaders().find (hash);
        return it != bridge.knownFaders().end() && ! it->second.title.empty() ? it->second.title : fallback;
    };

    if (c.monoFold)
    {
        // The mono group it most likely is: the music group you marked, a Voxology in BEAT mode (it sits on the beat's
        // group), the channels Analyze Mix found adding no sound of their own, then channels named like a group.
        auto add = [&c] (const std::string& n) {
            if (! n.empty() && std::find (c.busCandidates.begin(), c.busCandidates.end(), n) == c.busCandidates.end()) c.busCandidates.push_back (n);
        };
        for (const auto& [hash, ts] : trackSettings)
            if (ts.musicBus) add (titleOf (hash, ""));
        for (const auto& [hash, sv] : suiteVocals)
            if ((sv.info.flags & suite::VocalInfo::beat) != 0) add (titleOf (hash, sv.info.trackName));
        if (report.valid)
            for (const auto& g : report.groupHints) add (g);
        static const std::regex groupName (R"(\b(group|grp|bus|beat|instrumental|music|submix|stems?)\b)", std::regex::icase);
        for (const auto& [hash, f] : bridge.knownFaders())
            if (linked.count (hash) == 0 && voxologyOn (hash) == nullptr && std::regex_search (f.title, groupName)) add (f.title);
    }

    for (const auto& [hash, sv] : suiteVocals)
        if (voxologyOn (hash) != nullptr && linked.count (hash) == 0)
            c.vocalsWithoutLink.push_back (titleOf (hash, sv.info.trackName));

    setupNow = setup::issues (c);
    std::string key;
    for (const auto& s : setupNow)
    {
        key += s.id + "|" + s.title + "|";
        for (const auto& t : s.tracks) key += t + ",";
    }
    if (key != setupKey)
    {
        setupKey = key;
        ++setupVersion;
    }
}

const suite::VocalInfo* BridgeHub::voxologyOn (std::uint32_t hash) const
{
    const auto it = suiteVocals.find (hash);
    if (it == suiteVocals.end() || (it->second.info.flags & suite::VocalInfo::beat) != 0) return nullptr;
    return &it->second.info;
}

std::optional<mixology::mix::Role> BridgeHub::voxRole (std::uint32_t hash, const std::string& name) const
{
    using mixology::mix::Role;
    const auto* v = voxologyOn (hash);
    if (v == nullptr) return std::nullopt;
    // Voxology says it is a vocal; the channel's name says whether it is the ad-libs.
    return mixology::mix::roleFromName (name) == Role::adlibs ? Role::adlibs : Role::vox;
}

std::map<std::uint32_t, mixology::mix::TrackSettings> BridgeHub::settingsForAnalysis() const
{
    auto settings = trackSettings;
    for (const auto& [hash, sv] : suiteVocals)
        if (auto role = voxRole (hash, sv.info.trackName)) settings[hash].fromVoxology = role;
    return settings;
}

bool BridgeHub::handoffTone (const std::string& styleKey, suite::MixInfo& m) const
{
    if (! report.valid || report.style.key != styleKey) return false;
    const auto tone = mixology::mix::toneAgainstStyle (report);
    if (! tone.valid) return false;
    m.flags |= suite::MixInfo::analysed;
    if (report.monoMix) m.flags |= suite::MixInfo::monoMix;
    m.lowDb = static_cast<float> (tone.low);
    m.lowMidDb = static_cast<float> (tone.lowMid);
    m.presenceDb = static_cast<float> (tone.presence);
    m.airDb = static_cast<float> (tone.air);
    return true;
}

std::optional<suite::MasterInfo> BridgeHub::masterology() const
{
    if (suiteMasters.empty()) return std::nullopt;
    return suiteMasters.front();
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
        if (heard.count (hash) == 0 && f.db > mixology::FaderLaw::kMinusInf + 1.0
            && (ts == trackSettings.end() || (! ts->second.excluded && ! ts->second.musicBus)))
            unheard.push_back (f.title);
    }

    fadersAtAnalysis.clear();
    for (const auto& [hash, f] : bridge.knownFaders()) fadersAtAnalysis[hash] = f.db;
    // The processing each Link (and the Full Mix) had while Mixology listened: Polish works from there.
    procAtAnalysis.clear();
    {
        auto& registry = mixology::link::processRegistry();
        const auto links = registry.links (mixology::link::nowMs());
        for (const auto& l : links)
        {
            mixology::proc::Values v {};
            if (l.identity.hasName && uniqueLink (links, l.nameHash) != nullptr && registry.readProc (l.slot, l.generation, v))
                procAtAnalysis[l.nameHash] = v;
        }
    }
    mixProcAtAnalysisKnown = static_cast<bool> (mixProc.get);
    if (mixProcAtAnalysisKnown) mixProcAtAnalysis = mixProc.get();
    applyMusicBus();
    report = analysis.evaluate (settingsForAnalysis(), unheard);
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
            to->setProperty ("reference", t.reference);
            tracks.add (juce::var (to));
        }
        r->setProperty ("tracks", tracks);
        r->setProperty ("mixWidth", std::round (report.mixWidth * 100.0) / 100.0);
        r->setProperty ("routingFromSound", report.routingFromSound);
        r->setProperty ("vocalPathDb", std::round (report.vocalPathDb * 10.0) / 10.0);
        r->setProperty ("musicPathDb", std::round (report.musicPathDb * 10.0) / 10.0);
        r->setProperty ("hasVocal", report.hasVocalRatio);
        r->setProperty ("vocalRatio", std::round (report.vocalRatio * 10.0) / 10.0);
        r->setProperty ("vocalTarget", std::round (report.vocalTarget * 10.0) / 10.0);
        r->setProperty ("vocalFromYou", report.style.vocalFromYou);

        const auto& ref = report.reference;
        auto* ro = new juce::DynamicObject();
        ro->setProperty ("chosen", ref.chosen);
        ro->setProperty ("heard", ref.heard);
        ro->setProperty ("hash", static_cast<juce::int64> (ref.hash));
        ro->setProperty ("name", juce::String::fromUTF8 (ref.name.c_str()));
        ro->setProperty ("loudness", std::round (ref.loudness * 10.0) / 10.0);
        ro->setProperty ("loudnessDiff", std::round (ref.loudnessDiff * 10.0) / 10.0);
        ro->setProperty ("width", std::round (ref.width * 100.0) / 100.0);
        ro->setProperty ("widthDiff", std::round (ref.widthDiff * 100.0) / 100.0);
        ro->setProperty ("bands", numbers (ref.bands));
        juce::Array<juce::var> regions;
        for (const auto& rg : ref.regions)
        {
            auto* g = new juce::DynamicObject();
            g->setProperty ("name", juce::String::fromUTF8 (rg.name.c_str()));
            g->setProperty ("diff", std::round (rg.diff * 10.0) / 10.0);
            g->setProperty ("text", juce::String::fromUTF8 (rg.text.c_str()));
            regions.add (juce::var (g));
        }
        ro->setProperty ("regions", regions);
        r->setProperty ("reference", juce::var (ro));

        juce::Array<juce::var> sugs;
        for (const auto& sg : report.suggestions) sugs.add (suggestionVar (sg));
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
        if (auto it = trackSettings.find (hash); it != trackSettings.end() && (it->second.excluded || it->second.reference || it->second.musicBus)) continue;
        if (! audibleNow (hash)) continue;   // muted: not in the mix, though its Link still hears it
        const auto role = roleFor (hash, l.identity.name).value_or (mix::Role::other);
        const auto g = static_cast<std::size_t> (mix::groupOf (role));
        double fader = 0.0;
        if (auto it = bridge.knownFaders().find (hash); hash != 0 && it != bridge.knownFaders().end()) fader = std::max (it->second.db, -200.0);
        if (mix::viaMusicBus (role))
            for (const auto& [bh, bts] : trackSettings)
                if (bts.musicBus)
                    if (auto it = bridge.knownFaders().find (bh); it != bridge.knownFaders().end()) fader += std::max (it->second.db, -200.0);
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
        so->setProperty ("changed", changed || (s.present && eqRestore.has (s.hash)));
        if (s.present)
        {
            if (auto role = roleFor (s.hash, s.title)) so->setProperty ("role", mixology::mix::roleKey (*role));
            const auto ts = trackSettings.find (s.hash);
            so->setProperty ("roleSet", ts != trackSettings.end() && ts->second.role.has_value());
            so->setProperty ("excluded", ts != trackSettings.end() && ts->second.excluded);
            so->setProperty ("musicBus", ts != trackSettings.end() && ts->second.musicBus);
            if (auto sv = suiteVocals.find (s.hash); sv != suiteVocals.end())
            {
                // Voxology on this channel: what it does there (Polish leaves those to it).
                const auto& v = sv->second.info;
                auto* vo = new juce::DynamicObject();
                auto has = [&v] (std::uint32_t f) { return (v.flags & f) != 0; };
                vo->setProperty ("beat", has (suite::VocalInfo::beat));
                vo->setProperty ("on", has (suite::VocalInfo::processing));
                vo->setProperty ("style", voxStyleName (v.style));
                vo->setProperty ("comp", has (suite::VocalInfo::compressor));
                vo->setProperty ("compDb", std::round (v.compressionDb * 10.0f) / 10.0f);
                vo->setProperty ("deEsser", has (suite::VocalInfo::deEsser));
                vo->setProperty ("reverb", has (suite::VocalInfo::reverb));
                vo->setProperty ("delay", has (suite::VocalInfo::delay));
                vo->setProperty ("eq", has (suite::VocalInfo::eq));
                vo->setProperty ("pitch", has (suite::VocalInfo::pitch));
                vo->setProperty ("doubler", has (suite::VocalInfo::doubler));
                vo->setProperty ("version", juce::String (v.version >> 16) + "." + juce::String ((v.version >> 8) & 0xFF) + "." + juce::String (v.version & 0xFF));
                so->setProperty ("voxology", juce::var (vo));
            }
        }

        const auto sl = i < static_cast<int> (stripLinks.size()) ? stripLinks[static_cast<std::size_t> (i)] : StripLink {};
        so->setProperty ("linkStatus", sl.status == MatchStatus::matched ? "matched" : sl.status == MatchStatus::ambiguous ? "ambiguous" : "none");
        if (sl.status == MatchStatus::matched)
        {
            ++paired;
            so->setProperty ("trim", std::round (sl.trimDb * 10.0f) / 10.0f);
            so->setProperty ("linkAudio", sl.audio);
            // A stereo track reading exactly L100: Cubase's Stereo Combined Panner, whose left edge is all the
            // MIDI Remote reports (full width = "L100"). Its real position is unknown to Mixology.
            so->setProperty ("stereoPanner", sl.channels == 2 && s.hasPan && s.pan <= 0.0005);
        }
        // Cubase didn't follow a pan move here: no working pan (mono track into a mono group / output).
        so->setProperty ("noPan", s.present && bridge.panIgnored (s.hash));
        slots.add (juce::var (so));
    }
    o->setProperty ("slots", slots);
    o->setProperty ("mixMono", report.valid && report.monoMix);   // pans change nothing at the Stereo Out
    juce::Array<juce::var> setupList;   // the setup check: a mono group on the way, vocals without a Link...
    for (const auto& sg : setupNow) setupList.add (suggestionVar (sg));
    o->setProperty ("setup", setupList);
    int changedFaders = 0, changedEq = 0;
    o->setProperty ("changedCount", changedTotal (&changedFaders, &changedEq));
    o->setProperty ("changedFaders", changedFaders);
    o->setProperty ("changedEq", changedEq);
    o->setProperty ("restorePoints", static_cast<int> (bridge.restorePoints().size() + eqRestore.size()));
    o->setProperty ("linksSeen", linksSeen);
    o->setProperty ("linksPaired", paired);
    o->setProperty ("linksShared", mixology::link::processRegistryShared());
    o->setProperty ("lastMove", juce::String::fromUTF8 (bridge.lastMoveResult().c_str()));
    o->setProperty ("scanning", bridge.scanning());
    o->setProperty ("analysis", analysisState());
    o->setProperty ("apply", applyState());
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
    juce::var planResult;

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
    else if (name == "restore")
    {
        // The channel's fader / pan / mute / solo, and the Assist EQ Mixology set on its Link.
        int eqBack = 0, eqKept = 0;
        const bool valid = ref.slot >= 0 && ref.slot < bridge.slotCount() && bridge.slot (ref.slot).present && bridge.slot (ref.slot).hash == ref.hash;
        if (valid && ! bridge.isProtected (ref.hash)) restoreAssist (ref.hash, eqBack, eqKept);
        const bool hadFaderPoint = bridge.restorePoints().count (ref.hash) != 0;
        if (hadFaderPoint || eqBack + eqKept == 0) r = bridge.restoreChannel (ref, t);
        if (eqBack > 0) info = "Assist EQ back to how it was.";
        else if (eqKept > 0) info = "Its Assist EQ was changed since Mixology set it: left as you set it.";
        if (! hadFaderPoint && eqBack + eqKept > 0) r = { true, {} };
        ++applyVersion;
    }
    else if (name == "restoreAll")
    {
        const auto sum = bridge.restoreAll (t);
        int eqBack = 0, eqKept = 0;
        restoreAssist (0, eqBack, eqKept);
        const bool fadersDidNothing = sum.restored + sum.outsideBank + sum.skipped == 0;
        info = fadersDidNothing && eqBack + eqKept > 0 ? std::string() : sum.message;
        if (eqBack > 0) info += std::string (info.empty() ? "" : " ") + "Assist EQ back to how it was on " + std::to_string (eqBack) + " track" + (eqBack == 1 ? "" : "s") + ".";
        if (eqKept > 0) info += std::string (info.empty() ? "" : " ") + std::to_string (eqKept) + " Assist EQ" + (eqKept == 1 ? " was" : "s were") + " changed since: left as you set " + (eqKept == 1 ? "it." : "them.");
        ++applyVersion;
    }
    else if (name == "forget")
    {
        bridge.forgetRestorePoints (t);
        eqRestore.clear();
        ++applyVersion;
    }
    else if (name == "analyze")
    {
        const auto& st = value;
        mixology::mix::Style style;
        style.key = st.getProperty ("key", "hiphop.modern").toString().toStdString();
        style.name = st.getProperty ("name", "Hip Hop - Modern Clean").toString().toStdString();
        style.bass = st.getProperty ("bass", 4.0);
        style.scoop = st.getProperty ("scoop", 1.5);
        style.air = st.getProperty ("air", 2.0);
        if (bridge.applying())
            r = { false, "Mixology is still applying " + bridge.planProgress().what + ": try again in a second." };
        else if (mixology::link::processRegistry().links (mixology::link::nowMs()).empty())
            r = { false, "No Mixology Link found. Insert Mixology Link on your tracks (after their other plug-ins), then press Analyze Mix." };
        else
        {
            analysis.begin (style);
            analysis.setVocalOffset (vocalOffset, vocalFromYou);
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
            reevaluate();
            ++analysisVersion;
        }
    }
    else if (name == "setReference")
    {
        const auto hash = static_cast<std::uint32_t> (static_cast<juce::int64> (arg.getProperty ("hash", 0)));
        for (auto& [h, ts] : trackSettings) ts.reference = false;
        if (static_cast<bool> (value) && hash != 0) trackSettings[hash].reference = true;
        hearingReference = false;
        cacheTrackSettings();
        reevaluate();
        ++analysisVersion;
    }
    else if (name == "setMusicBus")
    {
        auto hash = static_cast<std::uint32_t> (static_cast<juce::int64> (arg.getProperty ("hash", 0)));
        if (hash == 0 && arg.hasProperty ("name"))   // from the Mix Assistant: by the channel's name
        {
            const auto wanted = arg.getProperty ("name", "").toString().toStdString();
            for (const auto& [h, f] : bridge.knownFaders())
                if (f.title == wanted) hash = h;
        }
        if (hash == 0) r = { false, "That channel has no name Mixology can keep track of." };
        else
        {
            const bool on = static_cast<bool> (value);
            for (auto& [h, ts] : trackSettings) ts.musicBus = false;
            if (on) trackSettings[hash].musicBus = true;
            cacheTrackSettings();
            reevaluate();
            ++analysisVersion;
            info = on ? "Got it: the music plays through this group. Mixology moves the music on its fader, and never counts it as extra music."
                      : "This channel is no longer the music group.";
        }
    }
    else if (name == "vocalTaste")
    {
        const auto taste = value.toString();
        if (taste == "style")
        {
            vocalOffset = 0.0;
            vocalFromYou = false;
            info = "Mixology aims for " + report.style.name + "'s usual vocal level again.";
        }
        else if (! report.valid || ! report.hasVocalRatio)
            r = { false, "Analyze Mix first (with the vocal singing): then Mixology knows where your vocal sits now." };
        else
        {
            const double step = taste == "quieter" ? -2.5 : taste == "louder" ? 2.5 : 0.0;
            vocalOffset = juce::jlimit (-24.0, 24.0, report.vocalRatio + step - report.styleVocalTarget);
            vocalFromYou = true;
            info = taste == "right" ? "Got it: Mixology keeps your vocal where it is (saved with the project)."
                 : std::string ("Got it: Balance and Auto Mix now aim for the vocal 2.5 dB ") + (step < 0 ? "lower" : "higher") + " than now (saved with the project).";
        }
        if (r.ok)
        {
            analysis.setVocalOffset (vocalOffset, vocalFromYou);
            reevaluate();
            cachePrefs();
            ++analysisVersion;
        }
    }
    else if (name == "plan")
    {
        const auto tool = mixology::mix::toolFromKey (value.toString().toStdString());
        if (! tool) r = { false, "Unknown tool." };
        else if (bridge.applying())   // faders are still on their way (Apply / Undo): a plan now would see them half-moved
            r = { false, "Mixology is still moving faders (" + bridge.planProgress().what + "): open it again in a moment." };
        else
        {
            currentPlan = mixology::mix::makePlan (*tool, report, channelsNow(), mixNow());
            ++currentPlanId;
            planResult = planToVar (currentPlan);
        }
    }
    else if (name == "planApply")
    {
        if (static_cast<juce::int64> (arg.getProperty ("id", 0)) != static_cast<juce::int64> (currentPlanId))
            r = { false, "That plan is out of date: open it again." };
        else
            r = applyCurrentPlan (arg.getProperty ("picked", juce::var()), static_cast<bool> (arg.getProperty ("allowBig", false)),
                                  static_cast<bool> (arg.getProperty ("headroom", false)), info);
    }
    else if (name == "planUndo")
    {
        const bool viaBridge = lastApply.valid && ! lastApply.bridgeUndo.empty();
        r = undoLastApply (info);
        auto* po = new juce::DynamicObject();
        po->setProperty ("bridge", r.ok && viaBridge);
        planResult = juce::var (po);
    }
    else if (name == "refListen") r = listenToReference (value.toString() == "ref", info);
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
    if (! planResult.isVoid()) o->setProperty (name == "planUndo" ? "undo" : "plan", planResult);
    return juce::var (o);
}

// ---------------------------------------------------------------------------------------------
// Milestone 4: plans

const mixology::link::LinkInfo* BridgeHub::uniqueLink (const std::vector<mixology::link::LinkInfo>& links, std::uint32_t hash) const
{
    const mixology::link::LinkInfo* found = nullptr;
    for (const auto& l : links)
        if (l.identity.hasName && l.nameHash == hash)
        {
            if (found != nullptr) return nullptr;   // two Links with this name: can't tell which
            found = &l;
        }
    return found;
}

void BridgeHub::fillProc (mixology::mix::ChannelNow& c, const mixology::link::LinkInfo& l) const
{
    if (! l.hasProcessing) return;
    c.hasProc = mixology::link::processRegistry().readProc (l.slot, l.generation, c.proc);
    if (auto it = procAtAnalysis.find (l.nameHash); it != procAtAnalysis.end())
    {
        c.procAtAnalysis = it->second;
        c.procAtAnalysisKnown = true;
    }
    for (int i = 0; i < l.duck.count; ++i) c.duck.push_back (l.duck.hashes[static_cast<std::size_t> (i)]);
}

mixology::mix::MixNow BridgeHub::mixNow() const
{
    mixology::mix::MixNow m;
    if (! mixProc.get) return m;
    m.known = true;
    m.now = mixProc.get();
    m.atAnalysisKnown = mixProcAtAnalysisKnown;
    m.atAnalysis = mixProcAtAnalysis;
    return m;
}

void BridgeHub::setMixProcessing (const void* owner, MixProcessing p)
{
    mixOwner = owner;
    mixProc = std::move (p);
}

void BridgeHub::clearMixProcessing (const void* owner)
{
    if (owner != mixOwner) return;   // another Mixology registered since
    mixOwner = nullptr;
    mixProc = {};
}

void BridgeHub::fillVoxology (mixology::mix::ChannelNow& c) const
{
    const auto* v = voxologyOn (c.hash);
    if (v == nullptr || (v->flags & suite::VocalInfo::processing) == 0) return;
    c.voxology.on = true;
    c.voxology.compressor = (v->flags & suite::VocalInfo::compressor) != 0;
    c.voxology.reverb = (v->flags & suite::VocalInfo::reverb) != 0;
    c.voxology.delay = (v->flags & suite::VocalInfo::delay) != 0;
    c.voxology.eq = (v->flags & suite::VocalInfo::eq) != 0;
    c.voxology.compressionDb = v->compressionDb;
}

std::vector<mixology::mix::ChannelNow> BridgeHub::channelsNow() const
{
    const auto links = mixology::link::processRegistry().links (mixology::link::nowMs());
    std::vector<mixology::mix::ChannelNow> out;
    std::set<std::uint32_t> seen;
    for (const auto& [hash, f] : bridge.knownFaders())
    {
        mixology::mix::ChannelNow c;
        c.hash = hash;
        c.name = f.title;
        c.faderKnown = true;
        c.faderDb = std::max (f.db, -200.0);
        c.hasPan = f.hasPan;
        c.pan = (f.pan - 0.5) * 200.0;
        c.locked = bridge.isProtected (hash);
        if (auto ts = trackSettings.find (hash); ts != trackSettings.end()) c.musicBus = ts->second.musicBus;
        fillVoxology (c);
        if (auto it = fadersAtAnalysis.find (hash); it != fadersAtAnalysis.end())
        {
            c.hasFaderAtAnalysis = true;
            c.faderAtAnalysis = std::max (it->second, -200.0);
        }
        if (const auto* l = uniqueLink (links, hash))
        {
            c.hasLink = true;
            c.assist = l->assist;
            c.channels = l->channels;
            c.panUnknown = l->channels == 2 && f.hasPan && f.pan <= 0.0005;   // Stereo Combined Panner (see getState)
            fillProc (c, *l);
        }
        if (bridge.panIgnored (hash)) c.panUnknown = true;   // Cubase doesn't pan it (mono group / output)
        seen.insert (hash);
        out.push_back (c);
    }
    for (const auto& l : links)
    {
        if (! l.identity.hasName || seen.count (l.nameHash) != 0) continue;
        mixology::mix::ChannelNow c;   // a Link whose channel Cubase hasn't shown (bridge off): EQ only
        c.hash = l.nameHash;
        c.name = l.identity.name;
        c.locked = bridge.isProtected (l.nameHash);
        c.hasLink = uniqueLink (links, l.nameHash) != nullptr;
        c.assist = l.assist;
        c.channels = l.channels;
        if (c.hasLink) fillProc (c, l);
        fillVoxology (c);
        seen.insert (l.nameHash);
        out.push_back (c);
    }
    return out;
}

juce::var BridgeHub::planToVar (const mixology::mix::Plan& p) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", static_cast<juce::int64> (currentPlanId));
    o->setProperty ("tool", mixology::mix::toolKey (p.tool));
    o->setProperty ("title", juce::String (p.title));
    o->setProperty ("valid", p.valid);
    o->setProperty ("summary", juce::String::fromUTF8 (p.summary.c_str()));
    juce::Array<juce::var> notes;
    for (const auto& n : p.notes) notes.add (juce::String::fromUTF8 (n.c_str()));
    o->setProperty ("notes", notes);
    juce::Array<juce::var> changes;
    for (const auto& c : p.changes)
    {
        auto* co = new juce::DynamicObject();
        co->setProperty ("hash", static_cast<juce::int64> (c.hash));
        co->setProperty ("name", juce::String::fromUTF8 (c.name.c_str()));
        co->setProperty ("role", mixology::mix::roleKey (c.role));
        co->setProperty ("kind", juce::String (c.kind));
        co->setProperty ("from", std::round (c.from * 10.0) / 10.0);
        co->setProperty ("to", std::round (c.to * 10.0) / 10.0);
        co->setProperty ("wanted", std::round (c.wanted * 10.0) / 10.0);
        co->setProperty ("big", c.big);
        co->setProperty ("hz", std::round (c.hz));
        co->setProperty ("why", juce::String::fromUTF8 (c.why.c_str()));
        if (! c.proc.empty())
        {
            co->setProperty ("fromText", juce::String::fromUTF8 (c.fromText.c_str()));
            co->setProperty ("toText", juce::String::fromUTF8 (c.toText.c_str()));
            co->setProperty ("fullMix", c.fullMix);
        }
        changes.add (juce::var (co));
    }
    o->setProperty ("changes", changes);
    o->setProperty ("headroomDb", std::round (p.headroomDb * 10.0) / 10.0);
    o->setProperty ("headroomWhy", juce::String::fromUTF8 (p.headroomWhy.c_str()));
    o->setProperty ("headroomCount", static_cast<int> (p.headroom.size()));
    juce::Array<juce::var> hints;
    for (const auto& n : p.groupHints) hints.add (juce::String::fromUTF8 (n.c_str()));
    o->setProperty ("groupHints", hints);
    return juce::var (o);
}

juce::var BridgeHub::applyState() const
{
    const auto& pr = bridge.planProgress();
    auto* o = new juce::DynamicObject();
    o->setProperty ("active", bridge.applying());
    o->setProperty ("what", juce::String::fromUTF8 (pr.what.c_str()));
    o->setProperty ("done", pr.done);
    o->setProperty ("total", pr.total);
    o->setProperty ("skipped", pr.skipped);
    o->setProperty ("message", juce::String::fromUTF8 (pr.message.c_str()));
    juce::Array<juce::var> notes;
    for (const auto& n : pr.notes) notes.add (juce::String::fromUTF8 (n.c_str()));
    o->setProperty ("notes", notes);
    o->setProperty ("serial", static_cast<juce::int64> (pr.serial));
    o->setProperty ("canUndo", lastApply.valid && ! lastApply.waitingForBridge);
    o->setProperty ("undoWhat", juce::String::fromUTF8 (lastApply.title.c_str()));
    o->setProperty ("hearingReference", hearingReference);
    return juce::var (o);
}

mixology::BridgeSession::Result BridgeHub::applyCurrentPlan (const juce::var& picked, bool allowBig, bool headroom, std::string& info)
{
    using PlanStep = mixology::BridgeSession::PlanStep;
    if (! currentPlan.valid) return { false, "Nothing to apply." };
    if (bridge.busy()) return { false, "Mixology is still busy with the last change: wait a second." };
    const auto* list = picked.getArray();
    headroom = headroom && currentPlan.headroomDb < 0.0 && ! currentPlan.headroom.empty();
    if ((list == nullptr || list->isEmpty()) && ! headroom) return { false, "Tick at least one change." };

    auto& registry = mixology::link::processRegistry();
    const auto links = registry.links (mixology::link::nowMs());
    std::map<std::uint32_t, PlanStep> steps;
    std::map<std::uint32_t, mixology::AssistSettings> eq;
    std::vector<const mixology::mix::Change*> procs;
    std::vector<std::string> noLink;
    static const juce::Array<juce::var> none;
    for (const auto& v : list != nullptr ? *list : none)
    {
        const int i = static_cast<int> (v);
        if (i < 0 || i >= static_cast<int> (currentPlan.changes.size())) continue;
        const auto& c = currentPlan.changes[static_cast<std::size_t> (i)];
        if (c.kind == "level" || c.kind == "pan")
        {
            auto& st = steps[c.hash];
            st.hash = c.hash;
            st.title = c.name;
            if (c.kind == "level")
            {
                st.setVolume = true;
                st.db = allowBig ? c.wanted : c.to;
                st.checkFrom = true;
                st.fromDb = c.from;
            }
            else
            {
                st.setPan = true;
                st.pan = std::clamp ((c.to + 100.0) / 200.0, 0.0, 1.0);
            }
            continue;
        }
        if (! c.proc.empty())
        {
            if (c.fullMix ? static_cast<bool> (mixProc.set) : uniqueLink (links, c.hash) != nullptr) procs.push_back (&c);
            else noLink.push_back (c.name);
            continue;
        }
        const auto* l = uniqueLink (links, c.hash);
        if (l == nullptr) { noLink.push_back (c.name); continue; }
        auto it = eq.find (c.hash);
        if (it == eq.end()) it = eq.emplace (c.hash, l->assist).first;
        auto& a = it->second;
        if (c.kind == "lowcut") a.lowCutHz = static_cast<float> (c.to);
        else if (c.kind == "mud") { a.mudDb = static_cast<float> (c.to); a.mudHz = static_cast<float> (c.hz); }
        else if (c.kind == "pocket") { a.pocketDb = static_cast<float> (c.to); a.pocketHz = static_cast<float> (c.hz); }
    }
    // Room for Masterology: every fader that plays moves the same amount, on top of its level change (if ticked).
    if (headroom)
        for (const auto& h : currentPlan.headroom)
        {
            auto& st = steps[h.hash];
            const double base = st.setVolume ? st.db : h.from;
            st.hash = h.hash;
            st.title = h.name;
            st.setVolume = true;
            st.db = std::min (base + currentPlan.headroomDb, mixology::mix::kMaxFaderDb);
            st.checkFrom = true;
            st.fromDb = h.from;
        }
    if (steps.empty() && eq.empty() && procs.empty())
        return { false, noLink.empty() ? std::string ("Tick at least one change.") : "Mixology Link isn't reachable on those tracks any more." };
    if (! steps.empty() && bridge.link() != mixology::BridgeSession::Link::connected)
        return { false, "Not connected to Cubase, so faders and pans can't move (Settings > Cubase Bridge)." };

    LastApply next;
    next.valid = true;
    next.title = currentPlan.title;
    if (! steps.empty())
    {
        std::vector<PlanStep> v;
        for (auto& [h, st] : steps) v.push_back (st);
        const auto r = bridge.applyPlan (currentPlan.title, std::move (v), now());
        if (! r.ok) return r;
        next.waitingForBridge = true;
        next.bridgeSerial = bridge.planProgress().serial + 1;
    }
    for (const auto& [hash, a] : eq)
    {
        const auto* l = uniqueLink (links, hash);
        next.assist.push_back ({ l->slot, l->generation, l->assist, hash });
        eqRestore.noteSet (hash, l->assist, a);
        registry.requestAssist (l->slot, l->generation, a);
    }
    // Processing (Polish): each control like a move on its page; what it was goes into the undo.
    std::set<std::uint32_t> procTracks;
    for (const auto* c : procs)
    {
        ProcUndo u;
        u.fullMix = c->fullMix;
        mixology::proc::Values before {};
        if (c->fullMix) before = mixProc.get();
        else
        {
            const auto* l = uniqueLink (links, c->hash);
            u.slot = l->slot;
            u.generation = l->generation;
            if (! registry.readProc (l->slot, l->generation, before)) before = mixology::proc::defaults();
            if (c->setDuck)
            {
                u.duckSet = true;
                u.duckBefore = l->duck;
                mixology::link::DuckSources d;
                for (auto h : c->duck)
                    if (d.count < mixology::link::kMaxDuckSources) d.hashes[static_cast<std::size_t> (d.count++)] = h;
                registry.requestDuck (l->slot, l->generation, d);
            }
        }
        for (const auto& [id, v] : c->proc)
        {
            u.before.push_back ({ id, before[static_cast<std::size_t> (id)] });
            if (c->fullMix) mixProc.set (id, v);
            else registry.requestProc (u.slot, u.generation, id, v);
        }
        procTracks.insert (c->hash);
        next.procs.push_back (std::move (u));
    }
    lastApply = std::move (next);
    ++applyVersion;

    std::vector<std::string> parts;
    if (! steps.empty()) parts.push_back ("moving " + std::to_string (steps.size()) + " channel" + (steps.size() == 1 ? "" : "s") + " in Cubase");
    if (! eq.empty()) parts.push_back ("Assist EQ on " + std::to_string (eq.size()) + " track" + (eq.size() == 1 ? "" : "s"));
    if (! procTracks.empty()) parts.push_back ("processing on " + std::to_string (procTracks.size()) + (procTracks.size() == 1 ? " track" : " tracks"));
    std::string joined;
    for (const auto& p : parts) joined += (joined.empty() ? "" : ", ") + p;
    info = currentPlan.title + ": " + joined + ".";
    if (! noLink.empty()) info += " (" + std::to_string (noLink.size()) + " skipped: no Mixology Link.)";
    return { true, {} };
}

mixology::BridgeSession::Result BridgeHub::undoLastApply (std::string& info)
{
    if (! lastApply.valid) return { false, "Nothing to undo." };
    if (bridge.busy() || lastApply.waitingForBridge) return { false, "Wait until Mixology has finished applying." };
    if (! lastApply.bridgeUndo.empty())
    {
        if (bridge.link() != mixology::BridgeSession::Link::connected) return { false, "Not connected to Cubase: can't put the faders back yet." };
        const auto r = bridge.applyPlan ("Undo " + lastApply.title, lastApply.bridgeUndo, now());
        if (! r.ok) return r;
    }
    auto& registry = mixology::link::processRegistry();
    for (const auto& a : lastApply.assist)
    {
        registry.requestAssist (a.slot, a.generation, a.before);
        eqRestore.noteUndo (a.hash, a.before);
    }
    for (auto it = lastApply.procs.rbegin(); it != lastApply.procs.rend(); ++it)   // newest first: the oldest value wins
    {
        for (const auto& [id, v] : it->before)
        {
            if (it->fullMix) { if (mixProc.set) mixProc.set (id, v); }
            else registry.requestProc (it->slot, it->generation, id, v);
        }
        if (it->duckSet) registry.requestDuck (it->slot, it->generation, it->duckBefore);
    }
    info = "Undoing " + lastApply.title + ": everything it changed goes back.";
    lastApply = {};
    ++applyVersion;
    return { true, {} };
}

mixology::BridgeSession::Result BridgeHub::listenToReference (bool toReference, std::string& info)
{
    const auto& ref = report.reference;
    if (! ref.chosen) return { false, "Choose your reference song first." };
    if (bridge.link() != mixology::BridgeSession::Link::connected) return { false, "Not connected to Cubase: Mixology can't solo the reference." };
    mixology::BridgeSession::PlanStep st;
    st.hash = ref.hash;
    st.title = ref.name;
    st.setMute = true;
    st.mute = ! toReference;
    st.setSolo = true;
    st.solo = toReference;
    const auto r = bridge.applyPlan (toReference ? "Hear the reference" : "Back to your mix", { st }, now());
    if (! r.ok) return r;
    hearingReference = toReference;
    ++applyVersion;

    if (toReference && ref.heard)
    {
        // Level-matched: the reference's Trim brings it to your mix's loudness, so you compare sound, not volume.
        const auto links = mixology::link::processRegistry().links (mixology::link::nowMs());
        if (const auto* l = uniqueLink (links, ref.hash))
        {
            const float trim = static_cast<float> (std::clamp (l->trimDb - ref.loudnessDiff, -24.0, 24.0));
            if (std::abs (trim - l->trimDb) > 0.2f)
            {
                mixology::link::processRegistry().requestTrim (l->slot, l->generation, trim);
                info = "Playing '" + ref.name + "' alone, matched to your mix's loudness (its Trim: " + (trim > 0 ? "+" : "") + juce::String (trim, 1).toStdString() + " dB).";
            }
        }
    }
    if (info.empty()) info = toReference ? "Playing '" + ref.name + "' alone." : "Back to your mix ('" + ref.name + "' muted).";
    return { true, {} };
}
