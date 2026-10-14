#pragma once

#include "mixology/BridgeProtocol.h"
#include "mixology/FaderLaw.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

/** The plug-in side of the Cubase bridge: connection, the bank of channels, safe writes, readback.

    Framework-free and clock-injected (every call takes `now` in seconds) so the whole thing is
    unit-tested against a simulated Cubase. Runs on the message thread only, never the audio thread.

    Safety rules (Milestone 2: every channel in the bank can be moved):
    - A write names the channel the user is looking at: its slot AND its name hash (ChannelRef). If that
      slot shows anything else by the time the command arrives here, it is refused; the Cubase script checks
      the same again when the message arrives there (bank moved, tracks reordered or renamed in between).
    - Protected channels (the padlock) are never written.
    - A channel that is writing automation (W) is never written, so no automation gets recorded by accident.
      Read automation (R) is allowed with a warning: playback may move the fader back.
    - Before Mixology first changes a channel, its volume / pan / mute / solo are saved (Restore).
    - dB moves are closed-loop per channel: Cubase reports its own display text after each move, so a move
      is refined until Cubase shows the requested value (exactly, or within 0.05 dB when its fader steps
      can't land on it). A new move on the same channel replaces the old one (latest wins).
    - The fader curve (FaderLaw) only learns pairs that have settled: while a fader moves, Cubase may report
      the new position with the old text, and one such pair would bend the curve (seen in Cubase Artist 14). */
namespace mixology
{
    struct BridgeSlot
    {
        bool present = false;
        std::string title;            // as Cubase reports it (cut at 63 UTF-8 bytes)
        std::uint32_t hash = 0;       // hash of the FULL name (computed in Cubase)
        std::uint32_t channelId = 0;  // hash of Cubase's channel ID, 0 = not reported (script v3+)

        bool hasVolume = false;
        double volume = 0.0;          // normalised fader position
        std::string volumeText;       // Cubase's display text
        double volumeDb = FaderLaw::kMinusInf;

        bool hasPan = false;
        double pan = 0.5;
        std::string panText;

        bool mute = false, solo = false;
        bool automationRead = false, automationWrite = false;
        bool hasColour = false;
        std::uint32_t rgb = 0;
    };

    /** The strip the user is looking at: which slot, and which channel it showed. */
    struct ChannelRef
    {
        int slot = -1;
        std::uint32_t hash = 0;
    };

    /** How a channel was before Mixology first changed it. */
    struct RestorePoint
    {
        std::string title;
        std::uint32_t hash = 0;
        bool ambiguous = false;       // two channels with this name were in the bank: can't tell them apart
        double volume = 0.0;
        std::string volumeText;
        bool hasPan = false;
        double pan = 0.5;
        std::string panText;
        bool mute = false, solo = false;
    };

    class BridgeSession
    {
    public:
        enum class Link { offline, waiting, connected, lost };

        struct Result
        {
            bool ok = false;
            std::string message;      // why it was refused
            std::string warning = {}; // ok, but worth telling the user (e.g. read automation)
        };

        struct LogLine
        {
            double time = 0.0;
            std::string text;
        };

        struct Protected
        {
            std::uint32_t hash = 0;
            std::string title;
        };

        struct RestoreSummary
        {
            int restored = 0;         // writes sent
            int outsideBank = 0;      // changed channels that aren't in the bank right now
            int skipped = 0;          // protected, writing automation, or name not unique
            std::string message;
        };

        static constexpr int kMaxSlots = 32;
        static constexpr double kHelloInterval = 1.0;
        static constexpr double kPingInterval = 1.0;
        static constexpr double kLostAfter = 3.5;
        static constexpr double kReplyTimeout = 0.6;
        static constexpr double kDbExact = 0.005;      // Cubase shows 2 decimals: aim for its display to match
        static constexpr double kDbTolerance = 0.05;   // good enough when Cubase can't land exactly
        static constexpr int kMaxRefineSteps = 6;
        static constexpr double kEchoWindow = 0.6;     // reports this soon after our own write are its echo
        static constexpr double kUserMoveSettle = 0.3; // a Cubase-side move is logged once it has settled
        static constexpr double kLearnSettle = 0.25;   // a (position, dB) pair is learned once no newer report came
        static constexpr std::size_t kMaxLogLines = 500;

        explicit BridgeSession (std::uint16_t nonce = 1);

        // ---- transport -------------------------------------------------------------------------
        void setPortsOpen (bool open, double now, const std::string& detail = {});
        void receive (const std::uint8_t* data, std::size_t size, double now);
        void tick (double now);
        std::vector<std::vector<std::uint8_t>> takeOutgoing();

