#include "BridgeHub.h"

#include "mixology/Pack.h"
#include "mixology/ProcChain.h"

#include <algorithm>
#include <array>
#include <cmath>
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

    // ---- the undo history's changes ----
    using HChange = mixology::history::Change;
    using HWhat = mixology::history::What;

    HChange channelChange (HWhat what, std::uint32_t hash, const std::string& title)
    {
        HChange c;
        c.what = what;
        c.hash = hash;
        c.track = title;
        return c;
    }

    HChange linkChange (HWhat what, const mixology::link::LinkInfo& l)
    {
        HChange c;
        c.what = what;
        c.hash = l.identity.hasName ? l.nameHash : 0u;
        c.track = l.identity.hasName ? l.identity.name : std::string();
        c.slot = l.slot;
        c.generation = l.generation;
        return c;
    }

    std::array<float, mixology::history::kAssistValues> assistValues (const mixology::AssistSettings& a)
    {
        return { a.lowCutHz, a.mudHz, a.mudDb, a.pocketHz, a.pocketDb };
    }

    void setAssistValue (mixology::AssistSettings& a, int index, float v)
    {
        switch (index)
        {
            case mixology::history::assistLowCutHz: a.lowCutHz = v; break;
            case mixology::history::assistMudHz: a.mudHz = v; break;
            case mixology::history::assistMudDb: a.mudDb = v; break;
            case mixology::history::assistPocketHz: a.pocketHz = v; break;
            default: a.pocketDb = v; break;
        }
    }

    /** The Assist EQ values that differ, as history changes on that Link. */
    std::vector<HChange> assistChanges (const mixology::link::LinkInfo& l, const mixology::AssistSettings& from, const mixology::AssistSettings& to)
    {
        std::vector<HChange> out;
        const auto a = assistValues (from), b = assistValues (to);
        for (int i = 0; i < mixology::history::kAssistValues; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            if (std::abs (a[k] - b[k]) < 1.0e-6f) continue;
            auto c = linkChange (HWhat::assist, l);
            c.index = i;
            c.before = a[k];
            c.after = b[k];
            out.push_back (std::move (c));
        }
        return out;
    }

    std::vector<std::uint32_t> duckList (const mixology::link::DuckSources& d)
    {
        std::vector<std::uint32_t> v;
        for (int i = 0; i < d.count; ++i) v.push_back (d.hashes[static_cast<std::size_t> (i)]);
        return v;
    }

    // The project's copy of what Mixology knows (sessionState): packed, compressed, base64 for the XML.
    juce::String packText (const std::string& bytes)
    {
        juce::MemoryOutputStream out;
        {
            juce::GZIPCompressorOutputStream gz (out, 6);
            gz.write (bytes.data(), bytes.size());
            gz.flush();
        }
        return juce::Base64::toBase64 (out.getData(), out.getDataSize());
    }

    std::string unpackText (const juce::String& text)
    {
        juce::MemoryOutputStream decoded;
        if (text.isEmpty() || ! juce::Base64::convertFromBase64 (decoded, text)) return {};
        juce::MemoryInputStream in (decoded.getData(), decoded.getDataSize(), false);
        juce::GZIPDecompressorInputStream gz (in);
        juce::MemoryOutputStream out;
        out.writeFromInputStream (gz, 64 * 1024 * 1024);
        return std::string (static_cast<const char*> (out.getData()), out.getDataSize());
    }

    // The smart tool that fixes a Mix Assistant point (the interface's tool ids): "" = none does (advice only).
    const char* toolForPoint (const std::string& id)
    {
        if (id == "vocal-low" || id == "vocal-high" || id == "kick-quiet" || id == "headroom") return "balance";
        if (id == "vocal-pocket") return "pocket";
        if (id == "mud" || id == "highpass") return "clean";
        if (id == "harsh" || id == "low-wide") return "polish";
        return "";
    }

    // Studio One, REAPER and Bitwig have no Mixology bridge: Mixology lists the moves and you set the faders.
    bool dawHasBridge (const std::string& key) { return key != "studioone" && key != "reaper" && key != "bitwig"; }

    // Each DAW's fader has its own curve (0 dB sits at a different place on each): learned and kept per DAW.
    // Cubase keeps the name it always had.
    juce::String lawKey (const std::string& daw) { return daw.empty() || daw == "cubase" ? juce::String ("faderLaw") : "faderLaw." + juce::String (daw); }

    constexpr const char* kSessionMagic = "MXS1";
    constexpr std::uint32_t kAdviceMagic = 0x3150584du;   // "MXP1"

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

void BridgeHub::sendToDaw (const std::vector<std::uint8_t>& m)
{
    if (toCubase == nullptr || m.empty()) return;
    if (m.front() == 0xF0)
    {
        if (m.size() >= 2)
            toCubase->sendMessageNow (juce::MidiMessage::createSysExMessage (m.data() + 1, static_cast<int> (m.size()) - 2));
        return;
    }
    toCubase->sendMessageNow (juce::MidiMessage (m.data(), static_cast<int> (m.size())));
}

void BridgeHub::saveMcuLaw()
{
    if (mcu == nullptr) return;
    mcuLawSaved = mcu->law().learned();
    settingsFile->setValue ("mcuLaw." + juce::String (dawKey), juce::String (mcu->law().serialise()));
    settingsFile->saveIfNeeded();
}

void BridgeHub::setDaw (const std::string& key)
{
    if (key == dawKey) return;
    using mixology::mcu::Dialect;
    const bool wasMcu = mcu != nullptr;
    const auto oldDialect = wasMcu ? mcu->dialect() : Dialect::logic;
    saveMcuLaw();
    settingsFile->setValue (lawKey (dawKey), juce::String (bridge.faderLaw().serialise()));
    dawKey = key;
    bridge.faderLaw().deserialise (settingsFile->getValue (lawKey (key)).toStdString());
    lawPointsSaved = bridge.faderLaw().size();
    settingsFile->saveIfNeeded();
    if (! dawHasBridge (key))
        closePorts ("Moving faders isn't available in " + juce::String (suite::dawName (key)) + ": Mixology lists each move for you to set");
    const bool known = ! key.empty() && key != "other";
    bridge.setDaw (known ? suite::dawName (key) : std::string ("your DAW"), key == "logic" || key == "cakewalk");
    const bool wantMcu = key == "logic" || key == "cakewalk";
    const auto dialect = key == "logic" ? Dialect::logic : Dialect::cakewalk;
    if (wantMcu == wasMcu && (! wantMcu || dialect == oldDialect)) return;

    const double t = now();
    if (wantMcu)
    {
        mcu = std::make_unique<mixology::mcu::McuBridge> (dialect);
        mcu->law().deserialise (settingsFile->getValue ("mcuLaw." + juce::String (key)).toStdString());
        mcuLawSaved = mcu->law().learned();
        mcu->setTrackNames (mcuNames);
        mcu->reset (t);
    }
    else
    {
        mcu.reset();
    }
    mcuMode.store (mcu != nullptr);
    {
        const juce::ScopedLock sl (inboxLock);
        inbox.clear();
    }
    // Start the conversation again: the other side is now a script or a surface.
    if (toCubase != nullptr && fromCubase != nullptr)
    {
        bridge.setPortsOpen (false, t, "switching to " + std::string (mcu != nullptr ? mcu->dawName() : "a bridge script"));
        bridge.setPortsOpen (true, t, mcu != nullptr ? std::string (mcu->dawName()) + " Mackie Control" : "bridge script");
    }
}

BridgeHub::~BridgeHub()
{
    stopTimer();
    saveMcuLaw();
    closePorts ("Mixology closed");
    settingsFile->setValue (lawKey (dawKey), juce::String (bridge.faderLaw().serialise()));
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
    if (dawHasBridge (dawKey)) openPorts();
    startTimerHz (100);
}

