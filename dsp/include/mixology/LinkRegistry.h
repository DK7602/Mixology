#pragma once

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
      thread writes the identity (name / colour, guarded by a sequence counter) and a heartbeat.
    - Mixology (main) reads everything from its message thread and can ask a Link to change its Trim; the
      Link's message thread picks the request up and applies it like a user move (host sees it, undo works). */
namespace mixology::link
{
    constexpr std::uint32_t kLayoutVersion = 1;
    constexpr int kMaxLinks = 256;
    constexpr std::size_t kNameBytes = 128;
    constexpr std::uint64_t kAliveMs = 2000;   // a Link without a heartbeat for this long is gone
    constexpr std::uint64_t kAudioMs = 1000;   // ... and without audio for this long isn't playing
    constexpr float kSilentDb = -100.0f;

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
        /** True (and the requested dB) when Mixology asked for a new Trim since `lastSeq`. */
        bool takeTrimRequest (int slot, std::uint32_t& lastSeq, float& db) const;
        /** How Mixology paired this Link lately (none when Mixology hasn't said anything for kAliveMs). */
        MatchStatus matchSeen (int slot, std::uint64_t nowMs) const;

        // ---- Mixology side ---------------------------------------------------------------------
        void mainHeartbeat (std::uint64_t nowMs);
        bool mainAlive (std::uint64_t nowMs) const;
        /** Every live Link (heartbeat within kAliveMs). */
        std::vector<LinkInfo> links (std::uint64_t nowMs) const;
        void requestTrim (int slot, std::uint32_t generation, float db);
        /** Tells a Link whether it is paired with a channel on the Tracks page (shown in its window). */
        void reportMatch (int slot, std::uint32_t generation, MatchStatus, std::uint64_t nowMs);

        Identity readIdentity (int slot) const;

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
