#include "mixology/BridgeProtocol.h"

#include <cmath>

namespace mixology::bridge
{
    namespace
    {
        // Which fields each command carries, in order.
        enum Field : unsigned
        {
            fSlot = 1u << 0, fToken = 1u << 1, fHash = 1u << 2, fValue = 1u << 3,
            fFlag = 1u << 4, fRgb = 1u << 5, fText = 1u << 6, fId = 1u << 7,
        };

        std::optional<unsigned> layoutFor (Cmd cmd)
        {
            switch (cmd)
            {
                case Cmd::hello:          return fToken;
                case Cmd::requestDump:    return 0u;
                case Cmd::setVolume:
                case Cmd::setPan:         return fSlot | fToken | fHash | fId | fValue;
                case Cmd::setMute:
                case Cmd::setSolo:        return fSlot | fToken | fHash | fId | fFlag;
                case Cmd::bank:           return fFlag;
                case Cmd::ping:           return fToken;
                case Cmd::helloAck:       return fFlag | fSlot | fToken;
                case Cmd::slotTitle:      return fSlot | fHash | fId | fText;
                case Cmd::slotVolume:
                case Cmd::slotPan:        return fSlot | fValue | fText;
                case Cmd::slotMute:
                case Cmd::slotSolo:       return fSlot | fFlag;
                case Cmd::slotColour:     return fSlot | fRgb | fFlag;
                case Cmd::slotAutomation: return fSlot | fFlag;
                case Cmd::ack:            return fToken | fFlag;
                case Cmd::log:            return fText;
                case Cmd::pong:           return fToken;
            }
            return std::nullopt;
        }

        // Field order inside a frame (fixed, independent of the bit values above).
        constexpr Field kOrder[] = { fFlag, fSlot, fToken, fHash, fId, fValue, fRgb, fText };

        void put7 (std::vector<std::uint8_t>& out, std::uint32_t v, int groups)
        {
            for (int g = groups - 1; g >= 0; --g)
                out.push_back (static_cast<std::uint8_t> ((v >> (7 * g)) & 0x7Fu));
        }

        struct Reader
        {
            const std::uint8_t* p;
            std::size_t left;

            bool get7 (std::uint32_t& v, int groups)
            {
                if (left < static_cast<std::size_t> (groups)) return false;
                v = 0;
                for (int g = 0; g < groups; ++g)
                {
                    if (p[g] > 0x7F) return false;
                    v = (v << 7) | p[g];
                }
                p += groups;
                left -= static_cast<std::size_t> (groups);
                return true;
            }
        };
    }

    std::uint32_t nameHash (const std::string& utf8)
    {
        std::uint32_t h = 2166136261u;
        for (unsigned char c : utf8)
        {
            h ^= c;
            h *= 16777619u;
        }
        return h & 0x0FFFFFFFu;
    }

    std::string truncateUtf8 (const std::string& utf8, std::size_t maxBytes)
    {
        if (utf8.size() <= maxBytes) return utf8;
        std::size_t cut = maxBytes;
        // Step back over continuation bytes (10xxxxxx) so a multi-byte character is never split.
        while (cut > 0 && (static_cast<unsigned char> (utf8[cut]) & 0xC0u) == 0x80u)
            --cut;
        return utf8.substr (0, cut);
    }

    std::vector<std::uint8_t> encode (const Message& m)
    {
        std::vector<std::uint8_t> out { kSysexStart, kManufacturer, kTag1, kTag2, kProtocolVersion,
                                        static_cast<std::uint8_t> (m.cmd) };
        const auto layout = layoutFor (m.cmd);
        if (! layout) return {};

        for (auto f : kOrder)
        {
            if ((*layout & f) == 0) continue;
            switch (f)
            {
                case fFlag:  put7 (out, static_cast<std::uint32_t> (m.flag) & 0x7Fu, 1); break;
                case fSlot:  put7 (out, static_cast<std::uint32_t> (m.slot) & 0x7Fu, 1); break;
                case fToken: put7 (out, m.token & 0x3FFFu, 2); break;
                case fHash:  put7 (out, m.hash & 0x0FFFFFFFu, 4); break;
                case fId:    put7 (out, m.channelId & 0x0FFFFFFFu, 4); break;
                case fValue:
                {
                    const double v = std::isfinite (m.value) ? std::fmin (1.0, std::fmax (0.0, m.value)) : 0.0;
                    put7 (out, static_cast<std::uint32_t> (std::lround (v * kValueMax)), 3);
                    break;
                }
                case fRgb:   put7 (out, m.rgb & 0xFFFFFFu, 4); break;
                case fText:
                {
                    const auto t = truncateUtf8 (m.text, kMaxTextBytes);
                    out.push_back (static_cast<std::uint8_t> (t.size()));
                    for (unsigned char c : t)
                    {
                        out.push_back (static_cast<std::uint8_t> (c >> 4));
                        out.push_back (static_cast<std::uint8_t> (c & 0x0Fu));
                    }
                    break;
                }
            }
        }
        out.push_back (kSysexEnd);
        return out;
    }

