#pragma once

#include "mixology/AssistEq.h"
#include "mixology/ProcParams.h"
#include "mixology/TrackAnalyzer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/** Mixology Link <-> Mixology: what every Link on a track publishes, and what Mixology asks of it.

    Lives in a SharedBlock (one per host process), so both plug-ins see it although the host loads them as
    separate libraries. The layout is fixed and versioned (kLayoutVersion is part of the block's name, so an
    older and a newer build never read each other's memory). Zero-filled memory is a valid, empty registry.

    Threads:
    - Each Link owns one slot. Its audio thread writes the meters (atomics, never blocks); its message
      thread writes the identity (name / colour, guarded by a sequence counter) and a heartbeat; its analysis
      thread writes an AnalysisFrame about five times a second (guarded by its own sequence counter).
    - Mixology (main) reads everything from its message thread and can ask a Link to change its Trim or its
      Assist EQ; the Link's message thread picks the request up and applies it like a user move (host sees
      it, undo works). */
namespace mixology::link
{
    constexpr std::uint32_t kLayoutVersion = 5;   // 2: analysis frames (M3), 3: + left / right balance, 4: + Assist EQ (M4), 5: + processing (M5)
    constexpr std::uint32_t kNoRequestSeen = 0xFFFFFFFFu;   // a Link's first look at a request counter: take none
    constexpr int kMaxLinks = 256;
    constexpr std::size_t kNameBytes = 128;
    constexpr std::uint64_t kAliveMs = 2000;   // a Link without a heartbeat for this long is gone
    constexpr std::uint64_t kAudioMs = 1000;   // ... and without audio for this long isn't playing
    constexpr float kSilentDb = -100.0f;
    constexpr int kProcSlots = 192;            // room for every processing control (proc::count today, more later)
    static_assert (proc::count <= kProcSlots);

    /** "MixologyLink.v<layout>" (SharedBlock adds the process ID). */
    std::string blockName();

    struct alignas (64) SlotData
    {
        std::atomic<std::uint32_t> state;          // 0 free, 1 claimed
        std::atomic<std::uint32_t> generation;     // bumps on every claim
        std::atomic<std::uint64_t> instanceId;     // random, chosen by the owner
        std::atomic<std::uint64_t> heartbeatMs;    // owner's message thread
        std::atomic<std::uint64_t> audioMs;        // owner's last audio block

        std::atomic<std::uint32_t> identitySeq;    // odd while the identity below is being written
        std::atomic<std::uint32_t> nameHash;       // same hash as the Cubase bridge (FNV-1a, 28 bits)
        std::atomic<std::uint32_t> rgb;            // 0xRRGGBB
        std::atomic<std::uint32_t> flags;          // bit 0: has a name, bit 1: has a colour
        std::array<std::atomic<char>, kNameBytes> name;   // UTF-8, zero-terminated

        std::atomic<float> peakL, peakR;           // dBFS, highest sample in the last meter window
        std::atomic<float> momentary, shortTerm;   // LUFS
        std::atomic<std::uint32_t> channels;
        std::atomic<float> trimDb;                 // the Link's Trim as it is now

        std::atomic<std::uint32_t> trimRequestSeq; // Mixology -> Link: bump to ask for trimRequestDb
        std::atomic<float> trimRequestDb;

        std::atomic<std::uint32_t> mainMatch;      // Mixology -> Link: MatchStatus with the channel in view
        std::atomic<std::uint64_t> mainMatchMs;    // when Mixology last said so

        // Analysis (Link's analysis thread): the latest AnalysisFrame, odd analysisSeq while it is written.
        std::atomic<std::uint32_t> analysisSeq;
        std::atomic<std::uint64_t> analysisMs;     // when it was measured (0 = never)
        std::atomic<std::uint32_t> analysisFlags;  // bit 0 silent, bit 1 transport playing
        std::atomic<std::int64_t> songSample;      // song position (samples) at that moment
        std::array<std::atomic<float>, kBands> bandDb;
        std::array<std::atomic<float>, kBands> sideShare;
        std::atomic<float> aLoudness, aRms, aPeak, aCorrelation, aWidth, aBalance;