        // ---- state -----------------------------------------------------------------------------
        Link link() const { return linkState; }
        int slotCount() const { return static_cast<int> (slots.size()); }
        const BridgeSlot& slot (int i) const { return slots.at (static_cast<std::size_t> (i)); }
        int scriptVersion() const { return remoteVersion; }
        ChannelRef refOf (int slotIndex) const;

        /** True while a closed-loop dB move is still being refined on that slot (any slot when -1). */
        bool moveInProgress (int slotIndex = -1) const;
        const std::string& lastMoveResult() const { return moveResult; }

        /** Another channel in the bank has the same name (Restore and Mixology Link can't tell them apart). */
        bool nameIsDuplicated (int slotIndex) const;

        FaderLaw& faderLaw() { return law; }
        const FaderLaw& faderLaw() const { return law; }

        // ---- protect (the padlock) -------------------------------------------------------------
        bool isProtected (std::uint32_t hash) const { return protectedChannels.count (hash) != 0; }
        Result setProtected (ChannelRef, bool on, double now);
        std::vector<Protected> protectedList() const;
        void setProtectedList (const std::vector<Protected>&);

        // ---- restore ---------------------------------------------------------------------------
        const std::map<std::uint32_t, RestorePoint>& restorePoints() const { return restore; }
        /** Changed by Mixology and not back where it started (bank slot; -1 = not in the bank). */
        bool isChanged (const RestorePoint&, int* slotOut = nullptr) const;
        int changedCount() const;
        Result restoreChannel (ChannelRef, double now);
        RestoreSummary restoreAll (double now);
        void forgetRestorePoints (double now);

        // ---- every channel's fader (Analyze Mix needs them, not just the 12 in view) -------------
        struct KnownFader
        {
            std::string title;
            double db = FaderLaw::kMinusInf;
            double seenAt = 0.0;
            double volume = 0.0;      // exact fader position, as Cubase reported it
            std::string volumeText;
            bool hasPan = false;
            double pan = 0.5;
            std::string panText;
            bool mute = false, solo = false;
        };
        /** Is the channel heard on the Stereo Out? Not when it is muted, or when another channel is soloed
            and this one isn't (as far as Mixology has seen them). Its Mixology Link still hears it either way. */
        bool isAudible (std::uint32_t hash) const;
        /** Fader levels of every channel seen so far (in the bank, or during a scan). */
        const std::map<std::uint32_t, KnownFader>& knownFaders() const { return faders; }
        /** Steps the bank through all channels to read their faders, then puts it back where it was.
            Takes about half a second per 12 channels; nothing can be moved meanwhile. */
        Result scanAllChannels (double now);
        bool scanning() const { return scan.active && ! scan.plan; }

        // ---- plans: many channels at once, wherever they are in the project ----------------------
        /** One channel's part of a plan (Auto Mix, Balance, Stereo Architect, an undo). Found by name hash. */
        struct PlanStep
        {
            std::uint32_t hash = 0;
            std::string title;
            bool setVolume = false;
            double db = 0.0;               // closed-loop target
            bool exactVolume = false;      // ... or an exact fader position (undo / restore)
            double position = 0.0;
            bool checkFrom = false;        // skip the channel if its fader isn't where the plan saw it
            double fromDb = 0.0;
            bool setPan = false;
            double pan = 0.5;
            bool checkPan = false;         // leave the pan alone if it isn't where the plan saw it
            double fromPan = 0.5;
            bool setMute = false, mute = false, setSolo = false, solo = false;
        };

        struct PlanProgress
        {
            bool active = false;
            std::string what;              // "Auto Mix", "Undo Balance"...
            int total = 0, done = 0, skipped = 0;
            std::vector<std::string> notes;   // why channels were skipped
            std::string message;           // summary once finished
            std::vector<PlanStep> undo;    // how the channels it changed were just before (exact positions); each
                                           // field only goes back if it is still where this plan put it
            std::uint64_t serial = 0;      // bumps when a plan finishes
        };

        /** Applies every step: first on the channels in view, then the bank pages through the project and
            comes back to where it was (like the fader scan). Same safety as single moves (protected, W,
            duplicated names skipped; restore points taken). Nothing else can be moved meanwhile. */
        Result applyPlan (const std::string& what, std::vector<PlanStep> steps, double now);
        bool applying() const { return scan.active && scan.plan; }
        bool busy() const { return scan.active; }
        const PlanProgress& planProgress() const { return plan; }

        // ---- commands --------------------------------------------------------------------------
        Result setVolumeDb (ChannelRef, double db, double now);
        Result nudgeDb (ChannelRef, double deltaDb, double now);
        /** Exact fader position, for dragging a fader in Mixology. Replaces a move in progress (latest wins). */
        Result setVolumeNormalised (ChannelRef, double position, double now);
        /** Logs where a Mixology fader drag ended (drags themselves aren't logged step by step). */
        void noteDragFinished (ChannelRef, double now);
        Result setPan (ChannelRef, double normalised, double now);
        Result setMute (ChannelRef, bool on, double now);
        Result setSolo (ChannelRef, bool on, double now);
        Result bank (bridge::BankAction, double now);
        void requestDump (double now);