void BridgeHub::openPorts()
{
    lastOpenAttempt = now();
    const auto outInfo = findDevice (juce::MidiOutput::getAvailableDevices(), kToCubase);
    const auto inInfo = findDevice (juce::MidiInput::getAvailableDevices(), kFromCubase);

    juce::String problem;
#if JUCE_MAC
    // On a Mac, Mixology makes the two ports itself (CoreMIDI virtual ports): nothing to install. An IAC bus (or
    // another driver) with the same names is used instead when there is one.
    if (outInfo.identifier.isEmpty() || inInfo.identifier.isEmpty())
    {
        toCubase = juce::MidiOutput::createNewDevice (kToCubase);
        fromCubase = juce::MidiInput::createNewDevice (kFromCubase, this);
        if (toCubase != nullptr && fromCubase != nullptr)
        {
            fromCubase->start();
            portProblem.clear();
            if (mcu != nullptr) mcu->reset (now());
            bridge.setPortsOpen (true, now(), "Mixology's own ports (macOS)");
            return;
        }
        toCubase.reset();
        fromCubase.reset();
    }
#endif
    if (outInfo.identifier.isEmpty() || inInfo.identifier.isEmpty())
    {
        problem = "Can't find the virtual MIDI port";
        if (outInfo.identifier.isEmpty() && inInfo.identifier.isEmpty())
            problem << "s '" << kToCubase << "' and '" << kFromCubase << "'";
        else
            problem << " '" << (outInfo.identifier.isEmpty() ? kToCubase : kFromCubase) << "'";
#if JUCE_MAC
        problem << ". Mixology couldn't make them: restart the DAW, or add them as IAC buses in Audio MIDI Setup, then press Reconnect.";
#else
        problem << ". Create them in loopMIDI (see Setup), then press Reconnect.";
#endif
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
            if (mcu != nullptr) mcu->reset (now());
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
    if (! message.isSysEx() && ! mcuMode.load()) return;   // a Mackie Control DAW also sends faders, lights, knob rings
    const auto* raw = message.getRawData();
    const auto size = static_cast<std::size_t> (message.getRawDataSize());
    const juce::ScopedLock sl (inboxLock);
    if (inbox.size() < 4096)
        inbox.emplace_back (raw, raw + size);
}

void BridgeHub::timerCallback()
{
    const double t = now();
    if (t - lastProjectCheck >= 0.5)
    {
        lastProjectCheck = t;
        updateProjects();   // you switched to another open project, or a Mixology joined this one
    }
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
                if (f.size() > 6 && f[6].isNotEmpty()) ts.vocalOffset = juce::jlimit (-24.0, 24.0, f[6].getDoubleValue());
            }
            std::vector<std::uint32_t> noPan;   // the channels Cubase doesn't pan, as this project learned them
            for (const auto& [hash, ts] : trackSettings)
                if (ts.noPan) noPan.push_back (hash);
            bridge.setNoPanList (noPan);
            seenNoPanVersion = bridge.noPanVersion();
            cacheTrackSettings();
            reevaluate();
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
            autoAnalyze = kv["autoAnalyze"] == "1";
            analysis.setVocalOffset (vocalOffset, vocalFromYou);
            reevaluate();
            cachePrefs();
            ++analysisVersion;
        }
    }
    applyPendingSession();   // after the track settings and prefs: the advice is evaluated with your roles
    if (t - lastSessionCache >= 0.5)
    {
        lastSessionCache = t;
        updateSessionCache();
    }
    if (t - doneCheckedAt >= 0.5)
    {
        doneCheckedAt = t;
        if (assistGuessed) guessAssistAtAnalysis();   // Links that registered after the project's advice came back
        updateDonePoints();
    }

    if (toCubase == nullptr && dawHasBridge (dawKey) && t - lastOpenAttempt > kReopenInterval)
        openPorts();

    std::vector<std::vector<std::uint8_t>> received;
    {
        const juce::ScopedLock sl (inboxLock);
        received.swap (inbox);
    }
    if (mcu != nullptr)
    {
        // Logic Pro / Cakewalk: the DAW speaks Mackie Control to the translator, the translator the bridge
        // protocol to the session.
        for (const auto& m : received)
            mcu->fromDaw (m.data(), m.size(), t);
        mcu->tick (t);
        for (const auto& m : mcu->takeForMixology())
            bridge.receive (m.data(), m.size(), t);
    }
    else
    {
        for (const auto& m : received)
            if (! m.empty() && m.front() == 0xF0)
                bridge.receive (m.data(), m.size(), t);
    }

    bridge.tick (t);
    if (hearingReference && ! bridge.applying())
    {
        // Back to the mix by other means (unsoloed in the DAW, or the bridge went): Auto Analyze listens again.
        const auto kf = bridge.knownFaders().find (report.reference.hash);
        if (bridge.link() != mixology::BridgeSession::Link::connected || (kf != bridge.knownFaders().end() && ! kf->second.solo))
        {
            hearingReference = false;
            ++applyVersion;
        }
    }
    if (bridge.planProgress().serial != seenPlanSerial)
    {
        seenPlanSerial = bridge.planProgress().serial;
        if (lastApply.valid && lastApply.waitingForBridge && seenPlanSerial >= lastApply.bridgeSerial)
        {
            lastApply.waitingForBridge = false;
            lastApply.bridgeUndo = bridge.planProgress().undo;
        }
        finishPendingPlans();
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
    {
        if (mcu != nullptr) mcu->fromMixology (m, t);
        else sendToDaw (m);
    }
    if (mcu != nullptr)
    {
        for (const auto& m : mcu->takeForDaw())
            sendToDaw (m);
        if (mcu->law().learned() != mcuLawSaved && t - lastLawSave > 5.0)
        {
            lastLawSave = t;
            saveMcuLaw();
        }
    }

    // Save the learned fader curve now and then (it is the same for every project).
    if (bridge.faderLaw().size() != lawPointsSaved && t - lastLawSave > 5.0)
    {
        lawPointsSaved = bridge.faderLaw().size();
        lastLawSave = t;
        settingsFile->setValue (lawKey (dawKey), juce::String (bridge.faderLaw().serialise()));
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
    // Read again when the file changed (another Mixology window, or the same DAW open twice, saved one).
    const auto file = userPresetFile();
    const auto modified = file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : 0;
    if (! userPresetsLoaded || modified != userPresetsModified)
    {
        userPresetsLoaded = true;
        userPresetsModified = modified;
        userPresetList = file.existsAsFile() ? mixology::presets::parse (file.loadFileAsString().toStdString())
                                             : std::vector<mixology::presets::Preset> {};
    }
    return userPresetList;
}

bool BridgeHub::writeUserPresets()
{
    const auto file = userPresetFile();
    file.getParentDirectory().createDirectory();
    const bool ok = file.replaceWithText (juce::String::fromUTF8 (mixology::presets::serialise (userPresetList).c_str()));
    userPresetsModified = file.existsAsFile() ? file.getLastModificationTime().toMilliseconds() : 0;
    return ok;
}

bool BridgeHub::saveUserPreset (const mixology::presets::Preset& p)
{
    userPresets();
    auto it = std::find_if (userPresetList.begin(), userPresetList.end(), [&p] (const auto& q) { return q.name == p.name; });
    if (it != userPresetList.end()) *it = p;
    else userPresetList.push_back (p);
    std::sort (userPresetList.begin(), userPresetList.end(), [] (const auto& a, const auto& b) { return juce::String (a.name).compareNatural (b.name) < 0; });
    return writeUserPresets();
}

bool BridgeHub::deleteUserPreset (const std::string& name)
{
    userPresets();
    const auto before = userPresetList.size();
    userPresetList.erase (std::remove_if (userPresetList.begin(), userPresetList.end(), [&name] (const auto& q) { return q.name == name; }), userPresetList.end());
    if (userPresetList.size() == before) return false;
    return writeUserPresets();
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
    if (mcu != nullptr)
    {
        // The display shows 7 characters: the Links' full names let the translator expand them.
        std::vector<std::string> names;
        for (const auto& l : links)
            if (l.identity.hasName && ! l.identity.name.empty()) names.push_back (l.identity.name);
        std::sort (names.begin(), names.end());
        if (names != mcuNames)
        {
            mcuNames = names;
            mcu->setTrackNames (mcuNames);
        }
    }
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
    const auto text = "vocalOffset=" + juce::String (vocalOffset, 2) + ";vocalFromYou=" + (vocalFromYou ? "1" : "0")
                    + ";autoAnalyze=" + (autoAnalyze ? "1" : "0");
    const juce::ScopedLock sl (protectedLock);
    prefsCache = text;
}

juce::String BridgeHub::sessionState()
{
    if (juce::MessageManager::existsAndIsCurrentThread()) updateSessionCache (true);
    const juce::ScopedLock sl (protectedLock);
    return hasPendingSession ? sessionPending : sessionCache;
}

void BridgeHub::setSessionState (const juce::String& text)
{
    const juce::ScopedLock sl (protectedLock);
    sessionPending = text;
    hasPendingSession = true;   // applied on the message thread (next timer tick), after the track settings
}

void BridgeHub::updateSessionCache (bool forSave)
{
    {
        const juce::ScopedLock sl (protectedLock);
        if (hasPendingSession) return;   // the project's copy isn't read yet: saving keeps it as it is
    }
    bool changed = sessionDirty;   // a project's parts came in: the text to save is made again
    sessionDirty = false;
    if (savedHistory.key != history.version())
    {
        // Packing a long History takes a moment: not while a knob is being turned (it changes many times a
        // second), but once it rests, every 10 s at the latest, and always when the project is saved.
        const double t = now();
        if (history.version() != historySeen) { historySeen = history.version(); historyChangedAt = t; }
        if (forSave || t - historyChangedAt >= 1.5 || t - historyPackedAt >= 10.0)
        {
            savedHistory = { history.version(), history.steps().empty() ? juce::String() : packText (history.save()) };
            historyPackedAt = t;
            changed = true;
        }
    }
    // While Analyze Mix listens again, the moments aren't the advice's: the advice saved stays the one on screen.
    const bool adviceSteady = analyzeState == Analyze::done || analyzeState == Analyze::idle;
    if (savedAdvice.key != adviceSerial && adviceSteady)
    {
        savedAdvice = { adviceSerial, report.valid && analysis.hasMoments() ? packText (adviceBlock()) : juce::String() };
        changed = true;
    }
    {
        mixology::pack::Writer w;
        w.str (bridge.saveRestorePoints());
        w.str (eqRestore.save());
        const bool none = bridge.restorePoints().empty() && eqRestore.size() == 0;
        const auto key = none ? 0ull : static_cast<std::uint64_t> (std::hash<std::string>{} (w.bytes())) | 1ull;
        if (savedRestore.key != key)
        {
            savedRestore = { key, none ? juce::String() : packText (w.bytes()) };
            changed = true;
        }
    }
    if (! changed) return;
    juce::String text (kSessionMagic);
    if (savedHistory.text.isNotEmpty()) text << ";h:" << savedHistory.text;
    if (savedAdvice.text.isNotEmpty()) text << ";a:" << savedAdvice.text;
    if (savedRestore.text.isNotEmpty()) text << ";r:" << savedRestore.text;
    const juce::ScopedLock sl (protectedLock);
    sessionCache = text;
}

void BridgeHub::applyPendingSession()
{
    juce::String text;
    {
        const juce::ScopedLock sl (protectedLock);
        if (! hasPendingSession) return;
        text = sessionPending;
        hasPendingSession = false;
    }
    if (! text.startsWith (kSessionMagic)) return;
    // Each part only fills what this Cubase doesn't know yet (another open project may share this Mixology).
    for (const auto& part : juce::StringArray::fromTokens (text.fromFirstOccurrenceOf (";", false, false), ";", ""))
    {
        const auto kind = part.substring (0, 2);
        const auto bytes = unpackText (part.substring (2));
        if (bytes.empty()) continue;
        if (kind == "h:" && history.steps().empty() && history.load (bytes))
        {
            projectHistoryUpTo = history.steps().empty() ? 0 : history.steps().back().id;
            if (! history.steps().empty())
                bridge.note (now(), "History from the project: " + std::to_string (history.steps().size()) + " steps.");
        }
        else if (kind == "a:" && ! report.valid && analyzeState == Analyze::idle && loadAdvice (bytes))
            savedAdvice = { adviceSerial, part.substring (2) };   // the same as the project's: no need to pack it again
        else if (kind == "r:")
        {
            mixology::pack::Reader r (bytes);
            const auto faders = r.str();
            const auto eq = r.str();
            if (r.ok())
            {
                bridge.loadRestorePoints (faders, now());
                if (eqRestore.size() == 0) eqRestore.load (eq);
                ++applyVersion;
            }
        }
    }
    sessionDirty = true;
    ++analysisVersion;
}

void BridgeHub::guessAssistAtAnalysis()
{
    const auto links = mixology::link::processRegistry().links (mixology::link::nowMs());
    for (const auto& l : links)
        if (l.identity.hasName && uniqueLink (links, l.nameHash) != nullptr && assistAtAnalysis.count (l.nameHash) == 0)
            assistAtAnalysis[l.nameHash] = l.assist;
}

std::string BridgeHub::changedSinceAnalysis (const std::vector<std::string>& names, bool anyFader, bool fadersOnly) const
{
    // What changed on these tracks (or on any fader) since Mixology listened: the advice doesn't know it yet.
    std::vector<std::string> parts;
    auto faderMove = [&] (std::uint32_t hash) {
        const auto now = bridge.knownFaders().find (hash);
        const auto then = fadersAtAnalysis.find (hash);
        if (now == bridge.knownFaders().end() || then == fadersAtAnalysis.end()) return 0.0;
        if (now->second.db <= -100.0 || then->second <= -100.0) return 0.0;
        const double d = now->second.db - then->second;
        return std::abs (d) > 0.3 ? d : 0.0;
    };
    auto db = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1).toStdString() + " dB"; };
    if (anyFader)
        for (const auto& [hash, f] : bridge.knownFaders())
            if (const double d = faderMove (hash); d != 0.0) parts.push_back ("'" + f.title + "' fader " + db (d));
    auto& registry = mixology::link::processRegistry();
    const auto links = registry.links (mixology::link::nowMs());
    for (const auto& n : names)
    {
        const auto hash = mixology::bridge::nameHash (n);
        if (! anyFader)
            if (const double d = faderMove (hash); d != 0.0) parts.push_back ("'" + n + "' fader " + db (d));
        if (fadersOnly) continue;
        const auto* l = uniqueLink (links, hash);
        if (l == nullptr) continue;
        if (auto it = assistAtAnalysis.find (hash); it != assistAtAnalysis.end() && ! mixology::AssistRestore::same (it->second, l->assist))
            parts.push_back ("'" + n + "' Assist EQ");
        mixology::proc::Values v {};
        if (auto it = procAtAnalysis.find (hash); it != procAtAnalysis.end() && registry.readProc (l->slot, l->generation, v))
            for (std::size_t i = 0; i < v.size(); ++i)
                if (std::abs (v[i] - it->second[i]) > 1.0e-3) { parts.push_back ("'" + n + "' processing"); break; }
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size() && i < 3; ++i) out += (i == 0 ? "" : ", ") + parts[i];
    if (parts.size() > 3) out += " and " + std::to_string (parts.size() - 3) + " more";
    return out;
}

