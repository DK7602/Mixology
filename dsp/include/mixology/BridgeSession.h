#pragma once

#include "mixology/BridgeProtocol.h"
#include "mixology/FaderLaw.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

/** The plug-in side of the Cubase bridge: connection, channel identity, safe writes, readback.

    Framework-free and clock-injected (every call takes `now` in seconds) so the whole thing is
    unit-tested against a simulated Cubase. Runs on the message thread only, never the audio thread.

    Safety rules (Milestone 1):
    - Only a LOCKED channel can be written. Every write carries the locked channel's name hash and Cubase's
      own channel ID, and the Cubase script refuses it if the slot holds anything else by the time it arrives.
    - If the locked slot's name changes (bank moved, tracks reordered, track renamed) writing PAUSES
      until the user locks again. After a reconnect it resumes only if the same name is back in the same slot.
    - A channel that is writing automation is never written (no automation gets recorded by accident).
    - Before the first write, the channel's original volume / pan / mute / solo are saved (Restore).
    - dB moves are closed-loop: Cubase reports its own display text after each move, so a move is
      refined until Cubase shows the requested value (exactly, or within 0.05 dB
      when its fader steps can't land on it). */
namespace mixology
{
    struct BridgeSlot
    {
        bool present = false;
        std::string title;
        std::uint32_t hash = 0;
        std::uint32_t channelId = 0;   // hash of Cubase's channel ID, 0 = not reported

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

    struct BridgeSnapshot
    {
        bool valid = false;
        int slot = -1;
        std::string title;
        std::uint32_t hash = 0;
        double volume = 0.0;
        std::string volumeText;
        bool hasPan = false;
        double pan = 0.5;
        bool mute = false, solo = false;
    };

    class BridgeSession
    {
    public:
        enum class Link { offline, waiting, connected, lost };
        enum class Lock { none, locked, paused };

        struct Result
        {
            bool ok = false;
            std::string message;
        };

        struct LogLine
        {
            double time = 0.0;
            std::string text;
        };

        static constexpr int kMaxSlots = 32;
        static constexpr double kHelloInterval = 1.0;
        static constexpr double kPingInterval = 1.0;
        static constexpr double kLostAfter = 3.5;
        static constexpr double kReplyTimeout = 0.6;
        static constexpr double kDbExact = 0.005;      // Cubase shows 2 decimals: aim for its display to match
        static constexpr double kDbTolerance = 0.05;   // good enough when Cubase can't land exactly
        static constexpr int kMaxRefineSteps = 6;
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

        Lock lock() const { return lockState; }
        int lockedSlot() const { return lockSlot; }
        const std::string& lockedTitle() const { return lockTitle; }
        const std::string& pauseReason() const { return pausedBecause; }
        const BridgeSnapshot& snapshot() const { return original; }
        bool moveInProgress() const { return move.active; }
        const std::string& lastMoveResult() const { return moveResult; }

        FaderLaw& faderLaw() { return law; }
        const FaderLaw& faderLaw() const { return law; }

        // ---- commands --------------------------------------------------------------------------
        Result lockChannel (int slotIndex, double now);
        void unlock (double now);
        Result setVolumeDb (double db, double now);
        Result nudgeDb (double deltaDb, double now);
        /** Exact fader position, for dragging a fader in Mixology. Replaces a move in progress (latest wins). */
        Result setVolumeNormalised (double position, double now);
        /** Logs where a Mixology fader drag ended (drags themselves aren't logged step by step). */
        void noteDragFinished (double now);
        Result setPan (double normalised, double now);
        Result setMute (bool on, double now);
        Result setSolo (bool on, double now);
        Result restore (double now);
        Result bank (bridge::BankAction, double now);
        void requestDump (double now);

        // ---- log -------------------------------------------------------------------------------
        const std::deque<LogLine>& logLines() const { return logBuffer; }
        std::uint64_t logVersion() const { return logCount; }
        /** Bumps whenever anything the UI shows has changed. */
        std::uint64_t stateVersion() const { return version; }

    private:
        struct Move
        {
            bool active = false;
            double targetDb = 0.0;
            double sentValue = 0.0;
            double sentAt = 0.0;
            std::uint16_t token = 0;
            int step = 0;
            bool acked = false;
            bool byValue = false;   // exact position (drag): no dB refinement, no log per step
        };

        void send (const bridge::Message&);
        void handle (const bridge::Message&, double now);
        void onTitle (const bridge::Message&, double now);
        void onVolume (const bridge::Message&, double now);
        void connected (int scriptVer, int count, double now);
        void pause (const std::string& why, double now);
        Result checkWritable() const;
        bool isLockedChannel (const BridgeSlot&) const;
        bridge::Message writeMessage (bridge::Cmd);
        void takeSnapshotIfNeeded (double now);
        void sendVolume (double value, double now);
        std::uint16_t nextToken();
        void addLog (double now, std::string text);
        BridgeSlot& slotRef (int i);
        void changed() { ++version; }

        std::vector<BridgeSlot> slots;
        std::vector<std::vector<std::uint8_t>> outgoing;
        FaderLaw law;

        Link linkState = Link::offline;
        int remoteVersion = 0;
        double lastHeard = 0.0, lastHelloSent = -1.0e9, lastPingSent = -1.0e9;
        std::uint16_t sessionNonce = 1, tokenCounter = 0;

        Lock lockState = Lock::none;
        int lockSlot = -1;
        std::string lockTitle, pausedBecause;
        std::uint32_t lockHash = 0, lockId = 0;
        bool resumeIfSameAfterReconnect = false;

        BridgeSnapshot original;
        Move move;
        std::string moveResult;

        std::deque<LogLine> logBuffer;
        std::uint64_t logCount = 0, version = 0;
        double lastUserMoveLog = -1.0e9;
    };

    const char* toString (BridgeSession::Link);
    std::string formatDb (double db);
}
