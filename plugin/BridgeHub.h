#pragma once

#include "mixology/BridgeSession.h"
#include "mixology/LinkRegistry.h"
#include "mixology/MixAnalysis.h"
#include "mixology/MixPlanner.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <optional>
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
    std::uint64_t stateVersion() const { return bridge.stateVersion() + linkVersion + analysisVersion + applyVersion; }

    /** Per strip: [peak L, peak R] dBFS from its paired Mixology Link, or null. */
    juce::var trackMeters() const;

    /** Analyze Mix, five times a second from the editor: the mix as Mixology hears it, and whether the
        host's transport is running. Adds a moment while listening. */
    void analysisTick (const mixology::AnalysisFrame& mix, bool playing);
    /** The Overview analyzer's six colours (kick, bass, drums, vocals, instruments, other): each a list of
        31 band levels in the analyzer's units, or null when nothing of that kind plays. */
    juce::var roleCurves (double sampleRate) const;

    /** UI commands. Strip commands take { slot, hash, value } (the strip as the UI showed it):
        setDb, minusInf, nudge, fader, faderEnd, pan, mute, solo, protect, restore, trim.
        Global ones: restoreAll, forget, bank(action), refresh, reconnect, copyLog.
        Analyze Mix: analyze, analyzeFinish, analyzeCancel, setRole, setExcluded, setReference.
        Milestone 4: plan(tool) -> { plan }, planApply { id, picked, allowBig, headroom }, planUndo -> { bridge },
        refListen ("ref" / "mix"), vocalTaste ("right" / "quieter" / "louder" / "style").
        Returns { ok, message, warning, info }. */
    juce::var command (const juce::String& name, const juce::var& arg);

    /** Protected channels, saved with the project (Mixology's plug-in state). Safe from any thread: the host
        may save or load state off the message thread, so both go through a small locked copy. */
    juce::String protectedState() const;
    void setProtectedState (const juce::String&);
    /** Your track roles and "leave out of Analyze Mix" choices, saved with the project the same way. */
    juce::String trackSettingsState() const;
    void setTrackSettingsState (const juce::String&);
    /** How you like your vocal ("Sounds right / too loud / too quiet"), saved with the project the same way. */
    juce::String mixPrefsState() const;
    void setMixPrefsState (const juce::String&);

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
    void cacheTrackSettings();
    void finishAnalysis();
    juce::var analysisState() const;

    // ---- Milestone 4: plans you approve, then apply (faders and pans through the bridge, EQ through the Links)
    std::vector<mixology::mix::ChannelNow> channelsNow() const;
    juce::var planToVar (const mixology::mix::Plan&) const;
    juce::var applyState() const;
    mixology::BridgeSession::Result applyCurrentPlan (const juce::var& picked, bool allowBig, bool headroom, std::string& info);
    mixology::BridgeSession::Result undoLastApply (std::string& info);
    mixology::BridgeSession::Result listenToReference (bool reference, std::string& info);
    const mixology::link::LinkInfo* uniqueLink (const std::vector<mixology::link::LinkInfo>&, std::uint32_t hash) const;

    mixology::mix::Plan currentPlan;
    std::uint64_t currentPlanId = 0;
    struct AssistUndo
    {
        int slot = -1;
        std::uint32_t generation = 0;
        mixology::AssistSettings before;
    };
    struct LastApply
    {
        bool valid = false;
        std::string title;
        bool waitingForBridge = false;
        std::uint64_t bridgeSerial = 0;     // the bridge plan's serial once it has finished
        std::vector<mixology::BridgeSession::PlanStep> bridgeUndo;
        std::vector<AssistUndo> assist;
    };
    LastApply lastApply;
    std::uint64_t applyVersion = 0, seenPlanSerial = 0;
    bool hearingReference = false;

    double vocalOffset = 0.0;          // your vocal setting (dB on top of the style's target)
    bool vocalFromYou = false;
    juce::String prefsCache, prefsPending;
    bool hasPendingPrefs = false;
    void cachePrefs();
    bool audibleNow (std::uint32_t hash) const;   // muted or soloed-out channels: their Link still hears them
    std::optional<mixology::mix::Role> roleFor (std::uint32_t hash, const std::string& name) const;

    enum class Analyze { idle, reading, listening, done };
    Analyze analyzeState = Analyze::idle;
    mixology::mix::Analysis analysis;
    mixology::mix::Report report;
    std::map<std::uint32_t, mixology::mix::TrackSettings> trackSettings;
    std::uint64_t analysisVersion = 0;
    bool transportPlaying = false;
    int tracksHeard = 0;
    juce::String trackSettingsCache, trackSettingsPending;
    bool hasPendingTrackSettings = false;
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
        int channels = 0;
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