void BridgeHub::updateDonePoints()
{
    // A point is done when the tool that fixes it has nothing left to do and its tracks changed since Mixology
    // listened (a plan applied, or you moved it yourself). Hearing the mix again (Auto Analyze, Analyze Mix)
    // confirms it: the point goes, or comes back as it sounds now.
    std::map<std::string, DonePoint> found;
    if (report.valid && analyzeState == Analyze::done)
    {
        const auto chans = channelsNow();
        const auto mix = mixNow();
        std::map<std::string, mixology::mix::Plan> plans;
        for (const auto& sg : report.suggestions)
        {
            const std::string tool = toolForPoint (sg.id);
            const auto t = mixology::mix::toolFromKey (tool);
            if (tool.empty() || ! t) continue;
            if (plans.count (tool) == 0) plans[tool] = mixology::mix::makePlan (*t, report, chans, mix);
            const auto& p = plans[tool];
            const bool nothingLeft = p.valid && (sg.id == "headroom" ? p.headroomDb == 0.0 : p.changes.empty());
            if (! nothingLeft) continue;
            // A level point is only done by faders, and only when Balance could have moved its tracks (in a DAW
            // without a bridge, or with them padlocked, its plan is empty because it can't act, not because it's done).
            const bool levels = tool == "balance";
            if (levels)
            {
                bool movable = false;
                for (const auto& c : chans)
                    if (c.faderKnown && ! c.locked && ! c.sharedName
                        && (sg.tracks.empty() || std::find (sg.tracks.begin(), sg.tracks.end(), c.name) != sg.tracks.end()))
                        movable = true;
                if (! movable) continue;
            }
            DonePoint d;
            d.what = changedSinceAnalysis (sg.tracks, levels, levels);
            d.done = ! d.what.empty();
            if (d.done) found[sg.id] = d;
        }
    }
    std::string key;
    for (const auto& [id, d] : found) key += id + ":" + d.what + ";";
    donePoints = std::move (found);
    if (key != doneShown)
    {
        doneShown = key;
        ++doneVersion;
    }
}

std::string BridgeHub::adviceBlock() const
{
    mixology::pack::Writer w;
    w.u32 (kAdviceMagic);
    w.str (analysis.save());
    w.f64 (analyzedMs);
    w.u32 (static_cast<std::uint32_t> (report.unheard.size()));
    for (const auto& n : report.unheard) w.str (n);
    // What the mix was while Mixology listened: plans and Polish work from there.
    w.u32 (static_cast<std::uint32_t> (fadersAtAnalysis.size()));
    for (const auto& [hash, db] : fadersAtAnalysis) { w.u32 (hash); w.f64 (db); }
    w.u32 (static_cast<std::uint32_t> (procAtAnalysis.size()));
    w.u32 (static_cast<std::uint32_t> (std::tuple_size<mixology::proc::Values>::value));
    for (const auto& [hash, v] : procAtAnalysis)
    {
        w.u32 (hash);
        for (float x : v) w.f32 (x);
    }
    w.flag (mixProcAtAnalysisKnown);
    for (float x : mixProcAtAnalysis) w.f32 (x);
    // Since 0.16: every Link's Assist EQ when Mixology listened (a project from 0.14 / 0.15 ends before this).
    w.u32 (static_cast<std::uint32_t> (assistAtAnalysis.size()));
    for (const auto& [hash, a] : assistAtAnalysis)
    {
        w.u32 (hash);
        for (float x : { a.lowCutHz, a.mudHz, a.mudDb, a.pocketHz, a.pocketDb }) w.f32 (x);
    }
    return w.take();
}

bool BridgeHub::loadAdvice (const std::string& bytes)
{
    mixology::pack::Reader r (bytes);
    if (r.u32() != kAdviceMagic) return false;
    mixology::mix::Analysis heard;
    if (! heard.load (r.str()) || ! heard.hasMoments()) return false;
    const double at = r.f64();
    std::vector<std::string> unheard (r.count (4));
    for (auto& n : unheard) n = r.str();
    AtAnalysis snap;
    for (std::uint32_t i = 0, n = r.count (12); i < n; ++i)
    {
        const auto hash = r.u32();
        snap.faders[hash] = r.f64();
    }
    const auto procs = r.count (4);
    const auto size = r.u32();
    constexpr auto kValues = std::tuple_size<mixology::proc::Values>::value;
    if (size != kValues) return false;   // saved by a Mixology with other controls: don't guess
    for (std::uint32_t i = 0; i < procs && r.ok(); ++i)
    {
        const auto hash = r.u32();
        auto& v = snap.procs[hash];
        for (float& x : v) x = r.f32();
    }
    snap.mixKnown = r.flag();
    for (float& x : snap.mix) x = r.f32();
    const bool assistKept = ! r.atEnd();
    if (assistKept)
        for (std::uint32_t i = 0, n = r.count (24); i < n && r.ok(); ++i)
        {
            const auto hash = r.u32();
            auto& a = snap.assist[hash];
            a.lowCutHz = r.f32(); a.mudHz = r.f32(); a.mudDb = r.f32(); a.pocketHz = r.f32(); a.pocketDb = r.f32();
        }
    if (! r.ok()) return false;

    analysis = std::move (heard);
    analysis.setVocalOffset (vocalOffset, vocalFromYou);
    useSnapshot (std::move (snap));
    if (! assistKept)
    {
        // Saved by Mixology 0.14 / 0.15: the Assist EQ it heard wasn't kept. The project opened as it was saved,
        // so what each Link has now is the best guess (a dip you make from here on counts as new).
        assistGuessed = true;
        guessAssistAtAnalysis();
    }
    applyMusicBus (analysis, fadersAtAnalysis);
    report = analysis.evaluate (settingsForAnalysis(), unheard);
    if (! report.valid) { report = {}; analysis.clear(); return false; }
    analyzeState = Analyze::done;
    autoListen.start (autoStyleKnown ? autoStyle : report.style);   // Auto Analyze carries on from it (when it's on)
    analyzedMs = at;
    adviceFromProject = true;
    return true;
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
        if (ts.role || ts.excluded || ts.reference || ts.musicBus || ts.noPan || ts.vocalOffset)
            lines.add (juce::String (static_cast<juce::int64> (hash)) + "\t" + (ts.role ? mixology::mix::roleKey (*ts.role) : "") + "\t" + (ts.excluded ? "1" : "0")
                       + "\t" + (ts.reference ? "1" : "0") + "\t" + (ts.musicBus ? "1" : "0") + "\t" + (ts.noPan ? "1" : "0")
                       + "\t" + (ts.vocalOffset ? juce::String (*ts.vocalOffset, 2) : juce::String()));
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

    if (analyzeState == Analyze::done || analyzeState == Analyze::idle) { autoTick (mixFrame, std::move (tracks), playing); return; }
    if (analyzeState != Analyze::listening || ! playing) return;
    mix::Moment m;
    m.mix = mixFrame;
    m.tracks = std::move (tracks);
    analysis.add (std::move (m));
    ++analysisVersion;
    if (analysis.secondsHeard() >= 60.0 || analysis.full()) finishAnalysis();
}

void BridgeHub::restoreAssist (std::uint32_t hash, int& restored, int& kept, std::vector<mixology::history::Change>* changes)
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
        if (d.action == mixology::AssistRestore::Action::restore)
        {
            if (changes != nullptr)
                for (auto& c : assistChanges (*l, l->assist, d.to)) changes->push_back (std::move (c));
            registry.requestAssist (l->slot, l->generation, d.to);
            ++restored;
        }
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

void BridgeHub::applyMusicBus (mixology::mix::Analysis& a, const std::map<std::uint32_t, double>& faders) const
{
    for (const auto& [hash, ts] : trackSettings)
        if (ts.musicBus)
        {
            double db = 0.0;
            if (auto it = faders.find (hash); it != faders.end()) db = it->second;
            else if (auto kf = bridge.knownFaders().find (hash); kf != bridge.knownFaders().end()) db = kf->second.db;
            a.setMusicBus (true, std::max (db, -120.0));
            return;
        }
    a.setMusicBus (false, 0.0);
}

