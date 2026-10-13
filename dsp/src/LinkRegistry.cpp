#include "mixology/LinkRegistry.h"

#include "mixology/BridgeProtocol.h"
#include "mixology/SharedBlock.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace mixology::link
{
    namespace
    {
        constexpr auto kRelaxed = std::memory_order_relaxed;
        constexpr auto kAcquire = std::memory_order_acquire;
        constexpr auto kRelease = std::memory_order_release;
    }

    std::string blockName() { return "MixologyLink.v" + std::to_string (kLayoutVersion); }

    Registry::Registry (void* memory)
        : block (*static_cast<Block*> (memory))
    {
        block.header.layout.store (kLayoutVersion, kRelaxed);
    }

    SlotData* Registry::slotPtr (int slot) const
    {
        if (slot < 0 || slot >= kMaxLinks) return nullptr;
        return &block.slots[static_cast<std::size_t> (slot)];
    }

    // ---------------------------------------------------------------------------------------------
    // Link side

    int Registry::claim (std::uint64_t instanceId, std::uint64_t nowMs)
    {
        for (int i = 0; i < kMaxLinks; ++i)
        {
            auto& s = block.slots[static_cast<std::size_t> (i)];
            std::uint32_t expected = 0;
            if (! s.state.compare_exchange_strong (expected, 1u, std::memory_order_acq_rel)) continue;

            s.instanceId.store (instanceId, kRelaxed);
            s.flags.store (0, kRelaxed);
            s.nameHash.store (0, kRelaxed);
            s.peakL.store (kSilentDb, kRelaxed);
            s.peakR.store (kSilentDb, kRelaxed);
            s.momentary.store (kSilentDb, kRelaxed);
            s.shortTerm.store (kSilentDb, kRelaxed);
            s.trimDb.store (0.0f, kRelaxed);
            s.audioMs.store (0, kRelaxed);
            s.mainMatchMs.store (0, kRelaxed);
            s.analysisMs.store (0, kRelaxed);
            s.heartbeatMs.store (nowMs, kRelaxed);
            s.generation.fetch_add (1, kRelease);
            return i;
        }
        return -1;
    }

    void Registry::release (int slot, std::uint64_t instanceId)
    {
        auto* s = slotPtr (slot);
        if (s == nullptr || s->instanceId.load (kRelaxed) != instanceId) return;
        s->heartbeatMs.store (0, kRelaxed);
        s->flags.store (0, kRelaxed);
        s->instanceId.store (0, kRelaxed);
        s->state.store (0, kRelease);
    }

    void Registry::setIdentity (int slot, const Identity& id)
    {
        auto* s = slotPtr (slot);
        if (s == nullptr) return;

        // Seqlock: odd while writing, so a reader that overlaps the write retries.
        const auto seq = s->identitySeq.load (kRelaxed);
        s->identitySeq.store (seq + 1, kRelaxed);
        std::atomic_thread_fence (kRelease);

        std::string name = id.hasName ? id.name : std::string();
        if (name.size() > kNameBytes - 1)
        {
            std::size_t cut = kNameBytes - 1;
            while (cut > 0 && (static_cast<unsigned char> (name[cut]) & 0xC0) == 0x80) --cut;   // UTF-8 boundary
            name.resize (cut);
        }
        for (std::size_t i = 0; i < kNameBytes; ++i)
            s->name[i].store (i < name.size() ? name[i] : '\0', kRelaxed);
        s->nameHash.store (id.hasName ? bridge::nameHash (id.name) : 0u, kRelaxed);   // hash of the FULL name
        s->rgb.store (id.rgb & 0xFFFFFFu, kRelaxed);
        s->flags.store ((id.hasName ? 1u : 0u) | (id.hasColour ? 2u : 0u), kRelaxed);

        s->identitySeq.store (seq + 2, kRelease);
    }

    Identity Registry::readIdentity (int slot) const
    {
        Identity out;
        auto* s = slotPtr (slot);
        if (s == nullptr) return out;

        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const auto before = s->identitySeq.load (kAcquire);
            if (before & 1u) continue;
            std::string name;
            for (std::size_t i = 0; i < kNameBytes; ++i)
            {
                const char c = s->name[i].load (kRelaxed);
                if (c == '\0') break;
                name.push_back (c);
            }
            const auto rgb = s->rgb.load (kRelaxed);
            const auto flags = s->flags.load (kRelaxed);
            std::atomic_thread_fence (kAcquire);
            if (s->identitySeq.load (kRelaxed) != before) continue;

            out.name = std::move (name);
            out.hasName = (flags & 1u) != 0;
            out.rgb = rgb;
            out.hasColour = (flags & 2u) != 0;
            return out;
        }
        return out;   // a writer kept getting in the way: report nothing this time
    }

    void Registry::heartbeat (int slot, std::uint64_t nowMs)
    {
        if (auto* s = slotPtr (slot)) s->heartbeatMs.store (nowMs, kRelaxed);
    }

    void Registry::publishMeters (int slot, float peakL, float peakR, float momentary, float shortTerm, int channels, std::uint64_t nowMs) noexcept
    {
        auto* s = slotPtr (slot);
        if (s == nullptr) return;
        s->peakL.store (peakL, kRelaxed);
        s->peakR.store (peakR, kRelaxed);
        s->momentary.store (momentary, kRelaxed);
        s->shortTerm.store (shortTerm, kRelaxed);
        s->channels.store (static_cast<std::uint32_t> (std::max (0, channels)), kRelaxed);
        s->audioMs.store (nowMs, kRelease);
    }

    void Registry::publishTrim (int slot, float db) noexcept
    {
        if (auto* s = slotPtr (slot)) s->trimDb.store (db, kRelaxed);
    }

    bool Registry::takeTrimRequest (int slot, std::uint32_t& lastSeq, float& db) const
    {
        auto* s = slotPtr (slot);
        if (s == nullptr) return false;
        const auto seq = s->trimRequestSeq.load (kAcquire);
        if (seq == lastSeq) return false;
        lastSeq = seq;
        db = s->trimRequestDb.load (kRelaxed);
        return true;
    }

    MatchStatus Registry::matchSeen (int slot, std::uint64_t nowMs) const
    {
        auto* s = slotPtr (slot);
        if (s == nullptr) return MatchStatus::none;
        const auto t = s->mainMatchMs.load (kAcquire);
        if (t == 0 || nowMs - std::min (nowMs, t) > kAliveMs) return MatchStatus::none;
        return static_cast<MatchStatus> (s->mainMatch.load (kRelaxed));
    }

    void Registry::publishAnalysis (int slot, const AnalysisFrame& f, bool playing, std::int64_t songSample, std::uint64_t nowMs)
    {
        auto* s = slotPtr (slot);
        if (s == nullptr) return;
        const auto seq = s->analysisSeq.load (kRelaxed);
        s->analysisSeq.store (seq + 1, kRelaxed);
        std::atomic_thread_fence (kRelease);
        for (std::size_t b = 0; b < static_cast<std::size_t> (kBands); ++b)
        {
            s->bandDb[b].store (f.bandDb[b], kRelaxed);
            s->sideShare[b].store (f.sideShare[b], kRelaxed);
        }
        s->aLoudness.store (f.loudness, kRelaxed);
        s->aRms.store (f.rmsDb, kRelaxed);
        s->aPeak.store (f.peakDb, kRelaxed);
        s->aCorrelation.store (f.correlation, kRelaxed);
        s->aWidth.store (f.width, kRelaxed);
        s->analysisFlags.store ((f.silent ? 1u : 0u) | (playing ? 2u : 0u), kRelaxed);
        s->songSample.store (songSample, kRelaxed);
        s->analysisMs.store (nowMs, kRelaxed);
        s->analysisSeq.store (seq + 2, kRelease);
    }

    bool Registry::readAnalysis (int slot, AnalysisFrame& f, bool& playing, std::int64_t& songSample, std::uint64_t& whenMs) const
    {
        auto* s = slotPtr (slot);
        if (s == nullptr) return false;
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const auto before = s->analysisSeq.load (kAcquire);
            if (before & 1u) continue;
            AnalysisFrame out;
            for (std::size_t b = 0; b < static_cast<std::size_t> (kBands); ++b)
            {
                out.bandDb[b] = s->bandDb[b].load (kRelaxed);
                out.sideShare[b] = s->sideShare[b].load (kRelaxed);
            }
            out.loudness = s->aLoudness.load (kRelaxed);
            out.rmsDb = s->aRms.load (kRelaxed);
            out.peakDb = s->aPeak.load (kRelaxed);
            out.correlation = s->aCorrelation.load (kRelaxed);
            out.width = s->aWidth.load (kRelaxed);
            const auto flags = s->analysisFlags.load (kRelaxed);
            const auto pos = s->songSample.load (kRelaxed);
            const auto when = s->analysisMs.load (kRelaxed);
            std::atomic_thread_fence (kAcquire);
            if (s->analysisSeq.load (kRelaxed) != before) continue;
            if (when == 0) return false;
            out.silent = (flags & 1u) != 0;
            f = out;
            playing = (flags & 2u) != 0;
            songSample = pos;
            whenMs = when;
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------------------------------------
    // Mixology side

    void Registry::mainHeartbeat (std::uint64_t nowMs) { block.header.mainHeartbeatMs.store (nowMs, kRelaxed); }

    bool Registry::mainAlive (std::uint64_t nowMs) const
    {
        const auto t = block.header.mainHeartbeatMs.load (kRelaxed);
        return t != 0 && nowMs - std::min (nowMs, t) <= kAliveMs;
    }

    std::vector<LinkInfo> Registry::links (std::uint64_t nowMs) const
    {
        std::vector<LinkInfo> out;
        for (int i = 0; i < kMaxLinks; ++i)
        {
            const auto& s = block.slots[static_cast<std::size_t> (i)];
            if (s.state.load (kAcquire) != 1u) continue;
            const auto beat = s.heartbeatMs.load (kRelaxed);
            if (beat == 0 || nowMs - std::min (nowMs, beat) > kAliveMs) continue;

            LinkInfo info;
            info.slot = i;
            info.generation = s.generation.load (kRelaxed);
            info.instanceId = s.instanceId.load (kRelaxed);
            info.identity = readIdentity (i);
            info.nameHash = info.identity.hasName ? s.nameHash.load (kRelaxed) : 0u;
            const auto audio = s.audioMs.load (kAcquire);
            info.audio = audio != 0 && nowMs - std::min (nowMs, audio) <= kAudioMs;
            info.peakL = info.audio ? s.peakL.load (kRelaxed) : kSilentDb;
            info.peakR = info.audio ? s.peakR.load (kRelaxed) : kSilentDb;
            info.momentary = info.audio ? s.momentary.load (kRelaxed) : kSilentDb;
            info.shortTerm = info.audio ? s.shortTerm.load (kRelaxed) : kSilentDb;
            info.channels = static_cast<int> (s.channels.load (kRelaxed));
            info.trimDb = s.trimDb.load (kRelaxed);
            std::uint64_t when = 0;
            if (readAnalysis (i, info.analysis, info.playing, info.songSample, when))
            {
                info.analysisMs = when;
                info.hasAnalysis = nowMs - std::min (nowMs, when) <= kAudioMs;
            }
            out.push_back (std::move (info));
        }
        return out;
    }

    void Registry::requestTrim (int slot, std::uint32_t generation, float db)
    {
        auto* s = slotPtr (slot);
        if (s == nullptr || s->generation.load (kRelaxed) != generation) return;   // that Link is gone
        s->trimRequestDb.store (db, kRelaxed);
        s->trimRequestSeq.fetch_add (1, kRelease);
    }

    void Registry::reportMatch (int slot, std::uint32_t generation, MatchStatus status, std::uint64_t nowMs)
    {
        auto* s = slotPtr (slot);
        if (s == nullptr || s->generation.load (kRelaxed) != generation) return;
        s->mainMatch.store (static_cast<std::uint32_t> (status), kRelaxed);
        s->mainMatchMs.store (nowMs, kRelease);
    }

    // ---------------------------------------------------------------------------------------------
    // Matching

    bool coloursClose (std::uint32_t a, std::uint32_t b)
    {
        for (int shift : { 16, 8, 0 })
            if (std::abs (static_cast<int> ((a >> shift) & 0xFF) - static_cast<int> ((b >> shift) & 0xFF)) > 12) return false;
        return true;
    }

    std::vector<Match> matchLinks (const std::vector<ChannelKey>& channels, const std::vector<LinkInfo>& links)
    {
        std::vector<Match> out (channels.size());

        for (std::size_t c = 0; c < channels.size(); ++c)
        {
            const auto& ch = channels[c];
            if (! ch.present || ch.hash == 0) continue;

            std::vector<int> sameName;
            for (std::size_t l = 0; l < links.size(); ++l)
                if (links[l].identity.hasName && links[l].nameHash == ch.hash) sameName.push_back (static_cast<int> (l));
            if (sameName.empty()) continue;

            std::vector<std::size_t> twins;   // channels in the bank with the same name (including this one)
            for (std::size_t o = 0; o < channels.size(); ++o)
                if (channels[o].present && channels[o].hash == ch.hash) twins.push_back (o);

            if (sameName.size() == 1 && twins.size() == 1)
            {
                out[c] = { MatchStatus::matched, sameName.front() };
                continue;
            }

            // Names repeat: the colour has to settle it, both ways (one Link for this channel, and
            // that Link fits no other channel with the same name).
            out[c].status = MatchStatus::ambiguous;
            if (! ch.hasColour) continue;
            std::vector<int> byColour;
            for (int l : sameName)
            {
                const auto& id = links[static_cast<std::size_t> (l)].identity;
                if (id.hasColour && coloursClose (id.rgb, ch.rgb)) byColour.push_back (l);
            }
            if (byColour.size() != 1) continue;
            const auto& linkColour = links[static_cast<std::size_t> (byColour.front())].identity.rgb;
            int fits = 0;
            for (auto o : twins)
                if (channels[o].hasColour && coloursClose (channels[o].rgb, linkColour)) ++fits;
            if (fits == 1) out[c] = { MatchStatus::matched, byColour.front() };
        }
        return out;
    }

    // ---------------------------------------------------------------------------------------------

    namespace
    {
        struct ProcessHandle
        {
            SharedBlock block { SharedBlock::perProcessName (blockName()), Registry::bytes() };
            Registry registry { block.data() };
        };

        ProcessHandle& handle()
        {
            static ProcessHandle h;
            return h;
        }
    }

    Registry& processRegistry() { return handle().registry; }
    bool processRegistryShared() { return handle().block.isShared(); }

    std::uint64_t nowMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint64_t> (duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count()) + 1;
    }
}