        // ---- log -------------------------------------------------------------------------------
        const std::deque<LogLine>& logLines() const { return logBuffer; }
        std::uint64_t logVersion() const { return logCount; }
        /** Bumps whenever anything the UI shows has changed. */
        std::uint64_t stateVersion() const { return version; }

    private:
        enum class MoveKind { setDb, drag, restore };   // drag and restore send exact positions: no dB refinement

        struct Move
        {
            bool active = false;
            MoveKind kind = MoveKind::setDb;
            std::uint32_t hash = 0;
            double targetDb = 0.0;
            double sentValue = 0.0;
            double sentAt = 0.0;
            std::uint16_t token = 0;
            int step = 0;
            bool logResult = true;  // drags are logged once, when they end
            bool hasPrev = false;   // the previous readback of this move (for the next refinement step)
            double prevValue = 0.0, prevDb = 0.0;
        };

        struct SlotMeta
        {
            Move move;
            double lastOwnWrite = -1.0e9;   // when Mixology last wrote this slot's volume
            double cubaseMovedAt = -1.0;    // a Cubase-side move waiting to settle before it is logged
            std::string loggedText;         // the last volume text logged for this slot
            double learnAt = -1.0;          // a reported (position, dB) pair waiting to settle before it is learned
            double learnValue = 0.0, learnDb = 0.0;
        };

        enum class Field { volume, pan, mute, solo };

        struct Scan
        {
            bool active = false;
            bool plan = false;          // applying a plan (else: reading faders)
            bool inPlace = false;       // a plan's first round: the channels already in view
            bool waiting = false;       // a plan's moves on this page are still landing
            double waitUntil = 0.0;
            double nextAt = 0.0;
            int page = 0;
            std::uint32_t originalFirst = 0, lastFirst = 0;
            int foundPage = -1, foundSlot = -1;
            bool restoring = false;
            std::deque<bridge::BankAction> pending;   // the way back, one step at a time
        };
        void scanStep (double now);
        void scanAdvance (double now);
        bool applyPlanHere (double now);
        void finishPlan (double now);
        void sendBank (bridge::BankAction);
        static constexpr double kScanPageSeconds = 0.5;
        static constexpr double kScanStepSeconds = 0.08;   // between bank steps on the way back
        static constexpr double kPlanPollSeconds = 0.05;
        static constexpr double kPlanSettleSeconds = 3.0;  // longest wait for a page's moves to land
        static constexpr double kPlanFromTolerance = 0.3;  // dB a fader may differ from what the plan saw

        void send (const bridge::Message&);
        void handle (const bridge::Message&, double now);
        void onTitle (const bridge::Message&, double now);
        void onVolume (const bridge::Message&, double now);
        void connected (int scriptVer, int count, double now);
        void cancelMoves (const std::string& why, double now);
        Result checkWritable (ChannelRef, Field) const;
        Result warningsFor (int slotIndex, Result ok, double now);
        bridge::Message writeMessage (bridge::Cmd, int slotIndex);
        void takeRestorePointIfNeeded (int slotIndex, double now);
        void sendVolume (int slotIndex, double value, double now);
        void startMove (int slotIndex, double db, double now);
        std::uint16_t nextToken();
        void addLog (double now, std::string text);
        BridgeSlot& slotRef (int i);
        SlotMeta& metaRef (int i);
        void changed() { ++version; }

        std::vector<BridgeSlot> slots;
        std::vector<SlotMeta> meta;
        std::vector<std::vector<std::uint8_t>> outgoing;
        FaderLaw law;

        Link linkState = Link::offline;
        int remoteVersion = 0;
        double lastHeard = 0.0, lastHelloSent = -1.0e9, lastPingSent = -1.0e9;
        std::uint16_t sessionNonce = 1, tokenCounter = 0;

        std::map<std::uint32_t, std::string> protectedChannels;   // hash -> title
        std::map<std::uint32_t, RestorePoint> restore;            // hash -> how it was
        std::map<std::uint32_t, bool> readWarned;                 // hash -> warned about R this session
        std::map<std::uint32_t, KnownFader> faders;               // hash -> last fader level seen
        Scan scan;
        std::map<std::uint32_t, PlanStep> planPending;
        PlanProgress plan;
        std::string moveResult;

        std::deque<LogLine> logBuffer;
        std::uint64_t logCount = 0, version = 0;
    };

    const char* toString (BridgeSession::Link);
    std::string formatDb (double db);
}