void BridgeHub::reevaluate()
{
    // While Analyze Mix listens again, the advice on screen still comes from the moments behind it.
    const bool listeningAgain = (analyzeState == Analyze::reading || analyzeState == Analyze::listening) && adviceAnalysis.hasMoments();
    auto& heard = listeningAgain ? adviceAnalysis : analysis;
    if (! report.valid || ! heard.hasMoments()) return;
    heard.setVocalOffset (vocalOffset, vocalFromYou);
    applyMusicBus (heard, fadersAtAnalysis);
    // The music group isn't a channel of its own music: never "without a Link".
    std::vector<std::string> unheard;
    for (const auto& n : report.unheard)
    {
        const auto ts = trackSettings.find (mixology::bridge::nameHash (n));
        if (ts == trackSettings.end() || (! ts->second.musicBus && ! ts->second.excluded)) unheard.push_back (n);
    }
    report = heard.evaluate (settingsForAnalysis(), unheard);
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
    const auto vocals = link.vocals (ms);
    std::map<std::uint32_t, suite::VocalInfo> merged;   // two Voxologys on one channel: what both do
    for (const auto& v : vocals)
    {
        if (v.nameHash == 0) continue;
        auto [it, fresh] = merged.try_emplace (v.nameHash, v);
        if (fresh) continue;
        auto& m = it->second;
        // Beat mode only when every Voxology there is in it (a vocal one makes it a vocal); the rest adds up.
        const auto beat = static_cast<std::uint32_t> (suite::VocalInfo::beat);
        const bool bothBeat = (m.flags & beat) != 0 && (v.flags & beat) != 0;
        m.flags = ((m.flags | v.flags) & ~beat) | (bothBeat ? beat : 0u);
        m.compressionDb = std::max (m.compressionDb, v.compressionDb);
        m.reverbMix = std::max (m.reverbMix, v.reverbMix);
        m.delayMix = std::max (m.delayMix, v.delayMix);
        m.recordedPeakDb = std::max (m.recordedPeakDb, v.recordedPeakDb);
        m.clippedSpots = std::max (m.clippedSpots, v.clippedSpots);
        if (m.style == suite::VocalInfo::Style::unknown) m.style = v.style;
    }
    for (const auto& [hash, v] : merged) suiteVocals[hash] = { v, t };

    // Answer every Voxology (each one, also two on one track): did Mixology find that track and treat it as a vocal
    // (and if not, why), and what does it hear there. Voxology says "MIXOLOGY ✓" only for a match.
    {
        using State = suite::VocalAck::State;
        const auto links = mixology::link::processRegistry().links (mixology::link::nowMs());
        const auto shared = sharedNames();
        const bool connected = bridge.link() == mixology::BridgeSession::Link::connected;
        for (const auto& v : vocals)
        {
            suite::VocalAck a;
            const auto hash = v.nameHash;
            const auto role = hash != 0 ? roleFor (hash, v.trackName) : std::nullopt;
            if (hash == 0) a.state = State::noName;
            else if ((v.flags & suite::VocalInfo::beat) != 0) a.state = State::beatTrack;
            else if (shared.count (hash) != 0) a.state = State::sharedName;
            else if (role && *role != mixology::mix::Role::vox && *role != mixology::mix::Role::adlibs) a.state = State::notVocal;
            else if (uniqueLink (links, hash) == nullptr) a.state = State::noLink;
            else a.state = State::matched;
            if (a.state == State::matched)
            {
                if (connected && bridge.knownFaders().count (hash) != 0 && ! bridge.isProtected (hash))
                    a.bits |= suite::VocalAck::balancesLevel;
                if (report.valid)
                    for (const auto& tr : report.tracks)
                        if (tr.hash == hash && tr.hasVocalRatio)
                        {
                            a.bits |= suite::VocalAck::heard;
                            a.vocalDb = static_cast<float> (tr.vocalRatio);
                            a.targetDb = static_cast<float> (tr.vocalTarget);
                        }
            }
            link.ackVocal (v, a, ms);
        }
    }
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

void BridgeHub::suiteTick()
{
    // The hub's own timer isn't running yet (a DAW without a bridge, window never opened): Voxology still gets its
    // answers and Mixology still knows the vocals.
    if (enabled) return;
    const double t = now();
    if (t - lastSuiteUpdate < 0.25) return;
    lastSuiteUpdate = t;
    updateSuite (t);
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
    for (const auto& [hash, name] : sharedNames())
        c.sharedNames.push_back (titleOf (hash, name));
    {
        const juce::ScopedLock sl (protectedLock);
        std::set<std::uint64_t> others;
        for (const auto& [o, i] : instances)
            if (i.project() != activeProject) others.insert (i.project());
        c.otherProjects = static_cast<int> (others.size());
    }

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
    using namespace mixology;
    // A mono mix as the setup check hears it right now (else as Analyze Mix heard it).
    if (enabled ? monoWatch.monoFold() : (report.valid && report.monoMix)) m.flags |= suite::MixInfo::monoMix;
    if (! report.valid || report.style.key != styleKey) return false;
    const auto tone = mix::toneAgainstStyle (report);
    if (! tone.valid) return false;
    // Analyze Mix hears the mix before the Full Mix's processing; Masterology hears it after: add what Mixology's
    // own EQ and Tone Balance do in each region.
    const auto v = mixProc.get ? mixProc.get() : proc::Values {};
    auto added = [&v] (int from, int to) {
        constexpr double rate = 48000.0;
        double sum = 0.0;
        for (int b = from; b <= to; ++b)
        {
            const double hz = bandHz (b);
            double db = 0.0;
            if (proc::isOn (v, proc::Module::eq)) db += proc::Chain::eqResponseDb (v, hz, rate);
            if (proc::isOn (v, proc::Module::tone))
            {
                db += dsp::magnitudeDb (dsp::design (dsp::Shape::lowShelf, 220.0, v[proc::tbWarmth], 0.6, rate), hz, rate);
                db += dsp::magnitudeDb (dsp::design (dsp::Shape::bell, 3200.0, v[proc::tbPresence], 0.8, rate), hz, rate);
                db += dsp::magnitudeDb (dsp::design (dsp::Shape::highShelf, 11000.0, v[proc::tbAir], 0.6, rate), hz, rate);
            }
            sum += db;
        }
        return sum / (to - from + 1);
    };
    m.flags |= suite::MixInfo::analysed;
    m.lowDb = static_cast<float> (tone.low + added (3, 8));
    m.lowMidDb = static_cast<float> (tone.lowMid + added (10, 14));
    m.presenceDb = static_cast<float> (tone.presence + added (21, 23));
    m.airDb = static_cast<float> (tone.air + added (26, 29));
    return true;
}

std::optional<suite::MasterInfo> BridgeHub::masterology() const
{
    if (suiteMasters.empty()) return std::nullopt;
    return suiteMasters.front();
}

std::vector<std::string> BridgeHub::unheardNow() const
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
    return unheard;
}

BridgeHub::AtAnalysis BridgeHub::snapshotNow() const
{
    AtAnalysis at;
    for (const auto& [hash, f] : bridge.knownFaders()) at.faders[hash] = f.db;
    // The processing each Link (and the Full Mix) had while Mixology listened: Polish works from there.
    auto& registry = mixology::link::processRegistry();
    const auto links = registry.links (mixology::link::nowMs());
    for (const auto& l : links)
    {
        mixology::proc::Values v {};
        if (l.identity.hasName && uniqueLink (links, l.nameHash) != nullptr && registry.readProc (l.slot, l.generation, v))
            at.procs[l.nameHash] = v;
        if (l.identity.hasName && uniqueLink (links, l.nameHash) != nullptr) at.assist[l.nameHash] = l.assist;
    }
    at.mixKnown = static_cast<bool> (mixProc.get);
    if (at.mixKnown) at.mix = mixProc.get();
    return at;
}

void BridgeHub::useSnapshot (AtAnalysis at)
{
    fadersAtAnalysis = std::move (at.faders);
    procAtAnalysis = std::move (at.procs);
    assistAtAnalysis = std::move (at.assist);
    assistGuessed = false;
    mixProcAtAnalysisKnown = at.mixKnown;
    mixProcAtAnalysis = at.mix;
}

void BridgeHub::finishAnalysis()
{
    const auto unheard = unheardNow();
    useSnapshot (snapshotNow());
    applyMusicBus (analysis, fadersAtAnalysis);
    report = analysis.evaluate (settingsForAnalysis(), unheard);
    analyzeState = Analyze::done;
    autoListen.start (report.style);   // Auto Analyze carries on from here (when it's on)
    adviceAnalysis = {};
    analyzedMs = static_cast<double> (juce::Time::currentTimeMillis());
    adviceFromProject = false;
    ++adviceSerial;
    ++analysisVersion;
}

mixology::mix::Style BridgeHub::styleFromVar (const juce::var& st)
{
    mixology::mix::Style style;
    style.key = st.getProperty ("key", "hiphop.modern").toString().toStdString();
    style.name = st.getProperty ("name", "Hip Hop - Modern Clean").toString().toStdString();
    style.bass = st.getProperty ("bass", 4.0);
    style.scoop = st.getProperty ("scoop", 1.5);
    style.air = st.getProperty ("air", 2.0);
    return style;
}

