#include "BridgeHub.h"

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
