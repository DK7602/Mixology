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
    bridge.faderLaw().deserialise (settingsFile->getValue ("faderLaw").toStdString());
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

juce::var BridgeHub::getState (bool includeLog)
{
    using Lock = mixology::BridgeSession::Lock;

    auto* o = new juce::DynamicObject();
    o->setProperty ("enabled", enabled);
    o->setProperty ("link", mixology::toString (bridge.link()));
    o->setProperty ("portProblem", portProblem);
    o->setProperty ("script", bridge.scriptVersion());
    o->setProperty ("toCubase", kToCubase);
    o->setProperty ("fromCubase", kFromCubase);

    juce::Array<juce::var> slots;
    for (int i = 0; i < bridge.slotCount(); ++i)
    {
        const auto& s = bridge.slot (i);
        auto* so = new juce::DynamicObject();
        so->setProperty ("present", s.present);
        so->setProperty ("title", juce::String::fromUTF8 (s.title.c_str()));
        so->setProperty ("hasVolume", s.hasVolume);
        so->setProperty ("volume", s.volume);
        so->setProperty ("volumeText", juce::String::fromUTF8 (s.volumeText.c_str()));
        so->setProperty ("volumeDb", s.volumeDb <= mixology::FaderLaw::kMinusInf ? juce::var() : juce::var (s.volumeDb));
        so->setProperty ("pan", s.pan);
        so->setProperty ("panText", juce::String::fromUTF8 (s.panText.c_str()));
        so->setProperty ("mute", s.mute);
        so->setProperty ("solo", s.solo);
        so->setProperty ("read", s.automationRead);
        so->setProperty ("write", s.automationWrite);
        so->setProperty ("colour", s.hasColour ? juce::var (juce::String::formatted ("#%06X", s.rgb)) : juce::var());
        slots.add (juce::var (so));
    }
    o->setProperty ("slots", slots);

    o->setProperty ("lock", bridge.lock() == Lock::none ? "none" : bridge.lock() == Lock::locked ? "locked" : "paused");
    o->setProperty ("lockedSlot", bridge.lockedSlot());
    o->setProperty ("lockedTitle", juce::String::fromUTF8 (bridge.lockedTitle().c_str()));
    o->setProperty ("pauseReason", juce::String::fromUTF8 (bridge.pauseReason().c_str()));
    o->setProperty ("moving", bridge.moveInProgress());
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

    const auto& snap = bridge.snapshot();
    auto* sn = new juce::DynamicObject();
    sn->setProperty ("valid", snap.valid);
    sn->setProperty ("title", juce::String::fromUTF8 (snap.title.c_str()));
    sn->setProperty ("volumeText", juce::String::fromUTF8 (snap.volumeText.c_str()));
    o->setProperty ("snapshot", juce::var (sn));

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
    const double t = now();
    mixology::BridgeSession::Result r { true, {} };

    if (name == "lock")          r = bridge.lockChannel (static_cast<int> (arg), t);
    else if (name == "unlock")   bridge.unlock (t);
    else if (name == "setDb")    r = bridge.setVolumeDb (static_cast<double> (arg), t);
    else if (name == "minusInf") r = bridge.setVolumeDb (mixology::FaderLaw::kMinusInf, t);
    else if (name == "nudge")    r = bridge.nudgeDb (static_cast<double> (arg), t);
    else if (name == "fader")    r = bridge.setVolumeNormalised (static_cast<double> (arg), t);
    else if (name == "faderEnd") bridge.noteDragFinished (t);
    else if (name == "pan")      r = bridge.setPan (static_cast<double> (arg), t);
    else if (name == "mute")     r = bridge.setMute (static_cast<bool> (arg), t);
    else if (name == "solo")     r = bridge.setSolo (static_cast<bool> (arg), t);
    else if (name == "restore")  r = bridge.restore (t);
    else if (name == "bank")     r = bridge.bank (static_cast<mixology::bridge::BankAction> (juce::jlimit (0, 4, static_cast<int> (arg))), t);
    else if (name == "refresh")  bridge.requestDump (t);
    else if (name == "reconnect")
    {
        closePorts ("Reconnect pressed");
        portProblem.clear();
        openPorts();
    }
    else if (name == "copyLog")  juce::SystemClipboard::copyTextToClipboard (logText());
    else                         r = { false, "Unknown command." };

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", r.ok);
    o->setProperty ("message", juce::String::fromUTF8 (r.message.c_str()));
    return juce::var (o);
}