        // Assist EQ (low cut Hz, mud Hz, mud dB, pocket Hz, pocket dB): as it is now (Link), and requests
        // (Mixology; odd assistRequestSeq while they are being written).
        std::array<std::atomic<float>, 5> assistNow;
        std::atomic<std::uint32_t> assistRequestSeq;
        std::array<std::atomic<float>, 5> assistRequest;

        // Processing (M5): every control as it is now (Link's message thread), one request counter and value
        // per control (Mixology: value first, then bump the counter), and the processing meters (Link's audio
        // thread, once per meter window).
        std::atomic<std::uint32_t> procCount;      // controls this Link knows (0: no processing)
        std::array<std::atomic<float>, kProcSlots> procNow;
        std::array<std::atomic<std::uint32_t>, kProcSlots> procRequestSeq;
        std::array<std::atomic<float>, kProcSlots> procRequest;
        std::array<std::atomic<float>, proc::kMeterValues> procMeters;
        std::atomic<std::uint32_t> procRunning;    // bit per proc::Module
        std::atomic<std::uint64_t> procMetersMs;
        std::atomic<float> sampleRate;
        std::atomic<std::uint32_t> latency;        // samples (the Limiter's lookahead while it is on)
    };

    struct alignas (64) Header
    {
        std::atomic<std::uint32_t> layout;         // kLayoutVersion once someone has used the block
        std::atomic<std::uint64_t> mainHeartbeatMs;// Mixology (main) is open somewhere in this process
    };

    struct Block
    {
        Header header;
        std::array<SlotData, kMaxLinks> slots;
    };

