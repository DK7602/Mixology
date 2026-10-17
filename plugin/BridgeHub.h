#pragma once

#include <functional>

#include "mixology/AssistRestore.h"
#include "mixology/BridgeSession.h"
#include "mixology/History.h"
#include "mixology/LinkRegistry.h"
#include "mixology/McuBridge.h"
#include "mixology/AutoAnalyze.h"
#include "mixology/MixAnalysis.h"
#include "mixology/MixPlanner.h"
#include "mixology/Presets.h"
#include "mixology/SetupCheck.h"
#include "suite/SuiteLink.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
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

    /** The DAW Mixology works in (the suite's DAW key: "cubase", "logic", ...). Logic Pro and Cakewalk take
        Mixology as a Mackie Control surface (mixology::mcu::McuBridge); the others run a bridge script. */
    void setDaw (const std::string& key);

    /** Mixology's own Full Mix processing, for Polish / Auto Mix. */
    struct MixProcessing
    {
        std::function<mixology::proc::Values()> get;
        std::function<void (int, float)> set;
    };

    /** Every Mixology in the host registers itself (message thread). One of them is the hub's Full Mix (Polish /
        Auto Mix work on its processing, it feeds Analyze Mix and publishes to Masterology): one on a channel called
        like the master ("Stereo Out", "Master"...), else the oldest, of the project on screen. When it goes, the
        next one takes over. */
    struct Instance
    {
        MixProcessing processing;
        std::function<std::uint64_t()> project;           // the project it belongs to
        std::function<void (std::uint64_t)> adopt;        // it belongs to this project after all
        std::function<juce::int64()> lastAudioMs;         // when it last processed audio (ms since 1970)
        std::function<juce::int64()> lastSeenMs;          // when its window was last on screen
        std::function<juce::String()> trackName;          // its channel's name, when the DAW tells it
    };
    void registerInstance (const void* owner, Instance);
    void unregisterInstance (const void* owner);
    bool isMixOwner (const void* owner) const { return owner != nullptr && owner == mixOwner; }

    /** A channel name that is most likely the master / main output. */
    static bool looksLikeMaster (const juce::String& trackName);

    // ---- Projects: each Mixology saves the id of its project with it. The hub holds one project at a time (the
    // one playing or on screen: a DAW plays one project at a time); another open project's padlocks, roles, History,
    // advice and restore points wait here and come back when you switch to it, so two projects never mix.
    struct ProjectState
    {
        juce::String protectedText, tracks, prefs, session;
        bool empty() const { return protectedText.isEmpty() && tracks.isEmpty() && prefs.isEmpty() && session.isEmpty(); }
    };
    /** The project a new Mixology belongs to: the one on screen when it is added there, else a new one. */
    std::uint64_t idForNewInstance();
    /** A project's saved state (setStateInformation, any thread): into the hub when it is the project on screen,
        else kept until you switch to it. `previous` is the id the instance had (a new one's own). */
    void loadProject (const void* owner, std::uint64_t previous, std::uint64_t id, const ProjectState&);
    /** What to save for a project (getStateInformation, any thread). known = false: the hub has nothing for it
        (the instance keeps the copy it loaded). */
    ProjectState saveProject (std::uint64_t id, bool& known);

    // ---- M6: the suite link (Voxology on the vocals, Masterology after Mixology) ---------------------------
    /** What Analyze Mix knows for Masterology (the style it analysed against, its tone against it, a mono mix);
        the plug-in adds its meters and publishes it. False before Analyze Mix (or after the style changed). */
    bool handoffTone (const std::string& styleKey, suite::MixInfo&) const;
    /** The Masterology after Mixology, if one is open in this project (message thread). */
    std::optional<suite::MasterInfo> masterology() const;

    /** Everything the Tracks page and Settings > Cubase Bridge show. */
    juce::var getState (bool includeLog);
    std::uint64_t stateVersion() const { return bridge.stateVersion() + linkVersion + analysisVersion + applyVersion + suiteVersion + setupVersion + history.version() + doneVersion + projectEpoch * 1000003ull; }

    /** The setup check's issues right now (a mono group, vocals without a Link...), as Mix Assistant suggestions. */
    const std::vector<mixology::mix::Suggestion>& setupIssues() const { return setupNow; }

    /** Per strip: [peak L, peak R] dBFS from its paired Mixology Link, or null. */
    juce::var trackMeters() const;

    /** Analyze Mix, five times a second from the editor: the mix as Mixology hears it, and whether the
        host's transport is running. Adds a moment while listening. */
    void analysisTick (const mixology::AnalysisFrame& mix, bool playing, int mixChannels = 2);
    /** The Overview analyzer's six colours (kick, bass, drums, vocals, instruments, other): each a list of
        31 band levels in the analyzer's units, or null when nothing of that kind plays. */
    juce::var roleCurves (double sampleRate) const;

    /** UI commands. Strip commands take { slot, hash, value } (the strip as the UI showed it):
        setDb, minusInf, nudge, fader, faderEnd, pan, mute, solo, protect, restore, trim.
        Global ones: restoreAll, forget, bank(action), refresh, reconnect, copyLog.
        Analyze Mix: analyze, analyzeFinish, analyzeCancel, setRole, setExcluded, setReference.
        Milestone 4: plan(tool) -> { plan }, planApply { id, picked, allowBig, headroom }, planUndo -> { bridge },
        refListen ("ref" / "mix"), vocalTaste ("right" / "quieter" / "louder" / "style"), setMusicBus { hash, value }.
        Undo history: history -> { history }, historyTo (step id, 0 = the start), historyUndo, historyRedo.
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
    /** What Mixology knows about this project beyond the plug-ins' own settings, saved with it so Mixology opens
        as you left it: the History, the advice on screen (the moments Analyze Mix heard, so a role change can
        still re-evaluate them) and the restore points. Packed and compressed, as text for the project. On the
        message thread it is brought up to date first; elsewhere it is the copy made at most half a second ago. */
    juce::String sessionState();
    void setSessionState (const juce::String&);

    juce::String logText() const;

    /** Global settings file shared by all instances (theme, learned fader curve). */
    juce::PropertiesFile& settings() { return *settingsFile; }

    // ---- Presets: yours live in one small file beside the settings (the same for every project). Message thread.
    const std::vector<mixology::presets::Preset>& userPresets();
    bool saveUserPreset (const mixology::presets::Preset&);   // replaces one of the same name; false: the file couldn't be written
    bool deleteUserPreset (const std::string& name);
    /** Loading a preset can be undone (one step per target): what it replaced. */
    void rememberPresetUndo (const juce::String& targetKey, const std::string& presetName, const mixology::proc::Values& before);
    bool takePresetUndo (const juce::String& targetKey, std::string& presetName, mixology::proc::Values& before);
    std::string presetUndoName (const juce::String& targetKey) const;
    /** Voxology (Vocal mode, processing) on this channel, and whether it compresses. */
    bool voxologyCompresses (std::uint32_t hash) const
    {
        const auto* v = voxologyOn (hash);
        return v != nullptr && (v->flags & suite::VocalInfo::processing) != 0 && (v->flags & suite::VocalInfo::compressor) != 0;
    }

    // ---- The undo history: every change Mixology makes, step by step (message thread) -----------------------
    /** Records a change made outside the hub (the processing pages, presets, Space ducking). Changes with the same
        group within a few seconds join one step (a knob being turned). Returns the step's id (0: nothing changed). */
    std::uint64_t recordHistory (const std::string& what, const std::string& group, std::vector<mixology::history::Change>);

    /** A track's role (yours, or guessed from its name), for the Space page ("duck under the vocal"). */
    std::optional<mixology::mix::Role> trackRole (std::uint32_t hash, const std::string& name) const { return roleFor (hash, name); }

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void openPorts();

    std::vector<mixology::presets::Preset> userPresetList;
    bool userPresetsLoaded = false;
    juce::int64 userPresetsModified = 0;   // the file's time when it was last read or written
    bool writeUserPresets();
    juce::File userPresetFile() const;
    struct PresetUndo { std::string name; mixology::proc::Values before {}; };
    std::map<juce::String, PresetUndo> presetUndo;
    void closePorts (const juce::String& why);
    void updateLinks();
    void applyPendingProtected();
    void cacheProtected();
    void cacheTrackSettings();
    void finishAnalysis();
    juce::var analysisState() const;

    // ---- Milestone 4: plans you approve, then apply (faders and pans through the bridge, EQ through the Links)
    std::vector<mixology::mix::ChannelNow> channelsNow() const;
    /** Names more than one track has (two Links reporting it, or two strips in view showing it): Mixology can't
        tell those tracks apart, so plans leave them alone and the Mix Assistant says to rename one. */
    std::map<std::uint32_t, std::string> sharedNames() const;
    void fillProc (mixology::mix::ChannelNow&, const mixology::link::LinkInfo&) const;
    void fillVoxology (mixology::mix::ChannelNow&) const;
    juce::var planToVar (const mixology::mix::Plan&) const;
    juce::var applyState() const;
    mixology::BridgeSession::Result applyCurrentPlan (const juce::var& picked, bool allowBig, bool headroom, std::string& info);
    mixology::BridgeSession::Result undoLastApply (std::string& info);
    mixology::BridgeSession::Result listenToReference (bool reference, std::string& info);
    const mixology::link::LinkInfo* uniqueLink (const std::vector<mixology::link::LinkInfo>&, std::uint32_t hash) const;

    std::map<std::uint32_t, double> fadersAtAnalysis;   // every fader when Analyze Mix finished listening
    std::map<std::uint32_t, mixology::proc::Values> procAtAnalysis;   // every Link's processing then (Polish)
    std::map<std::uint32_t, mixology::AssistSettings> assistAtAnalysis;   // ... and its Assist EQ (Vocal Pocket, Clean Up)
    bool assistGuessed = false;   // advice saved before 0.16 didn't keep it: the Assist EQ when the project opened
    void guessAssistAtAnalysis();
    // The Mix Assistant's points the smart tools already fixed since Mixology listened (their tool has nothing
    // left to do and their tracks changed): worked out at most twice a second.
    struct DonePoint { bool done = false; std::string what; };
    std::map<std::string, DonePoint> donePoints;
    std::string doneShown;              // what the interface was told last
    std::uint64_t doneVersion = 0;
    double doneCheckedAt = -1.0;
    void updateDonePoints();
    std::string changedSinceAnalysis (const std::vector<std::string>& names, bool anyFader, bool fadersOnly = false) const;
    bool mixProcAtAnalysisKnown = false;
    mixology::proc::Values mixProcAtAnalysis {};
    const void* mixOwner = nullptr;
    MixProcessing mixProc;
    std::vector<std::pair<const void*, Instance>> instances;   // guarded by protectedLock (state calls come from any thread)
    std::uint64_t activeProject = 0;                          // the project the hub holds (0: none yet); protectedLock
    std::map<std::uint64_t, ProjectState> parked;             // the other open projects; protectedLock
    std::uint64_t projectEpoch = 0;                           // the hub switched project (the interface shows it all again)
    double lastProjectCheck = 0.0;
    void updateProjects();
    void chooseMixOwner();
    void switchProject (std::uint64_t to);
    void resetProject();
    ProjectState liveProject();
    void mergeProject (const ProjectState&);   // a second id for the project on screen: fills only what is empty
    mixology::mix::MixNow mixNow() const;
    void reevaluate();         // the report again (after a role / setting change), without listening again
    // tells an analysis where the music group's fader was while it listened
    void applyMusicBus (mixology::mix::Analysis&, const std::map<std::uint32_t, double>& faders) const;
    std::vector<std::string> unheardNow() const;   // channels Cubase shows that no Link reports

    // What the mix was while Mixology listened: plans and Polish work from there.
    struct AtAnalysis
    {
        std::map<std::uint32_t, double> faders;
        std::map<std::uint32_t, mixology::proc::Values> procs;
        bool mixKnown = false;
        mixology::proc::Values mix {};
        std::map<std::uint32_t, mixology::AssistSettings> assist;   // every Link's Assist EQ then
    };
    AtAnalysis snapshotNow() const;
    void useSnapshot (AtAnalysis);

    // Auto Analyze (after the first Analyze Mix): keeps listening while the song plays and refreshes the
    // advice every 15 s of playback when it changed. Advice only: never moves anything. Saved with the project.
    bool autoAnalyze = false;
    // The style picked in the header (the interface sends it): Auto Analyze starts from it when the project
    // opens with Auto Analyze on and no advice yet, and follows it when you pick another style.
    mixology::mix::Style autoStyle;
    bool autoStyleKnown = false;
    bool autoScanAsked = false;       // the one fader read Auto Analyze asks for when it starts from scratch
    mixology::mix::AutoAnalyze autoListen;
    AtAnalysis autoSnapshot;          // the mix at the start of autoListen's window
    double autoCheckedMs = 0.0, autoUpdatedMs = 0.0;   // wall clock (ms since 1970)
    juce::String autoPause;           // why it isn't listening right now ("" = listening)
    int autoSecondsShown = -1;
    std::uint64_t autoMixKey() const; // changes whenever any processing changes
    static mixology::mix::Style styleFromVar (const juce::var&);
    void autoTick (const mixology::AnalysisFrame& mix, std::vector<mixology::mix::TrackSample> tracks, bool playing);
    mixology::mix::Plan currentPlan;
    std::uint64_t currentPlanId = 0;
    struct AssistUndo
    {
        int slot = -1;
        std::uint32_t generation = 0;
        mixology::AssistSettings before;
        std::uint32_t hash = 0;
    };
    mixology::AssistRestore eqRestore;   // the Assist EQ before Mixology's first change, per track (Restore)
    /** Puts the Assist EQ back on one track (or all when hash == 0). Counts restored and kept-yours tracks; adds
        what it changed to `changes` (the undo history). */
    void restoreAssist (std::uint32_t hash, int& restored, int& kept, std::vector<mixology::history::Change>* changes = nullptr);
    int changedTotal (int* faders = nullptr, int* eq = nullptr) const;
    struct ProcUndo   // processing a plan changed: the values before, per Link (or the Full Mix)
    {
        bool fullMix = false;
        int slot = -1;
        std::uint32_t generation = 0;
        std::vector<std::pair<int, float>> before;
        bool duckSet = false;
        mixology::link::DuckSources duckBefore;
    };
    struct LastApply
    {
        bool valid = false;
        std::string title;
        bool waitingForBridge = false;
        std::uint64_t bridgeSerial = 0;     // the bridge plan's serial once it has finished
        std::vector<mixology::BridgeSession::PlanStep> bridgeUndo;
        std::vector<AssistUndo> assist;
        std::vector<ProcUndo> procs;
    };
    LastApply lastApply;

    // The undo history. A plan's faders join their step once Cubase has moved them (the bridge knows then which
    // channels it really changed, and how each was before).
    mixology::history::History history;
    struct PendingPlan
    {
        std::uint64_t serial = 0;   // the bridge plan's serial once it has finished
        std::uint64_t step = 0;
        std::map<std::uint32_t, mixology::BridgeSession::PlanStep> targets;
    };
    std::vector<PendingPlan> pendingPlans;
    void finishPendingPlans();
    std::uint64_t projectHistoryUpTo = 0;   // the newest step that came back with the project (0 = none)

    // ---- Kept with the project (sessionState): each part packed again only when it changed --------------------
    struct SavedPart
    {
        std::uint64_t key = ~0ull;
        juce::String text;   // compressed, base64; empty = nothing to keep
    };
    SavedPart savedHistory, savedAdvice, savedRestore;
    juce::String sessionCache, sessionPending;
    bool hasPendingSession = false;
    bool sessionDirty = false;
    double lastSessionCache = 0.0;
    std::uint64_t adviceSerial = 0;     // the advice's moments changed (Analyze Mix finished, Auto Analyze updated it)
    double analyzedMs = 0.0;            // when the advice on screen was made (ms since 1970)
    bool adviceFromProject = false;     // it came back with the project (until Mixology hears the mix again)
    mixology::mix::Analysis adviceAnalysis;   // the moments behind the advice while a new Analyze Mix listens
    void updateSessionCache (bool forSave = false);
    std::uint64_t historySeen = 0;
    double historyChangedAt = 0.0, historyPackedAt = 0.0;
    void applyPendingSession();
    std::string adviceBlock() const;
    bool loadAdvice (const std::string&);
    /** Plays changes from the history back: faders / pans / mute / solo as one bridge plan, the rest on the Links. */
    mixology::BridgeSession::Result playHistory (const std::vector<mixology::history::Change>&, const std::string& what, std::string& info);
    mixology::BridgeSession::Result moveHistory (const juce::String& how, std::uint64_t id, std::string& info);
    juce::var historyList() const;
    juce::var historyState() const;
    static double wallClock();   // seconds since 1970: when a step happened
    const mixology::link::LinkInfo* linkOf (const std::vector<mixology::link::LinkInfo>&, const mixology::history::Change&) const;
    std::uint64_t applyVersion = 0, seenPlanSerial = 0;
    std::uint64_t seenNoPanVersion = 0;   // the bridge's No pan list, as last saved in trackSettings
    void syncNoPan();
    bool hearingReference = false;

    double vocalOffset = 0.0;          // your vocal setting (dB on top of the style's target)
    bool vocalFromYou = false;
    juce::String prefsCache, prefsPending;
    bool hasPendingPrefs = false;
    void cachePrefs();
    bool audibleNow (std::uint32_t hash) const;   // muted or soloed-out channels: their Link still hears them
    std::optional<mixology::mix::Role> roleFor (std::uint32_t hash, const std::string& name) const;

    // M6: what the suite link says (Voxology on a track: a vocal, or ad-libs; what it does there).
    struct SuiteVocal
    {
        suite::VocalInfo info;   // flags of every Voxology on the channel together
        double lastSeen = 0.0;
    };
    std::map<std::uint32_t, SuiteVocal> suiteVocals;
    std::vector<suite::MasterInfo> suiteMasters;
    std::string suiteKey, suiteRoleKey;       // what the interface shows / what the roles are, as last seen
    std::uint64_t suiteVersion = 0;
    double lastSuiteUpdate = 0.0;
    void updateSuite (double t);
    const suite::VocalInfo* voxologyOn (std::uint32_t hash) const;   // a Voxology in Vocal mode on it
    std::optional<mixology::mix::Role> voxRole (std::uint32_t hash, const std::string& name) const;
    std::map<std::uint32_t, mixology::mix::TrackSettings> settingsForAnalysis() const;

    // The setup check (fed with every analysis tick): problems no mixing move fixes, with the fix.
    mixology::setup::MonoWatch monoWatch;
    int mixChannels = 2;
    std::vector<mixology::mix::Suggestion> setupNow;
    std::string setupKey;
    std::uint64_t setupVersion = 0;
    void updateSetup();

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

    // Logic Pro / Cakewalk: Mixology is the DAW's Mackie Control surface; the session talks to this translator.
    std::string dawKey;
    std::unique_ptr<mixology::mcu::McuBridge> mcu;
    std::atomic<bool> mcuMode { false };   // read on the MIDI thread: pass every message, not only SysEx
    std::vector<std::string> mcuNames;
    std::size_t mcuLawSaved = 0;
    void sendToDaw (const std::vector<std::uint8_t>&);
    void saveMcuLaw();

    std::unique_ptr<juce::PropertiesFile> settingsFile;
    std::size_t lawPointsSaved = 0;
    double lastLawSave = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeHub)
};