std::uint64_t BridgeHub::autoMixKey() const
{
    // FNV-1a over everything that changes the sound but isn't a fader: every Link's processing and Assist EQ,
    // the Full Mix's processing, and Mixology's own changes (the History, plans applied).
    std::uint64_t h = 1469598103934665603ull;
    auto add = [&h] (const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*> (p);
        for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    const auto hv = history.version();
    add (&hv, sizeof hv);
    add (&applyVersion, sizeof applyVersion);
    auto& registry = mixology::link::processRegistry();
    for (const auto& l : registry.links (mixology::link::nowMs()))
    {
        mixology::proc::Values v {};
        if (registry.readProc (l.slot, l.generation, v)) add (v.data(), v.size() * sizeof (float));
        const float a[] = { l.assist.lowCutHz, l.assist.mudHz, l.assist.mudDb, l.assist.pocketHz, l.assist.pocketDb };
        add (a, sizeof a);
    }
    if (mixProc.get)
    {
        const auto v = mixProc.get();
        add (v.data(), v.size() * sizeof (float));
    }
    return h;
}

void BridgeHub::autoTick (const mixology::AnalysisFrame& mixFrame, std::vector<mixology::mix::TrackSample> tracks, bool playing)
{
    using namespace mixology;
    juce::String pause;
    // From scratch (the project opened with Auto Analyze on, no advice yet): read every fader once, like Analyze Mix.
    if (autoAnalyze && ! report.valid && autoStyleKnown && ! autoScanAsked && playing
        && bridge.link() == BridgeSession::Link::connected && bridge.channelOrder().empty() && ! bridge.busy())
    {
        autoScanAsked = true;
        bridge.scanAllChannels (now());
    }
    if (! autoAnalyze || (! report.valid && ! autoStyleKnown)) pause = "off";
    else if (bridge.applying() || bridge.scanning() || hearingReference) { pause = "busy"; autoListen.reset(); }   // the mix is changing
    else if (! playing) pause = "stopped";
    else if (mixFrame.silent) pause = "quiet";
    if (pause != autoPause) { autoPause = pause; ++analysisVersion; }
    if (pause.isNotEmpty()) return;

    mix::Moment m;
    m.mix = mixFrame;
    m.tracks = std::move (tracks);
    const auto step = autoListen.add (std::move (m), autoMixKey());
    if (step == mix::AutoAnalyze::Step::restarted) autoSnapshot = snapshotNow();
    if (step == mix::AutoAnalyze::Step::check)
    {
        auto& a = autoListen.analysis();
        a.setVocalOffset (vocalOffset, vocalFromYou);
        applyMusicBus (a, autoSnapshot.faders);
        auto fresh = a.evaluate (settingsForAnalysis(), unheardNow());
        autoCheckedMs = juce::Time::currentTimeMillis();
        if (mix::adviceChanged (report, fresh))
        {
            report = std::move (fresh);
            analysis = a;   // role changes re-evaluate the moments behind the advice on screen
            useSnapshot (autoSnapshot);
            autoUpdatedMs = autoCheckedMs;
            analyzeState = Analyze::done;   // the first advice, when Auto Analyze started from scratch
            analyzedMs = autoCheckedMs;
            adviceFromProject = false;
            ++adviceSerial;
        }
        else if (adviceFromProject)
        {
            // The advice from the project still holds for the mix as it sounds now: it's today's advice.
            adviceFromProject = false;
            autoUpdatedMs = autoCheckedMs;
        }
        ++analysisVersion;
    }
    const int shown = static_cast<int> (autoListen.secondsHeard());
    if (shown != autoSecondsShown) { autoSecondsShown = shown; ++analysisVersion; }
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
    {
        auto* au = new juce::DynamicObject();
        au->setProperty ("on", autoAnalyze);
        au->setProperty ("pause", autoPause);   // "" listening, "stopped", "quiet", "busy", "off"
        au->setProperty ("seconds", autoSecondsShown < 0 ? 0 : autoSecondsShown);
        au->setProperty ("checked", autoCheckedMs);
        au->setProperty ("updated", autoUpdatedMs);
        o->setProperty ("auto", juce::var (au));
    }
    o->setProperty ("analyzed", analyzedMs);          // when the advice on screen was made
    o->setProperty ("fromProject", adviceFromProject); // ... and it came back with the project

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
            if (t.hasVocalRatio)   // a lead vocal track on its own: where it sits while it sings, where you like it
            {
                to->setProperty ("vocalRatio", std::round (t.vocalRatio * 10.0) / 10.0);
                to->setProperty ("vocalTarget", std::round (t.vocalTarget * 10.0) / 10.0);
                to->setProperty ("vocalOwn", t.vocalOwn);
            }
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
        for (const auto& sg : report.suggestions)
        {
            auto v = suggestionVar (sg);
            if (auto* so = v.getDynamicObject())
            {
                so->setProperty ("tool", juce::String (toolForPoint (sg.id)));   // the smart tool that fixes it
                if (auto it = donePoints.find (sg.id); it != donePoints.end())
                {
                    so->setProperty ("done", true);   // ... and it did, since Mixology listened
                    so->setProperty ("doneWhat", juce::String::fromUTF8 (it->second.what.c_str()));
                }
            }
            sugs.add (v);
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
    o->setProperty ("via", mcu != nullptr ? "mcu" : "script");   // Logic Pro / Cakewalk: Mixology is their Mackie Control
    o->setProperty ("dawSeen", mcu != nullptr && mcu->dawSeen());
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
            so->setProperty ("stereoPanner", dawKey == "cubase" && sl.channels == 2 && s.hasPan && s.pan <= 0.0005);
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
    o->setProperty ("history", historyState());
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
    // The Tracks page's scroll bar: every channel in MixConsole order (from the last fader scan) and where the bank is.
    o->setProperty ("viewOffset", bridge.viewOffset());
    o->setProperty ("channelNames", stringList (bridge.channelTitles()));
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

    // The undo history: what a strip command is about to change, from the strip as it is now (recorded below
    // once it went through). Moves on one channel within a few seconds join one step (a drag, a pan turned).
    std::vector<HChange> hist;
    std::string histWhat, histGroup;
    const bool stripShown = ref.slot >= 0 && ref.slot < bridge.slotCount() && bridge.slot (ref.slot).present
                            && bridge.slot (ref.slot).hash == ref.hash;
    if (stripShown)
    {
        const auto& s = bridge.slot (ref.slot);
        const auto title = "'" + s.title + "'";
        const auto group = ":" + std::to_string (s.hash);
        auto add = [&] (HWhat what, double before, double after, const std::string& label, const std::string& key) {
            auto c = channelChange (what, s.hash, s.title);
            c.before = before;
            c.after = after;
            hist.push_back (c);
            histWhat = label + " " + title;
            histGroup = key + group;
        };
        if (name == "setDb" || name == "minusInf" || name == "nudge" || name == "fader")
        {
            // Nudges in a row (mouse wheel) add up from where the fader is heading, as the bridge does.
            const double nudgeFrom = bridge.movingToDb (ref.slot).value_or (s.volumeDb);
            const double db = name == "setDb" ? number : name == "minusInf" ? mixology::FaderLaw::kMinusInf
                            : name == "nudge" ? std::round ((nudgeFrom + number) * 100.0) / 100.0 : bridge.faderLaw().toDb (number);
            add (HWhat::fader, s.volumeDb, db, "Fader", "fader");
            hist.back().beforePos = s.volume;
            if (name == "fader") hist.back().afterPos = number;
        }
        else if (name == "pan" && s.hasPan) add (HWhat::pan, s.pan, number, "Pan", "pan");
        else if (name == "mute") add (HWhat::mute, s.mute ? 1.0 : 0.0, static_cast<bool> (value) ? 1.0 : 0.0, static_cast<bool> (value) ? "Mute" : "Unmute", "mute");
        else if (name == "solo") add (HWhat::solo, s.solo ? 1.0 : 0.0, static_cast<bool> (value) ? 1.0 : 0.0, static_cast<bool> (value) ? "Solo" : "Solo off", "solo");
        else if (name == "trim" && ref.slot < static_cast<int> (stripLinks.size()))
        {
            const auto& sl = stripLinks[static_cast<std::size_t> (ref.slot)];
            add (HWhat::trim, sl.trimDb, juce::jlimit (-24.0, 24.0, number), "Trim", "trim");
            hist.back().slot = sl.slot;
            hist.back().generation = sl.generation;
        }
        else if (name == "restore")
        {
            if (const auto p = bridge.restorePoints().find (s.hash); p != bridge.restorePoints().end())
            {
                const auto& rp = p->second;
                add (HWhat::fader, s.volumeDb, bridge.faderLaw().toDb (rp.volume), "Restore", "");
                hist.back().beforePos = s.volume;
                hist.back().afterPos = rp.volume;
                if (rp.hasPan && s.hasPan) add (HWhat::pan, s.pan, rp.pan, "Restore", "");
                add (HWhat::mute, s.mute ? 1.0 : 0.0, rp.mute ? 1.0 : 0.0, "Restore", "");
                add (HWhat::solo, s.solo ? 1.0 : 0.0, rp.solo ? 1.0 : 0.0, "Restore", "");
            }
            histWhat = "Restore " + title;
            histGroup.clear();
        }
    }
    std::vector<HChange> restoreAllHist;
    if (name == "restoreAll")
        for (const auto& [hash, p] : bridge.restorePoints())
        {
            const auto kf = bridge.knownFaders().find (hash);
            if (p.ambiguous || bridge.isProtected (hash) || kf == bridge.knownFaders().end() || ! bridge.isChanged (p)) continue;
            const auto& f = kf->second;
            auto c = channelChange (HWhat::fader, hash, p.title);
            c.before = f.db;
            c.beforePos = f.volume;
            c.after = bridge.faderLaw().toDb (p.volume);
            c.afterPos = p.volume;
            restoreAllHist.push_back (c);
            if (p.hasPan && f.hasPan)
            {
                auto pc = channelChange (HWhat::pan, hash, p.title);
                pc.before = f.pan;
                pc.after = p.pan;
                restoreAllHist.push_back (pc);
            }
            for (auto [what, was, to] : { std::tuple { HWhat::mute, f.mute, p.mute }, std::tuple { HWhat::solo, f.solo, p.solo } })
            {
                auto mc = channelChange (what, hash, p.title);
                mc.before = was ? 1.0 : 0.0;
                mc.after = to ? 1.0 : 0.0;
                restoreAllHist.push_back (mc);
            }
        }

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
        const bool hadFaderPoint = bridge.restorePoints().count (ref.hash) != 0;
        if (hadFaderPoint) r = bridge.restoreChannel (ref, t);
        // The Assist EQ only when the fader part went through (else nothing changes and the History stays right).
        if ((! hadFaderPoint || r.ok) && valid && ! bridge.isProtected (ref.hash)) restoreAssist (ref.hash, eqBack, eqKept, &hist);
        if (! hadFaderPoint && eqBack + eqKept == 0) r = bridge.restoreChannel (ref, t);   // nothing to restore: its own answer
        if (eqBack > 0) info = "Assist EQ back to how it was.";
        else if (eqKept > 0) info = "Its Assist EQ was changed since Mixology set it: left as you set it.";
        if (! hadFaderPoint && eqBack + eqKept > 0) r = { true, {} };
        ++applyVersion;
    }
    else if (name == "restoreAll")
    {
        const auto sum = bridge.restoreAll (t);
        int eqBack = 0, eqKept = 0;
        if (sum.restored + sum.outsideBank > 0) hist = std::move (restoreAllHist);
        restoreAssist (0, eqBack, eqKept, &hist);
        histWhat = "Restore all";
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
        const auto style = styleFromVar (value);
        if (bridge.applying())
            r = { false, "Mixology is still applying " + bridge.planProgress().what + ": try again in a second." };
        else if (mixology::link::processRegistry().links (mixology::link::nowMs()).empty())
            r = { false, "No Mixology Link found. Insert Mixology Link on your tracks (after their other plug-ins), then press Analyze Mix." };
        else
        {
            if (report.valid && analyzeState == Analyze::done) adviceAnalysis = analysis;   // Cancel goes back to it
            analysis.begin (style);
            analysis.setVocalOffset (vocalOffset, vocalFromYou);
            autoListen.reset();
            if (bridge.link() == mixology::BridgeSession::Link::connected && bridge.scanAllChannels (t).ok)
                analyzeState = Analyze::reading;   // first every channel's fader, then listen
            else
                analyzeState = Analyze::listening;
            ++analysisVersion;
        }
    }
    else if (name == "autoStyle")
    {
        // The interface's style, at start and on every change. Auto Analyze listens for it (and a new style is new
        // advice at the next check). Analyze Mix's own listening is left alone.
        const auto st = styleFromVar (value);
        auto differs = [] (double a, double b) { return std::abs (a - b) > 1e-6; };
        const bool changed = ! autoStyleKnown || st.key != autoStyle.key || differs (st.bass, autoStyle.bass)
                             || differs (st.scoop, autoStyle.scoop) || differs (st.air, autoStyle.air);
        autoStyle = st;
        autoStyleKnown = true;
        if (changed && analyzeState != Analyze::reading && analyzeState != Analyze::listening)
        {
            autoListen.start (autoStyle);
            ++analysisVersion;
        }
    }
    else if (name == "autoAnalyze")
    {
        autoAnalyze = static_cast<bool> (value);
        autoListen.reset();
        autoCheckedMs = autoUpdatedMs = 0.0;
        autoSecondsShown = -1;
        cachePrefs();
        ++analysisVersion;
        if (autoAnalyze && ! report.valid) info = "Auto Analyze is on: press play and it listens by itself.";
    }
    else if (name == "analyzeFinish")
    {
        if (analyzeState != Analyze::listening) r = { false, "Analyze Mix isn't listening right now." };
        else if (! analysis.hasMoments()) r = { false, "Nothing heard yet: press play in {daw}." };
        else finishAnalysis();
    }
    else if (name == "analyzeCancel")
    {
        // The advice on screen stays, and so do the moments behind it (a role change re-evaluates those).
        if (report.valid && adviceAnalysis.hasMoments()) analysis = std::move (adviceAnalysis);
        adviceAnalysis = {};
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
            if (hash == 0 && ! wanted.empty()) hash = mixology::bridge::nameHash (wanted);   // not read from the DAW (yet)
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
    else if (name == "vocalTaste" && static_cast<juce::int64> (arg.getProperty ("hash", 0)) != 0)
    {
        // Your ears for one vocal track: its own aim (a verse and a hook on their own tracks can want different levels).
        const auto hash = static_cast<std::uint32_t> (static_cast<juce::int64> (arg.getProperty ("hash", 0)));
        const auto taste = value.toString();
        const mixology::mix::TrackReport* t = nullptr;
        for (const auto& x : report.tracks)
            if (x.hash == hash) t = &x;
        const std::string quotedName = t != nullptr ? "'" + t->name + "'" : std::string ("That track");
        if (taste == "style")
        {
            trackSettings[hash].vocalOffset.reset();
            info = quotedName + " follows the whole vocal's setting again.";
        }
        else if (t == nullptr || ! t->hasVocalRatio)
            r = { false, quotedName + " didn't sing while Mixology listened: play a part where it sings, then Analyze Mix again." };
        else
        {
            const double step = taste == "quieter" ? -2.5 : taste == "louder" ? 2.5 : 0.0;
            trackSettings[hash].vocalOffset = juce::jlimit (-24.0, 24.0, t->vocalRatio + step - report.styleVocalTarget);
            info = taste == "right" ? "Got it: Mixology keeps " + quotedName + " where it is (saved with the project)."
                 : "Got it: Balance and Auto Mix now aim for " + quotedName + " 2.5 dB " + (step < 0 ? "lower" : "higher") + " than now (saved with the project).";
        }
        if (r.ok)
        {
            cacheTrackSettings();
            reevaluate();
            ++analysisVersion;
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
    else if (name == "bankTo")    r = bridge.showChannel (static_cast<int> (number), t);   // the Tracks page's scroll bar
    else if (name == "readChannels") r = bridge.scanAllChannels (t);                      // learns the channels' order
    else if (name == "reconnect")
    {
        closePorts ("Reconnect pressed");
        portProblem.clear();
        openPorts();
    }
    else if (name == "copyLog")   juce::SystemClipboard::copyTextToClipboard (logText());
    else if (name == "history")   planResult = historyList();
    else if (name == "historyTo" || name == "historyUndo" || name == "historyRedo")
    {
        r = moveHistory (name, static_cast<std::uint64_t> (static_cast<juce::int64> (number)), info);
        planResult = historyList();
    }
    else                          r = { false, "Unknown command." };

    if (r.ok && ! hist.empty()) history.record (histWhat, histGroup, std::move (hist), wallClock());

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", r.ok);
    o->setProperty ("message", juce::String::fromUTF8 (r.message.c_str()));
    o->setProperty ("warning", juce::String::fromUTF8 (r.warning.c_str()));
    o->setProperty ("info", juce::String::fromUTF8 (info.c_str()));
    if (! planResult.isVoid()) o->setProperty (name == "planUndo" ? "undo" : name.startsWith ("history") ? "history" : "plan", planResult);
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

// ---------------------------------------------------------------------------------------------
// The Mixology instances, and the project each belongs to

bool BridgeHub::looksLikeMaster (const juce::String& trackName)
{
    static const std::regex master (R"(^\s*(stereo\s*out(put)?|master|main(\s*out(put)?)?|mix\s*bus|2[\s-]*bus|output(\s*1)?)\b)", std::regex::icase);
    return std::regex_search (trackName.toStdString(), master);
}

void BridgeHub::registerInstance (const void* owner, Instance in)
{
    {
        const juce::ScopedLock sl (protectedLock);
        instances.emplace_back (owner, std::move (in));
    }
    chooseMixOwner();
}

void BridgeHub::unregisterInstance (const void* owner)
{
    std::uint64_t next = 0;
    bool activeGone = false;
    {
        const juce::ScopedLock sl (protectedLock);
        instances.erase (std::remove_if (instances.begin(), instances.end(), [owner] (const auto& i) { return i.first == owner; }), instances.end());
        auto alive = [this] (std::uint64_t id) {
            return std::any_of (instances.begin(), instances.end(), [id] (const auto& i) { return i.second.project() == id; });
        };
        for (auto it = parked.begin(); it != parked.end();)   // a project closed: nothing of it to keep
            it = alive (it->first) ? std::next (it) : parked.erase (it);
        if (activeProject != 0 && ! alive (activeProject))
        {
            activeGone = true;
            juce::int64 latest = -1;
            for (const auto& [o, i] : instances)   // the open project used most recently comes back
                if (const auto when = std::max (i.lastAudioMs(), i.lastSeenMs()); when > latest) { latest = when; next = i.project(); }
        }
    }
    if (owner == mixOwner) { mixOwner = nullptr; mixProc = {}; }
    if (activeGone)
    {
        if (next != 0) switchProject (next);
        else
        {
            resetProject();
            const juce::ScopedLock sl (protectedLock);
            activeProject = 0;
        }
    }
    chooseMixOwner();
}

void BridgeHub::chooseMixOwner()
{
    const void* best = nullptr;
    MixProcessing proc;
    {
        const juce::ScopedLock sl (protectedLock);
        int bestScore = -1;
        for (const auto& [o, i] : instances)   // oldest first: the first with the best score wins
        {
            const int score = (activeProject == 0 || i.project() == activeProject ? 2 : 0) + (looksLikeMaster (i.trackName()) ? 1 : 0);
            if (score > bestScore) { bestScore = score; best = o; proc = i.processing; }
        }
    }
    if (best == mixOwner) return;
    mixOwner = best;
    mixProc = std::move (proc);
    ++analysisVersion;
}

std::uint64_t BridgeHub::idForNewInstance()
{
    const auto t = juce::Time::currentTimeMillis();
    const juce::ScopedLock sl (protectedLock);
    // Added to the project playing or on screen right now: it is part of it.
    if (activeProject != 0)
        for (const auto& [o, i] : instances)
            if (i.project() == activeProject && t - std::max (i.lastAudioMs(), i.lastSeenMs()) < 2000) return activeProject;
    std::uint64_t id = 0;
    while (id == 0 || id == activeProject || parked.count (id) != 0)
        id = (static_cast<std::uint64_t> (static_cast<std::uint32_t> (juce::Random::getSystemRandom().nextInt())) << 32)
             ^ static_cast<std::uint32_t> (juce::Random::getSystemRandom().nextInt()) ^ static_cast<std::uint64_t> (t);
    if (activeProject == 0) activeProject = id;   // the first Mixology: its project is the one on screen
    return id;
}

void BridgeHub::loadProject (const void* owner, std::uint64_t previous, std::uint64_t id, const ProjectState& ps)
{
    bool apply = false;
    {
        const juce::ScopedLock sl (protectedLock);
        // A new Mixology was the only one of its (new) project: its project is the one it loads now.
        if (previous != id && activeProject == previous
            && std::none_of (instances.begin(), instances.end(), [&] (const auto& i) { return i.first != owner && i.second.project() == previous; }))
            activeProject = id;
        if (activeProject == 0) activeProject = id;
        apply = activeProject == id;
        if (! apply) parked[id] = ps;   // another project is on screen: this one waits until you switch to it
        else parked.erase (id);
    }
    if (! apply) return;
    setProtectedState (ps.protectedText);
    setTrackSettingsState (ps.tracks);
    setMixPrefsState (ps.prefs);
    setSessionState (ps.session);
}

BridgeHub::ProjectState BridgeHub::saveProject (std::uint64_t id, bool& known)
{
    {
        const juce::ScopedLock sl (protectedLock);
        known = true;
        if (id != activeProject)
        {
            if (const auto it = parked.find (id); it != parked.end()) return it->second;
            if (activeProject != 0) { known = false; return {}; }
        }
    }
    return liveProject();
}

BridgeHub::ProjectState BridgeHub::liveProject()
{
    return { protectedState(), trackSettingsState(), mixPrefsState(), sessionState() };
}

void BridgeHub::updateProjects()
{
    const auto t = juce::Time::currentTimeMillis();
    std::uint64_t switchTo = 0;
    std::vector<ProjectState> merges;
    {
        const juce::ScopedLock sl (protectedLock);
        if (activeProject == 0 || instances.empty()) return;
        juce::int64 activeAudio = 0;
        for (const auto& [o, i] : instances)
            if (i.project() == activeProject) activeAudio = std::max (activeAudio, i.lastAudioMs());

        if (t - activeAudio < 500)
        {
            // A DAW plays one project at a time: a Mixology with another id that plays right now belongs to the
            // project on screen (one added while the song played, or two from a project saved before 0.20).
            for (auto& [o, i] : instances)
            {
                const auto old = i.project();
                if (old == activeProject || t - i.lastAudioMs() >= 500) continue;
                i.adopt (activeProject);
                const bool last = std::none_of (instances.begin(), instances.end(), [old] (const auto& x) { return x.second.project() == old; });
                if (const auto it = parked.find (old); last && it != parked.end())
                {
                    merges.push_back (it->second);
                    parked.erase (it);
                }
            }
        }
        else if (t - activeAudio > 1500)
        {
            // The project on screen went quiet and another one plays: you switched project. (Audio only: a window
            // opened while the song is stopped may be a second Mixology in this same project.)
            juce::int64 latest = t - 500;
            for (const auto& [o, i] : instances)
                if (i.project() != activeProject && i.lastAudioMs() > latest) { latest = i.lastAudioMs(); switchTo = i.project(); }
        }
    }
    for (const auto& ps : merges) mergeProject (ps);
    if (switchTo != 0) switchProject (switchTo);
    chooseMixOwner();
}

void BridgeHub::mergeProject (const ProjectState& ps)
{
    // Only what the project on screen doesn't have yet (it is the same project under another id).
    if (protectedState().isEmpty() && ps.protectedText.isNotEmpty()) setProtectedState (ps.protectedText);
    if (trackSettingsState().isEmpty() && ps.tracks.isNotEmpty()) setTrackSettingsState (ps.tracks);
    if (! vocalFromYou && ! autoAnalyze && ps.prefs.isNotEmpty()) setMixPrefsState (ps.prefs);
    if (ps.session.isNotEmpty()) setSessionState (ps.session);   // each part only fills what is empty
}

void BridgeHub::switchProject (std::uint64_t to)
{
    const auto live = liveProject();
    ProjectState next;
    bool found = false;
    {
        const juce::ScopedLock sl (protectedLock);
        const auto from = activeProject;
        if (from != 0 && std::any_of (instances.begin(), instances.end(), [from] (const auto& i) { return i.second.project() == from; }))
            parked[from] = live;
        if (const auto it = parked.find (to); it != parked.end())
        {
            next = it->second;
            found = true;
            parked.erase (it);
        }
        activeProject = to;
    }
    resetProject();
    if (found)
    {
        setProtectedState (next.protectedText);
        setTrackSettingsState (next.tracks);
        setMixPrefsState (next.prefs);
        setSessionState (next.session);
    }
    bridge.note (now(), "Another project is on screen: Mixology shows its own History, advice and padlocks.");
}

void BridgeHub::resetProject()
{
    // Everything that belongs to one project goes; what belongs to the host (the bridge, the Links, the suite,
    // the learned fader curve, your presets) stays.
    {
        const juce::ScopedLock sl (protectedLock);
        hasPendingProtected = hasPendingTrackSettings = hasPendingPrefs = hasPendingSession = false;
    }
    bridge.setProtectedList ({});
    cacheProtected();
    trackSettings.clear();
    bridge.setNoPanList ({});
    seenNoPanVersion = bridge.noPanVersion();
    cacheTrackSettings();
    vocalOffset = 0.0;
    vocalFromYou = false;
    autoAnalyze = false;
    cachePrefs();

    history.clear();
    pendingPlans.clear();
    projectHistoryUpTo = 0;
    lastApply = {};
    currentPlan = {};
    presetUndo.clear();
    bridge.forgetRestorePoints (now());
    eqRestore.clear();

    analyzeState = Analyze::idle;
    analysis.clear();
    analysis.setVocalOffset (0.0, false);
    adviceAnalysis = {};
    report = {};
    tracksHeard = 0;
    fadersAtAnalysis.clear();
    procAtAnalysis.clear();
    assistAtAnalysis.clear();
    assistGuessed = false;
    mixProcAtAnalysisKnown = false;
    mixProcAtAnalysis = {};
    donePoints.clear();
    doneShown.clear();
    ++doneVersion;
    autoListen.reset();
    autoSnapshot = {};
    autoScanAsked = false;
    autoCheckedMs = autoUpdatedMs = 0.0;
    autoPause.clear();
    autoSecondsShown = -1;
    hearingReference = false;
    monoWatch.reset();
    ++adviceSerial;
    analyzedMs = 0.0;
    adviceFromProject = false;

    savedHistory = {};
    savedAdvice = {};
    savedRestore = {};
    {
        const juce::ScopedLock sl (protectedLock);
        sessionCache.clear();
    }
    ++analysisVersion;
    ++applyVersion;
    ++setupVersion;
    ++projectEpoch;
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
    c.voxology.recordedHot = (v->flags & suite::VocalInfo::recordedHot) != 0;
    c.voxology.recordedClipped = (v->flags & suite::VocalInfo::recordedClipped) != 0;
    c.voxology.recordedPeakDb = v->recordedPeakDb;
    c.voxology.clippedSpots = static_cast<int> (v->clippedSpots);
}

std::map<std::uint32_t, std::string> BridgeHub::sharedNames() const
{
    std::map<std::uint32_t, std::string> out;
    std::map<std::uint32_t, int> count;
    for (const auto& l : mixology::link::processRegistry().links (mixology::link::nowMs()))
        if (l.identity.hasName && ++count[l.nameHash] == 2) out[l.nameHash] = l.identity.name;
    for (int i = 0; i < bridge.slotCount(); ++i)
        if (bridge.slot (i).present && bridge.nameIsDuplicated (i)) out[bridge.slot (i).hash] = bridge.slot (i).title;
    return out;
}

std::vector<mixology::mix::ChannelNow> BridgeHub::channelsNow() const
{
    const auto links = mixology::link::processRegistry().links (mixology::link::nowMs());
    const auto shared = sharedNames();
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
        c.sharedName = shared.count (hash) != 0;
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
            if (auto it = assistAtAnalysis.find (hash); it != assistAtAnalysis.end()) { c.assistAtAnalysisKnown = true; c.assistAtAnalysis = it->second; }
            c.channels = l->channels;
            c.panUnknown = dawKey == "cubase" && l->channels == 2 && f.hasPan && f.pan <= 0.0005;   // Stereo Combined Panner (see getState)
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
        c.sharedName = shared.count (l.nameHash) != 0;
        c.hasLink = uniqueLink (links, l.nameHash) != nullptr;
        c.assist = l.assist;
        if (auto it = assistAtAnalysis.find (l.nameHash); it != assistAtAnalysis.end()) { c.assistAtAnalysisKnown = true; c.assistAtAnalysis = it->second; }
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
    juce::Array<juce::var> first;
    for (const auto& n : p.first) first.add (juce::String::fromUTF8 (n.c_str()));
    o->setProperty ("first", first);
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
        return { false, "{daw:noFaderLink}" };

    LastApply next;
    next.valid = true;
    next.title = currentPlan.title;
    if (! steps.empty())
    {
        std::vector<PlanStep> v;
        for (auto& [h, st] : steps) v.push_back (st);
        next.bridgeSerial = bridge.planProgress().serial + 1;
        const auto r = bridge.applyPlan (currentPlan.title, std::move (v), now());
        if (! r.ok) return r;
        next.waitingForBridge = true;
    }
    std::vector<HChange> hist;   // the undo history (the faders join once Cubase has moved them)
    for (const auto& [hash, a] : eq)
    {
        const auto* l = uniqueLink (links, hash);
        next.assist.push_back ({ l->slot, l->generation, l->assist, hash });
        for (auto& c : assistChanges (*l, l->assist, a)) hist.push_back (std::move (c));
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
        HChange target;
        target.what = HWhat::proc;
        if (c->fullMix)
        {
            before = mixProc.get();
            target.fullMix = true;
            target.track = "Full Mix";
        }
        else
        {
            const auto* l = uniqueLink (links, c->hash);
            target = linkChange (HWhat::proc, *l);
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
                auto dc = linkChange (HWhat::duck, *l);
                dc.listBefore = duckList (l->duck);
                dc.listAfter = duckList (d);
                hist.push_back (std::move (dc));
                registry.requestDuck (l->slot, l->generation, d);
            }
        }
        for (const auto& [id, v] : c->proc)
        {
            u.before.push_back ({ id, before[static_cast<std::size_t> (id)] });
            auto pc = target;
            pc.index = id;
            pc.before = before[static_cast<std::size_t> (id)];
            pc.after = v;
            hist.push_back (std::move (pc));
            if (c->fullMix) mixProc.set (id, v);
            else registry.requestProc (u.slot, u.generation, id, v);
        }
        procTracks.insert (c->hash);
        next.procs.push_back (std::move (u));
    }
    if (const auto id = history.record (currentPlan.title, "", std::move (hist), wallClock(), ! steps.empty()); id != 0 && ! steps.empty())
        pendingPlans.push_back ({ next.bridgeSerial, id, steps });
    lastApply = std::move (next);
    ++applyVersion;

    std::vector<std::string> parts;
    if (! steps.empty()) parts.push_back ("moving " + std::to_string (steps.size()) + " channel" + (steps.size() == 1 ? "" : "s") + " in {daw}");
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
    const auto serial = bridge.planProgress().serial + 1;
    if (! lastApply.bridgeUndo.empty())
    {
        if (bridge.link() != mixology::BridgeSession::Link::connected) return { false, "Mixology isn't connected to {daw}'s faders: it can't put them back yet." };
        const auto r = bridge.applyPlan ("Undo " + lastApply.title, lastApply.bridgeUndo, now());
        if (! r.ok) return r;
    }
    auto& registry = mixology::link::processRegistry();
    const auto links = registry.links (mixology::link::nowMs());
    auto linkAt = [&links] (int slot, std::uint32_t generation) -> const mixology::link::LinkInfo* {
        for (const auto& l : links)
            if (l.slot == slot && l.generation == generation) return &l;
        return nullptr;
    };
    std::vector<HChange> hist;   // the undo history: this Undo is a step of its own
    for (const auto& a : lastApply.assist)
    {
        if (const auto* l = linkAt (a.slot, a.generation))
            for (auto& c : assistChanges (*l, l->assist, a.before)) hist.push_back (std::move (c));
        registry.requestAssist (a.slot, a.generation, a.before);
        eqRestore.noteUndo (a.hash, a.before);
    }
    const auto mixNowValues = mixProc.get ? mixProc.get() : mixology::proc::defaults();
    for (auto it = lastApply.procs.rbegin(); it != lastApply.procs.rend(); ++it)   // newest first: the oldest value wins
    {
        const auto* l = it->fullMix ? nullptr : linkAt (it->slot, it->generation);
        mixology::proc::Values current = mixNowValues;
        if (! it->fullMix && (l == nullptr || ! registry.readProc (it->slot, it->generation, current))) current = mixology::proc::defaults();
        for (const auto& [id, v] : it->before)
        {
            if (it->fullMix || l != nullptr)
            {
                HChange c;
                if (l != nullptr) c = linkChange (HWhat::proc, *l);
                else { c.what = HWhat::proc; c.fullMix = true; c.track = "Full Mix"; }
                c.index = id;
                c.before = current[static_cast<std::size_t> (id)];
                c.after = v;
                hist.push_back (std::move (c));
            }
            if (it->fullMix) { if (mixProc.set) mixProc.set (id, v); }
            else registry.requestProc (it->slot, it->generation, id, v);
        }
        if (it->duckSet)
        {
            if (l != nullptr)
            {
                auto dc = linkChange (HWhat::duck, *l);
                dc.listBefore = duckList (l->duck);
                dc.listAfter = duckList (it->duckBefore);
                hist.push_back (std::move (dc));
            }
            registry.requestDuck (it->slot, it->generation, it->duckBefore);
        }
    }
    const bool viaBridge = ! lastApply.bridgeUndo.empty();
    if (const auto id = history.record ("Undo " + lastApply.title, "", std::move (hist), wallClock(), viaBridge); id != 0 && viaBridge)
    {
        PendingPlan pending { serial, id, {} };
        for (const auto& st : lastApply.bridgeUndo) pending.targets[st.hash] = st;
        pendingPlans.push_back (std::move (pending));
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
    if (bridge.link() != mixology::BridgeSession::Link::connected) return { false, "Mixology isn't connected to {daw}'s faders: it can't solo the reference." };
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

// ---------------------------------------------------------------------------------------------
// The undo history: every change Mixology makes, step by step. Recorded where each change is made (strip commands,
// plans, Restore here; the processing pages, presets and ducking from the editor); played back here.

double BridgeHub::wallClock()
{
    return static_cast<double> (juce::Time::currentTimeMillis()) * 0.001;
}

std::uint64_t BridgeHub::recordHistory (const std::string& what, const std::string& group, std::vector<mixology::history::Change> changes)
{
    return history.record (what, group, std::move (changes), wallClock());
}

void BridgeHub::finishPendingPlans()
{
    // A plan has finished: its undo says which channels it really changed and how each was; what it asked for is
    // what they are now.
    const auto& pr = bridge.planProgress();
    for (auto it = pendingPlans.begin(); it != pendingPlans.end();)
    {
        if (pr.serial < it->serial) { ++it; continue; }
        std::vector<HChange> changes;
        if (pr.serial == it->serial)
            for (const auto& u : pr.undo)
            {
                const auto target = it->targets.find (u.hash);
                if (target == it->targets.end()) continue;
                const auto& to = target->second;
                if (u.setVolume)
                {
                    auto c = channelChange (HWhat::fader, u.hash, u.title);
                    c.before = bridge.faderLaw().toDb (u.position);
                    c.beforePos = u.position;
                    c.after = to.exactVolume ? bridge.faderLaw().toDb (to.position) : to.db;
                    c.afterPos = to.exactVolume ? to.position : -1.0;
                    changes.push_back (std::move (c));
                }
                if (u.setPan && to.setPan)
                {
                    auto c = channelChange (HWhat::pan, u.hash, u.title);
                    c.before = u.pan;
                    c.after = to.pan;
                    changes.push_back (std::move (c));
                }
                for (auto [what, set, was, now] : { std::tuple { HWhat::mute, u.setMute && to.setMute, u.mute, to.mute },
                                                    std::tuple { HWhat::solo, u.setSolo && to.setSolo, u.solo, to.solo } })
                    if (set)
                    {
                        auto c = channelChange (what, u.hash, u.title);
                        c.before = was ? 1.0 : 0.0;
                        c.after = now ? 1.0 : 0.0;
                        changes.push_back (std::move (c));
                    }
            }
        history.amend (it->step, std::move (changes));
        it = pendingPlans.erase (it);
    }
}

const mixology::link::LinkInfo* BridgeHub::linkOf (const std::vector<mixology::link::LinkInfo>& links, const mixology::history::Change& c) const
{
    if (c.hash != 0) return uniqueLink (links, c.hash);
    for (const auto& l : links)
        if (l.slot == c.slot && l.generation == c.generation) return &l;
    return nullptr;
}

mixology::BridgeSession::Result BridgeHub::playHistory (const std::vector<mixology::history::Change>& changes, const std::string& what, std::string& info)
{
    using PlanStep = mixology::BridgeSession::PlanStep;
    auto& registry = mixology::link::processRegistry();
    const auto links = registry.links (mixology::link::nowMs());
    std::map<std::uint32_t, PlanStep> steps;
    std::map<std::pair<int, std::uint32_t>, std::pair<const mixology::link::LinkInfo*, mixology::AssistSettings>> assist;
    std::set<std::string> missing;
    int onLinks = 0;
    for (const auto& c : changes)
        switch (c.what)
        {
            case HWhat::fader: case HWhat::pan: case HWhat::mute: case HWhat::solo:
            {
                // Each only if it is still where Mixology left it: a fader you moved in Cubase since stays yours.
                auto& st = steps[c.hash];
                st.hash = c.hash;
                st.title = c.track;
                if (c.what == HWhat::fader)
                {
                    st.setVolume = true;
                    st.exactVolume = c.afterPos >= 0.0;
                    st.position = std::max (0.0, c.afterPos);
                    st.db = c.after;
                    st.checkFrom = true;
                    st.fromDb = c.before;
                }
                else if (c.what == HWhat::pan)
                {
                    st.setPan = true;
                    st.pan = c.after;
                    st.checkPan = true;
                    st.fromPan = c.before;
                }
                else if (c.what == HWhat::mute) { st.setMute = true; st.mute = c.after > 0.5; }
                else { st.setSolo = true; st.solo = c.after > 0.5; }
                break;
            }
            case HWhat::proc:
                if (c.fullMix)
                {
                    if (mixProc.set) { mixProc.set (c.index, static_cast<float> (c.after)); ++onLinks; }
                    else missing.insert ("Full Mix");
                }
                else if (const auto* l = linkOf (links, c))
                {
                    registry.requestProc (l->slot, l->generation, c.index, static_cast<float> (c.after));
                    ++onLinks;
                }
                else missing.insert (c.track);
                break;
            case HWhat::assist:
                if (const auto* l = linkOf (links, c))
                {
                    auto it = assist.find ({ l->slot, l->generation });
                    if (it == assist.end()) it = assist.emplace (std::pair { l->slot, l->generation }, std::pair { l, l->assist }).first;
                    setAssistValue (it->second.second, c.index, static_cast<float> (c.after));
                }
                else missing.insert (c.track);
                break;
            case HWhat::trim:
                if (const auto* l = linkOf (links, c))
                {
                    registry.requestTrim (l->slot, l->generation, static_cast<float> (c.after));
                    ++onLinks;
                }
                else missing.insert (c.track);
                break;
            case HWhat::duck:
                if (const auto* l = linkOf (links, c))
                {
                    mixology::link::DuckSources d;
                    for (auto h : c.listAfter)
                        if (d.count < mixology::link::kMaxDuckSources) d.hashes[static_cast<std::size_t> (d.count++)] = h;
                    registry.requestDuck (l->slot, l->generation, d);
                    ++onLinks;
                }
                else missing.insert (c.track);
                break;
        }
    for (const auto& [key, la] : assist)
    {
        const auto* l = la.first;
        if (l->identity.hasName) eqRestore.noteSet (l->nameHash, l->assist, la.second);
        registry.requestAssist (l->slot, l->generation, la.second);
        ++onLinks;
    }
    steps.erase (0);
    if (! steps.empty())
    {
        std::vector<PlanStep> v;
        for (auto& [h, st] : steps) v.push_back (st);
        const auto r = bridge.applyPlan (what, std::move (v), now());
        if (! r.ok) return r;
    }

    info = what + ".";
    if (! steps.empty()) info += " Moving " + std::to_string (steps.size()) + " channel" + (steps.size() == 1 ? "" : "s") + " in {daw}.";
    if (! missing.empty())
    {
        std::string names;
        for (const auto& n : missing) names += (names.empty() ? "'" : ", '") + (n.empty() ? std::string ("a track without a name") : n) + "'";
        info += " Skipped " + names + ": " + (missing.size() == 1 ? "its" : "their") + " Mixology Link isn't there (renamed or removed?).";
    }
    (void) onLinks;
    return { true, {} };
}

mixology::BridgeSession::Result BridgeHub::moveHistory (const juce::String& how, std::uint64_t id, std::string& info)
{
    if (bridge.busy() || history.busy()) return { false, "Mixology is still moving faders: try again in a moment." };
    const auto was = history.current();
    std::string what;
    std::vector<HChange> changes;
    if (how == "historyUndo")
    {
        if (const auto* s = history.find (was)) what = "Undo: " + s->what;
        changes = history.undo();
    }
    else if (how == "historyRedo")
    {
        const auto& all = history.steps();
        for (const auto& s : all)
            if (s.undone) { what = "Redo: " + s.what; break; }
        changes = history.redo();
    }
    else
    {
        const auto* s = history.find (id);
        what = id == 0 ? std::string ("Back to the start") : s != nullptr ? (s->undone ? "Forward to: " : "Back to: ") + s->what : std::string();
        changes = history.moveTo (id);
    }
    if (history.current() == was) return { false, "Nothing to undo or redo there." };

    const bool needsCubase = std::any_of (changes.begin(), changes.end(), [] (const HChange& c) {
        return c.what == HWhat::fader || c.what == HWhat::pan || c.what == HWhat::mute || c.what == HWhat::solo;
    });
    if (needsCubase && bridge.link() != mixology::BridgeSession::Link::connected)
    {
        history.moveTo (was);   // nothing happened
        return { false, "{daw:noFaderLink}" };
    }
    const auto r = playHistory (changes, what, info);
    if (! r.ok)
    {
        history.moveTo (was);
        return r;
    }
    // The single-step undos elsewhere (a tool's Undo, a preset's UNDO) would now undo something else: the history
    // takes over.
    lastApply = {};
    presetUndo.clear();
    ++applyVersion;
    return { true, {} };
}

juce::var BridgeHub::historyList() const
{
    constexpr std::size_t kLines = 40;
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> steps;
    for (const auto& s : history.steps())
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("id", static_cast<juce::int64> (s.id));
        so->setProperty ("time", s.time * 1000.0);   // ms, for the interface's Date
        so->setProperty ("what", juce::String::fromUTF8 (s.what.c_str()));
        so->setProperty ("summary", juce::String::fromUTF8 (mixology::history::summary (s).c_str()));
        juce::Array<juce::var> lines;
        for (std::size_t i = 0; i < s.changes.size() && i < kLines; ++i)
            lines.add (juce::String::fromUTF8 (mixology::history::describe (s.changes[i]).c_str()));
        if (s.changes.size() > kLines) lines.add ("... and " + juce::String (static_cast<int> (s.changes.size() - kLines)) + " more");
        so->setProperty ("lines", lines);
        so->setProperty ("undone", s.undone);
        so->setProperty ("pending", s.pending);
        steps.add (juce::var (so));
    }
    o->setProperty ("steps", steps);
    o->setProperty ("current", static_cast<juce::int64> (history.current()));
    o->setProperty ("version", static_cast<juce::int64> (history.version()));
    o->setProperty ("busy", history.busy() || bridge.busy());
    o->setProperty ("fromProject", static_cast<juce::int64> (projectHistoryUpTo));   // steps up to this one: earlier sessions
    return juce::var (o);
}

juce::var BridgeHub::historyState() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", static_cast<juce::int64> (history.version()));
    o->setProperty ("count", static_cast<int> (history.steps().size()));
    o->setProperty ("canUndo", history.canUndo());
    o->setProperty ("canRedo", history.canRedo());
    const auto* cur = history.find (history.current());
    o->setProperty ("undoWhat", cur != nullptr ? juce::String::fromUTF8 (cur->what.c_str()) : juce::String());
    juce::String redo;
    for (const auto& s : history.steps())
        if (s.undone) { redo = juce::String::fromUTF8 (s.what.c_str()); break; }
    o->setProperty ("redoWhat", redo);
    o->setProperty ("busy", history.busy() || bridge.busy());
    return juce::var (o);
}