    static_assert (std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert (std::atomic<float>::is_always_lock_free);
    static_assert (std::atomic<char>::is_always_lock_free);
    static_assert (std::atomic<std::int64_t>::is_always_lock_free);

    enum class MatchStatus : std::uint32_t { none = 0, matched = 1, ambiguous = 2 };

    struct Identity
    {
        std::string name;
        bool hasName = false;
        std::uint32_t rgb = 0;
        bool hasColour = false;
    };

    /** One Link as Mixology sees it. */
    struct LinkInfo
    {
        int slot = -1;
        std::uint32_t generation = 0;
        std::uint64_t instanceId = 0;
        Identity identity;
        std::uint32_t nameHash = 0;
        float peakL = kSilentDb, peakR = kSilentDb, momentary = kSilentDb, shortTerm = kSilentDb, trimDb = 0.0f;
        int channels = 0;
        bool audio = false;   // processed audio in the last second

        bool hasAnalysis = false;   // a frame measured within the last second
        AnalysisFrame analysis;
        bool playing = false;       // the host's transport was running at that moment
        std::int64_t songSample = 0;
        std::uint64_t analysisMs = 0;

        AssistSettings assist;      // the Link's Assist EQ as it is now

        bool hasProcessing = false; // a Link with the M5 processing
        std::uint32_t procRunning = 0;   // modules processing right now (bit per proc::Module)
        float sampleRate = 0.0f;
        int latency = 0;
    };

    class Registry
    {
    public:
        /** `memory` must hold sizeof (Block) bytes, zero-filled or already used by another Registry. */
        explicit Registry (void* memory);

        static constexpr std::size_t bytes() { return sizeof (Block); }

        // ---- Link side -------------------------------------------------------------------------
        int claim (std::uint64_t instanceId, std::uint64_t nowMs);   // -1 when all slots are taken
        void release (int slot, std::uint64_t instanceId);
        void setIdentity (int slot, const Identity&);
        void heartbeat (int slot, std::uint64_t nowMs);
        /** Audio thread: lock-free, never blocks. */
        void publishMeters (int slot, float peakL, float peakR, float momentary, float shortTerm, int channels, std::uint64_t nowMs) noexcept;
        void publishTrim (int slot, float db) noexcept;
        /** True (and the requested dB) when Mixology asked for a new Trim since `lastSeq`. Start `lastSeq` at
            kNoRequestSeen: the first call only learns the counter (a request meant for an earlier Link in
            this slot is never applied). */
        bool takeTrimRequest (int slot, std::uint32_t& lastSeq, float& db) const;
        void publishAssist (int slot, const AssistSettings&) noexcept;
        /** Like takeTrimRequest, for the Assist EQ (all five values at once). */
        bool takeAssistRequest (int slot, std::uint32_t& lastSeq, AssistSettings&) const;
        /** How Mixology paired this Link lately (none when Mixology hasn't said anything for kAliveMs). */
        MatchStatus matchSeen (int slot, std::uint64_t nowMs) const;
        /** Analysis thread: one writer per slot. */
        void publishAnalysis (int slot, const AnalysisFrame&, bool playing, std::int64_t songSample, std::uint64_t nowMs);
        /** Processing: the controls as they are now, and the Link's sample rate and latency (message thread). */
        void publishProc (int slot, const proc::Values&, float sampleRate, int latency);
        /** Audio thread: lock-free. */
        void publishProcMeters (int slot, const proc::Meters&, std::uint64_t nowMs) noexcept;
        /** Remembers every request counter as it is (call right after claim(): older requests are not ours). */
        void learnProcRequests (int slot, std::array<std::uint32_t, kProcSlots>& seen) const;
        /** Requests made since the last call: control indices and values (at most `max`), newest value each. */
        int takeProcRequests (int slot, std::array<std::uint32_t, kProcSlots>& seen, int* indices, float* values, int max) const;

        // ---- Mixology side ---------------------------------------------------------------------
        void mainHeartbeat (std::uint64_t nowMs);
        bool mainAlive (std::uint64_t nowMs) const;
        /** Every live Link (heartbeat within kAliveMs). */
        std::vector<LinkInfo> links (std::uint64_t nowMs) const;
        void requestTrim (int slot, std::uint32_t generation, float db);
        void requestAssist (int slot, std::uint32_t generation, const AssistSettings&);
        void requestProc (int slot, std::uint32_t generation, int index, float value);
        /** A Link's processing controls as it published them last (false: no such Link, or no processing). */
        bool readProc (int slot, std::uint32_t generation, proc::Values&) const;
        /** Its processing meters (false when it hasn't processed audio within kAudioMs). */
        bool readProcMeters (int slot, std::uint32_t generation, proc::Meters&, std::uint64_t nowMs) const;
        /** Tells a Link whether it is paired with a channel on the Tracks page (shown in its window). */
        void reportMatch (int slot, std::uint32_t generation, MatchStatus, std::uint64_t nowMs);

        Identity readIdentity (int slot) const;
        /** The latest frame; false if there is none or a writer kept getting in the way. */
        bool readAnalysis (int slot, AnalysisFrame&, bool& playing, std::int64_t& songSample, std::uint64_t& whenMs) const;

    private:
        Block& block;
        SlotData* slotPtr (int slot) const;
    };

    // ---------------------------------------------------------------------------------------------
    /** Pairs the channels in the Cubase bank with the Links that report the same name (and, when names
        repeat, the same colour). A channel without a confident match gets none: Mixology never guesses. */
    struct ChannelKey
    {
        bool present = false;
        std::uint32_t hash = 0;
        bool hasColour = false;
        std::uint32_t rgb = 0;
    };

    struct Match
    {
        MatchStatus status = MatchStatus::none;
        int link = -1;   // index into the `links` vector
    };

    std::vector<Match> matchLinks (const std::vector<ChannelKey>& channels, const std::vector<LinkInfo>& links);
    bool coloursClose (std::uint32_t a, std::uint32_t b);

    // ---------------------------------------------------------------------------------------------
    /** This library's handle on the registry of the host process (opened on first use, kept until the
        library unloads). Each plug-in library has its own handle; they all reach the same memory. */
    Registry& processRegistry();
    /** False when the OS refused shared memory: Links and Mixology then can't see each other. */
    bool processRegistryShared();
    /** Milliseconds on a clock every library in the process shares (steady, never 0). */
    std::uint64_t nowMs();
}
