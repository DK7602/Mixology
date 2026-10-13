#pragma once

#include "mixology/BridgeSession.h"

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

    /** Everything the Settings > Cubase Bridge panel shows. */
    juce::var getState (bool includeLog);
    std::uint64_t stateVersion() const { return bridge.stateVersion(); }

    /** UI commands: lock(slot), unlock, setDb(db), nudge(delta), pan(0..1), mute(bool), solo(bool),
        restore, bank(action), reconnect, copyLog. Returns { ok, message }. */
    juce::var command (const juce::String& name, const juce::var& arg);

    juce::String logText() const;

    /** Global settings file shared by all instances (theme, learned fader curve). */
    juce::PropertiesFile& settings() { return *settingsFile; }

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void openPorts();
    void closePorts (const juce::String& why);
    static double now();

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
