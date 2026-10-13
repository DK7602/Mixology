#pragma once

#include "mixology/BridgeSession.h"
#include "mixology/LinkRegistry.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

/** Process-wide owner of the Cubase bridge: the two virtual MIDI ports and the BridgeSession.

    Every Mixology instance in the host shares one hub (juce::SharedResourcePointer). Creating the hub
    opens nothing: ports are only opened after enable() (called when the Mixology window first opens),
    so plug-in scans and validators never touch MIDI. Runs on the message thread; MIDI input arrives on
    the MIDI thread and is handed over through a small locked inbox (never the audio thread).

    Ports (create both in loopMIDI, or as Windows MIDI Services loopbacks):
      "Mixology to Cubase"  we send, the Mixology Bridge script in Cubase receives
      "Cubase to Mixology"  the script sends, we receive */
class BridgeHub final : private juce::Timer,
                        private juce::MidiInputCallback
{
public:
    static constexpr const char* kToCubase = "Mixology to Cubase";
    static constexpr const char* kFromCubase = "Cubase to Mixology";

    BridgeHub();
    ~BridgeHub() override;

    void enable();
    bool isEnabled() const { return enabled; }

    mixology::BridgeSession& session() { return bridge; }

    /** Everything the Tracks page and Settings > Cubase Bridge show. */
    juce::var getState (bool includeLog);
    std::uint64_t stateVersion() const { return bridge.stateVersion() + linkVersion; }

    /** Per strip: [peak L, peak R] dBFS from its paired Mixology Link, or null. */
    juce::var trackMeters() const;

    /** UI commands. Strip commands take { slot, hash, value } (the strip as the UI showed it):
        setDb, minusInf, nudge, fader, faderEnd, pan, mute, solo, protect, restore, trim.
        Global ones: restoreAll, forget, bank(action), refresh, reconnect, copyLog.
        Returns { ok, message, warning }. */
    juce::var command (const juce::String& name, const juce::var& arg);

    /** Protected channels, saved with the project (Mixology's plug-in state). Safe from any thread: the host
        may save or load state off the message thread, so both go through a small locked copy. */
    juce::String protectedState() const;
    void setProtectedState (const juce::String&);

    juce::String logText() const;

    /** Global settings file shared by all instances (theme, learned fader curve). */
    juce::PropertiesFile& settings() { return *settingsFile; }

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void openPorts();
    void closePorts (const juce::String& why);
    void updateLinks();
    void applyPendingProtected();
    void cacheProtected();
    static double now();

    juce::CriticalSection protectedLock;
    juce::String protectedCache, protectedPending;
    bool hasPendingProtected = false;

    struct StripLink
    {
        mixology::link::MatchStatus status = mixology::link::MatchStatus::none;
        int slot = -1;                 // registry slot of the paired Link
        std::uint32_t generation = 0;
        float peakL = -100.0f, peakR = -100.0f, momentary = -100.0f, trimDb = 0.0f;
        bool audio = false;
    };
    std::vector<StripLink> stripLinks;
    int linksSeen = 0;
    std::uint64_t linkVersion = 0;
    double lastLinkUpdate = 0.0;

    bool enabled = false;
    mixology::BridgeSession bridge;
    std::unique_ptr<juce::MidiOutput> toCubase;
    std::unique_ptr<juce::MidiInput> fromCubase;
    juce::String portProblem;
    double lastOpenAttempt = -1.0e9;

    juce::CriticalSection inboxLock;
    std::vector<std::vector<std::uint8_t>> inbox;

    std::unique_ptr<juce::PropertiesFile> settingsFile;
    std::size_t lawPointsSaved = 0;
    double lastLawSave = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeHub)
};