    std::optional<Message> decode (const std::uint8_t* data, std::size_t size)
    {
        if (data == nullptr || size < 7) return std::nullopt;
        if (data[0] != kSysexStart || data[size - 1] != kSysexEnd) return std::nullopt;
        if (data[1] != kManufacturer || data[2] != kTag1 || data[3] != kTag2) return std::nullopt;
        if (data[4] != kProtocolVersion) return std::nullopt;

        Message m;
        m.cmd = static_cast<Cmd> (data[5]);
        const auto layout = layoutFor (m.cmd);
        if (! layout) return std::nullopt;

        Reader r { data + 6, size - 7 };
        std::uint32_t v = 0;
        for (auto f : kOrder)
        {
            if ((*layout & f) == 0) continue;
            switch (f)
            {
                case fFlag:  if (! r.get7 (v, 1)) return std::nullopt; m.flag = static_cast<int> (v); break;
                case fSlot:  if (! r.get7 (v, 1)) return std::nullopt; m.slot = static_cast<int> (v); break;
                case fToken: if (! r.get7 (v, 2)) return std::nullopt; m.token = static_cast<std::uint16_t> (v); break;
                case fHash:  if (! r.get7 (v, 4)) return std::nullopt; m.hash = v; break;
                case fId:    if (! r.get7 (v, 4)) return std::nullopt; m.channelId = v; break;
                case fValue: if (! r.get7 (v, 3)) return std::nullopt; m.value = static_cast<double> (v) / kValueMax; break;
                case fRgb:   if (! r.get7 (v, 4)) return std::nullopt; m.rgb = v & 0xFFFFFFu; break;
                case fText:
                {
                    std::uint32_t n = 0;
                    if (! r.get7 (n, 1) || n > kMaxTextBytes) return std::nullopt;
                    m.text.clear();
                    for (std::uint32_t i = 0; i < n; ++i)
                    {
                        std::uint32_t hi = 0, lo = 0;
                        if (! r.get7 (hi, 1) || ! r.get7 (lo, 1) || hi > 15 || lo > 15) return std::nullopt;
                        m.text.push_back (static_cast<char> ((hi << 4) | lo));
                    }
                    break;
                }
            }
        }
        if (r.left != 0) return std::nullopt;   // trailing bytes: not a frame we understand
        return m;
    }

    const char* commandName (Cmd cmd)
    {
        switch (cmd)
        {
            case Cmd::hello:          return "hello";
            case Cmd::requestDump:    return "requestDump";
            case Cmd::setVolume:      return "setVolume";
            case Cmd::setPan:         return "setPan";
            case Cmd::setMute:        return "setMute";
            case Cmd::setSolo:        return "setSolo";
            case Cmd::bank:           return "bank";
            case Cmd::ping:           return "ping";
            case Cmd::helloAck:       return "helloAck";
            case Cmd::slotTitle:      return "slotTitle";
            case Cmd::slotVolume:     return "slotVolume";
            case Cmd::slotPan:        return "slotPan";
            case Cmd::slotMute:       return "slotMute";
            case Cmd::slotSolo:       return "slotSolo";
            case Cmd::slotColour:     return "slotColour";
            case Cmd::slotAutomation: return "slotAutomation";
            case Cmd::ack:            return "ack";
            case Cmd::log:            return "log";
            case Cmd::pong:           return "pong";
        }
        return "?";
    }

    const char* ackStatusText (AckStatus s)
    {
        switch (s)
        {
            case AckStatus::applied:         return "applied";
            case AckStatus::nameMismatch:    return "refused: the slot holds a different channel now";
            case AckStatus::emptySlot:       return "refused: no channel in that slot";
            case AckStatus::badMessage:      return "refused: message not understood";
            case AckStatus::automationWrite: return "refused: the channel is writing automation";
        }
        return "?";
    }
}
